#include "chrome/browser/ui/webui/intro/jatter_history_handler.h"

#include "base/values.h"
#include "chrome/browser/browser_process.h"
#include "components/user_data_importer/common/importer_data_types.h"

JatterHistoryHandler::JatterHistoryHandler(HistoryChoiceCallback choice_callback)
    : choice_callback_(std::move(choice_callback)) {}

JatterHistoryHandler::~JatterHistoryHandler() = default;

void JatterHistoryHandler::RegisterMessages() {
  web_ui()->RegisterMessageCallback(
      "initializeHistoryImport",
      base::BindRepeating(&JatterHistoryHandler::HandleInitialize,
                          base::Unretained(this)));

  web_ui()->RegisterMessageCallback(
      "handleHistoryImport",
      base::BindRepeating(&JatterHistoryHandler::HandleChoice,
                          base::Unretained(this)));
}

void JatterHistoryHandler::HandleChoice(const base::ListValue& args) {
  if (args.size() < 2 || !args[0].is_bool() || !args[1].is_int()) return;
  
  bool should_import = args[0].GetBool();
  int browser_index = args[1].GetInt();
  
  user_data_importer::SourceProfile selected_profile;
  
  // Safely grab the profile from the handler's list
  if (should_import && importer_list_ && browser_index >= 0 && 
      static_cast<size_t>(browser_index) < importer_list_->count()) {
    selected_profile = importer_list_->GetSourceProfileAt(browser_index);
  } else {
    should_import = false; // Fallback to skip if index is invalid
  }
  
  if (choice_callback_) {
    std::move(choice_callback_).Run(should_import, selected_profile);
  }
}

void JatterHistoryHandler::HandleInitialize(const base::ListValue& args) {
  AllowJavascript();
  
  // Create the ImporterList and start detecting installed browsers
  importer_list_ = std::make_unique<ImporterList>();
  importer_list_->DetectSourceProfiles(
      g_browser_process->GetApplicationLocale(),
      false, // Do not include interactive/running profiles
      base::BindOnce(&JatterHistoryHandler::OnBrowserProfilesLoaded,
                     weak_ptr_factory_.GetWeakPtr()));
}

void JatterHistoryHandler::OnBrowserProfilesLoaded() {
  base::ListValue browser_list;
  
  for (size_t i = 0; i < importer_list_->count(); ++i) {
    // FIX: Change namespace from importer:: to user_data_importer::
    const user_data_importer::SourceProfile& profile = importer_list_->GetSourceProfileAt(i);
    
    base::DictValue browser_info;
    browser_info.Set("id", static_cast<int>(i));
    browser_info.Set("name", profile.importer_name);
    browser_list.Append(std::move(browser_info));
  }
  
  // Fire an event to the frontend with the populated list
  FireWebUIListener("available-browsers-loaded", browser_list);
}
