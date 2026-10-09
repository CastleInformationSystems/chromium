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
  // Support both direct profile directories and User Data roots.
  base::FilePath history_path = source_path_.AppendASCII("History");
  if (!base::PathExists(history_path)) {
    history_path = source_path_.AppendASCII("Default").AppendASCII("History");
  }

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

  // Copy WAL and SHM files to replay uncommitted transactions safely
  base::FilePath wal_source = history_path.AddExtension(FILE_PATH_LITERAL("-wal"));
  if (base::PathExists(wal_source)) {
    base::CopyFile(wal_source, temp_dir.GetPath().AppendASCII("History-wal"));
  }

  base::FilePath shm_source = history_path.AddExtension(FILE_PATH_LITERAL("-shm"));
  if (base::PathExists(shm_source)) {
    base::CopyFile(shm_source, temp_dir.GetPath().AppendASCII("History-shm"));
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
      // Move the batch over the bridge and prep a fresh vector
      bridge_->SetHistoryVisits(std::move(visit_batch));
      visit_batch = std::vector<chrome::mojom::ImporterVisitRowPtr>();
    }
  }

  if (!visit_batch.empty() && !cancelled_) {
    bridge_->SetHistoryVisits(std::move(visit_batch));
  }
}
