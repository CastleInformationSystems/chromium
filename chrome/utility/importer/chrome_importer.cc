#include "chrome/utility/importer/chrome_importer.h"

#include "base/files/file_util.h"
#include "base/files/scoped_temp_dir.h"
#include "base/strings/utf_string_conversions.h"
#include "chrome/common/importer/importer_bridge.h"
#include "components/history/core/browser/history_types.h"
#include "sql/database.h"
#include "sql/statement.h"
#include "url/gurl.h"

ChromeImporter::ChromeImporter() = default;
ChromeImporter::~ChromeImporter() = default;

void ChromeImporter::StartImport(const user_data_importer::SourceProfile& source_profile,
                                 uint16_t items,
                                 ImporterBridge* bridge) {
  bridge_ = bridge;
  source_path_ = source_profile.source_path;
  cancelled_ = false;

  bridge_->NotifyStarted();

  if ((items & user_data_importer::HISTORY) && !cancelled_) {
    ImportHistory();
  }

  bridge_->NotifyEnded();
}

void ChromeImporter::Cancel() {
  cancelled_ = true;
}

void ChromeImporter::ImportHistory() {
  base::FilePath history_path = source_path_.AppendASCII("Default")
                                            .AppendASCII("History");

  if (!base::PathExists(history_path)) {
    return;
  }

  base::ScopedTempDir temp_dir;
  if (!temp_dir.CreateUniqueTempDir()) {
    return;
  }

  base::FilePath temp_history_path = temp_dir.GetPath().AppendASCII("History");
  if (!base::CopyFile(history_path, temp_history_path)) {
    return;
  }

  // FIX 1: Use a whitelisted tag string that the compiler recognizes
  sql::Database db(sql::Database::Tag{"History"});
  
  if (!db.Open(temp_history_path)) {
    return;
  }

  const char kHistoryQuery[] =
      "SELECT url, title, visit_count, typed_count, last_visit_time, hidden "
      "FROM urls WHERE hidden = 0";

  sql::Statement s(db.GetUniqueStatement(kHistoryQuery));

  // FIX 2: Use the IPC-safe ImporterURLRow struct from your custom namespace
  std::vector<user_data_importer::ImporterURLRow> rows;
  
  while (s.Step() && !cancelled_) {
    GURL url(s.ColumnString(0));
    
    if (!url.is_valid() || url.SchemeIsFile()) {
      continue;
    }

    user_data_importer::ImporterURLRow row;
    row.url = url;
    row.title = s.ColumnString16(1);
    row.visit_count = s.ColumnInt(2);
    row.typed_count = s.ColumnInt(3);
    row.last_visit = base::Time::FromInternalValue(s.ColumnInt64(4));
    row.hidden = (s.ColumnInt(5) != 0);

    rows.push_back(row);
  }

  if (!rows.empty() && !cancelled_) {
    // FIX 3: Use the user_data_importer namespace for the VisitSource enum
    bridge_->SetHistoryItems(rows, user_data_importer::VISIT_SOURCE_FIREFOX_IMPORTED);
  }
}
