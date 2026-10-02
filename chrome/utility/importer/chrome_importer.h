#ifndef CHROME_UTILITY_IMPORTER_CHROME_IMPORTER_H_
#define CHROME_UTILITY_IMPORTER_CHROME_IMPORTER_H_

#include "base/files/file_path.h"
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

  base::FilePath source_path_;
  bool cancelled_ = false;
};

#endif  // CHROME_UTILITY_IMPORTER_CHROME_IMPORTER_H_