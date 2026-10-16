#ifndef CHROME_UTILITY_IMPORTER_CHROME_IMPORTER_H_
#define CHROME_UTILITY_IMPORTER_CHROME_IMPORTER_H_

#include <string>

#include "base/files/file_path.h"
#include "base/files/scoped_temp_dir.h"
#include "chrome/utility/importer/importer.h"

class ChromeImporter : public Importer {
 public:
  ChromeImporter();

  ChromeImporter(const ChromeImporter&) = delete;
  ChromeImporter& operator=(const ChromeImporter&) = delete;

  // importer::Importer:
  void StartImport(const user_data_importer::SourceProfile& source_profile,
                   uint16_t items,
                   ImporterBridge* bridge) override;
  void Cancel() override;

 private:
  ~ChromeImporter() override;

  void ImportHistory();
  void ImportBookmarks();
  void ImportAutofillFormData();

  // Copies an SQLite database (and its WAL/SHM logs) to temp_dir_ to bypass live locks.
  base::FilePath GetCopiedSQLiteDb(const std::string& db_name);

  base::FilePath source_path_;
  base::ScopedTempDir temp_dir_;
  bool cancelled_ = false;
};

#endif  // CHROME_UTILITY_IMPORTER_CHROME_IMPORTER_H_