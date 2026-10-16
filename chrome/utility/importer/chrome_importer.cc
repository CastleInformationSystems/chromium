#include "chrome/utility/importer/chrome_importer.h"

#include <memory>
#include <string>
#include <vector>

#include "base/files/file_util.h"
#include "base/json/json_reader.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/utf_string_conversions.h"
#include "base/time/time.h"
#include "base/values.h"
#include "chrome/common/importer/importer_autofill_form_data_entry.h"
#include "chrome/common/importer/importer_bridge.h"
#include "chrome/common/importer/profile_import.mojom.h"
#include "components/history/core/browser/history_types.h"
#include "components/user_data_importer/common/imported_bookmark_entry.h"
#include "sql/database.h"
#include "sql/statement.h"
#include "url/gurl.h"

namespace {

void ParseBookmarksTree(const base::DictValue* node,
                        std::vector<std::u16string>& current_path,
                        std::vector<user_data_importer::ImportedBookmarkEntry>& out_bookmarks,
                        bool in_toolbar) {
  if (!node) return;

  const std::string* type = node->FindString("type");
  
  if (type && *type == "url") {
    user_data_importer::ImportedBookmarkEntry bookmark;
    const std::string* url = node->FindString("url");
    const std::string* name = node->FindString("name");
    
    if (url) bookmark.url = GURL(*url);
    if (name) bookmark.title = base::UTF8ToUTF16(*name);
    
    bookmark.path = current_path;
    bookmark.in_toolbar = in_toolbar;
    bookmark.is_folder = false;

    const std::string* date_added_str = node->FindString("date_added");
    if (date_added_str) {
      int64_t date_added_internal = 0;
      if (base::StringToInt64(*date_added_str, &date_added_internal)) {
        bookmark.creation_time = base::Time::FromInternalValue(date_added_internal);
      }
    }

    if (bookmark.url.is_valid()) {
      out_bookmarks.push_back(std::move(bookmark));
    }
  } else if (type && *type == "folder") {
    const std::string* name = node->FindString("name");
    if (name) {
      current_path.push_back(base::UTF8ToUTF16(*name));
    }

    const base::ListValue* children = node->FindList("children");
    if (children) {
      for (const auto& child : *children) {
        if (const base::DictValue* child_dict = child.GetIfDict()) {
          ParseBookmarksTree(child_dict, current_path, out_bookmarks, in_toolbar);
        }
      }
    }

    if (name) {
      current_path.pop_back();
    }
  }
}

}  // namespace

ChromeImporter::ChromeImporter() = default;
ChromeImporter::~ChromeImporter() = default;

void ChromeImporter::StartImport(const user_data_importer::SourceProfile& source_profile,
                                 uint16_t items,
                                 ImporterBridge* bridge) {
  bridge_ = bridge;
  source_path_ = source_profile.source_path;
  cancelled_ = false;

  bridge_->NotifyStarted();

  if (!temp_dir_.CreateUniqueTempDir()) {
    bridge_->NotifyEnded();
    return;
  }

  if ((items & user_data_importer::HISTORY) && !cancelled_) {
    bridge_->NotifyItemStarted(user_data_importer::HISTORY);
    ImportHistory();
    bridge_->NotifyItemEnded(user_data_importer::HISTORY);
  }

  if ((items & user_data_importer::FAVORITES) && !cancelled_) {
    bridge_->NotifyItemStarted(user_data_importer::FAVORITES);
    ImportBookmarks();
    bridge_->NotifyItemEnded(user_data_importer::FAVORITES);
  }

  if ((items & user_data_importer::AUTOFILL_FORM_DATA) && !cancelled_) {
    bridge_->NotifyItemStarted(user_data_importer::AUTOFILL_FORM_DATA);
    ImportAutofillFormData();
    bridge_->NotifyItemEnded(user_data_importer::AUTOFILL_FORM_DATA);
  }

  bridge_->NotifyEnded();
}

void ChromeImporter::Cancel() {
  cancelled_ = true;
}

base::FilePath ChromeImporter::GetCopiedSQLiteDb(const std::string& db_name) {
  // Support both direct profile directories and User Data roots.
  base::FilePath db_path = source_path_.AppendASCII(db_name);
  if (!base::PathExists(db_path)) {
    db_path = source_path_.AppendASCII("Default").AppendASCII(db_name);
  }

  if (!base::PathExists(db_path)) {
    return base::FilePath();
  }

  base::FilePath temp_db_path = temp_dir_.GetPath().AppendASCII(db_name);
  if (!base::CopyFile(db_path, temp_db_path)) {
    return base::FilePath();
  }

  // Copy WAL and SHM files to replay uncommitted transactions safely
  base::FilePath wal_source = db_path.AddExtension(FILE_PATH_LITERAL("-wal"));
  if (base::PathExists(wal_source)) {
    base::CopyFile(wal_source, temp_db_path.AddExtension(FILE_PATH_LITERAL("-wal")));
  }

  base::FilePath shm_source = db_path.AddExtension(FILE_PATH_LITERAL("-shm"));
  if (base::PathExists(shm_source)) {
    base::CopyFile(shm_source, temp_db_path.AddExtension(FILE_PATH_LITERAL("-shm")));
  }

  return temp_db_path;
}

void ChromeImporter::ImportHistory() {
  base::FilePath temp_history_path = GetCopiedSQLiteDb("History");
  if (temp_history_path.empty()) {
    return;
  }

  sql::Database db(sql::Database::Tag{"History"});
  if (!db.Open(temp_history_path)) {
    return;
  }

  // CRITICAL: We MUST JOIN the tables and ORDER BY v.visit_time ASC.
  // This guarantees that when the browser process rebuilds the graph,
  // a parent visit is fully ingested and mapped before its child arrives.
  const char kHistoryQuery[] =
      "SELECT u.url, u.title, u.visit_count, u.typed_count, u.hidden, "
      "v.id, v.from_visit, v.visit_time, v.visit_duration, v.transition, v.external_referrer_url "
      "FROM visits v "
      "JOIN urls u ON v.url = u.id "
      "WHERE u.hidden = 0 "
      "ORDER BY v.visit_time ASC";

  sql::Statement s(db.GetUniqueStatement(kHistoryQuery));

  std::vector<chrome::mojom::ImporterVisitRowPtr> visit_batch;
  constexpr size_t kMaxBatchSize = 1500; 

  while (s.Step() && !cancelled_) {
    GURL url(s.ColumnString(0));
    if (!url.is_valid() || url.SchemeIsFile()) continue;

    // Mojo creates a smart pointer for us
    auto row = chrome::mojom::ImporterVisitRow::New();
    
    row->url = url;
    row->title = s.ColumnString16(1);
    row->visit_count = s.ColumnInt(2);
    row->typed_count = s.ColumnInt(3);
    row->hidden = (s.ColumnInt(4) != 0);
    row->source_visit_id = s.ColumnInt64(5);
    row->source_from_visit_id = s.ColumnInt64(6);
    row->visit_time = base::Time::FromInternalValue(s.ColumnInt64(7));
    row->visit_duration = base::Microseconds(s.ColumnInt64(8));
    row->transition = s.ColumnInt(9);
    row->external_referrer_url = s.ColumnString(10);

    visit_batch.push_back(std::move(row));

    if (visit_batch.size() >= kMaxBatchSize) {
      bridge_->SetHistoryVisits(std::move(visit_batch));
      visit_batch = std::vector<chrome::mojom::ImporterVisitRowPtr>();
    }
  }

  if (!visit_batch.empty() && !cancelled_) {
    bridge_->SetHistoryVisits(std::move(visit_batch));
  }
}

void ChromeImporter::ImportBookmarks() {
  base::FilePath bookmarks_path = source_path_.AppendASCII("Bookmarks");
  if (!base::PathExists(bookmarks_path)) {
    bookmarks_path = source_path_.AppendASCII("Default").AppendASCII("Bookmarks");
  }

  if (!base::PathExists(bookmarks_path)) {
    return;
  }

  std::string content;
  if (!base::ReadFileToString(bookmarks_path, &content)) {
    return;
  }

  std::optional<base::Value> json = 
      base::JSONReader::Read(content, base::JSON_PARSE_RFC);
  
  if (!json || !json->is_dict()) {
    return;
  }

  const base::DictValue* dict = json->GetIfDict();
  if (!dict) {
    return;
  }

  const base::DictValue* roots = dict->FindDict("roots");
  if (!roots) {
    return;
  }

  std::vector<user_data_importer::ImportedBookmarkEntry> bookmarks;
  std::vector<std::u16string> path;

  const char* const kRootKeys[] = {"bookmark_bar", "other", "synced"};
  for (const char* key : kRootKeys) {
    const base::DictValue* root_node = roots->FindDict(key);
    if (root_node) {
      const base::ListValue* children = root_node->FindList("children");
      if (children) {
        bool in_toolbar = (std::string(key) == "bookmark_bar");
        for (const auto& child : *children) {
          if (const base::DictValue* child_dict = child.GetIfDict()) {
            ParseBookmarksTree(child_dict, path, bookmarks, in_toolbar);
          }
        }
      }
    }
  }

  if (!bookmarks.empty() && !cancelled_) {
    bridge_->AddBookmarks(bookmarks, std::u16string());
  }
}

void ChromeImporter::ImportAutofillFormData() {
  base::FilePath temp_web_data_path = GetCopiedSQLiteDb("Web Data");
  if (temp_web_data_path.empty()) {
    return;
  }

  sql::Database db(sql::Database::Tag{"Web"});
  if (!db.Open(temp_web_data_path)) {
    return;
  }

  const char kAutofillQuery[] = 
      "SELECT name, value, date_created, date_last_used, count FROM autofill";
  sql::Statement s(db.GetUniqueStatement(kAutofillQuery));
  if (!s.is_valid()) {
    return;
  }

  std::vector<ImporterAutofillFormDataEntry> form_entries;
  while (s.Step() && !cancelled_) {
    ImporterAutofillFormDataEntry entry;
    entry.name = s.ColumnString16(0);
    entry.value = s.ColumnString16(1);
    
    // Assign directly to the correct M147 struct properties (NO timestamps array)
    entry.first_used = base::Time::FromTimeT(s.ColumnInt64(2));
    entry.last_used = base::Time::FromTimeT(s.ColumnInt64(3));
    entry.times_used = s.ColumnInt(4);

    form_entries.push_back(std::move(entry));
  }

  if (!form_entries.empty() && !cancelled_) {
    bridge_->SetAutofillFormData(form_entries);
  }
}
