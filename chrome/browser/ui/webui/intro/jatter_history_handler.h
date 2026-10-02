#ifndef CHROME_BROWSER_UI_WEBUI_INTRO_JATTER_HISTORY_HANDLER_H_
#define CHROME_BROWSER_UI_WEBUI_INTRO_JATTER_HISTORY_HANDLER_H_

#include "base/functional/callback.h"
#include "base/memory/scoped_refptr.h"
#include "base/memory/weak_ptr.h"
#include "chrome/browser/importer/importer_list.h"
#include "content/public/browser/web_ui_message_handler.h"
#include <string>

class JatterHistoryHandler : public content::WebUIMessageHandler {
 public:
  using HistoryChoiceCallback = base::OnceCallback<void(bool should_import, const user_data_importer::SourceProfile& profile)>;

  explicit JatterHistoryHandler(HistoryChoiceCallback choice_callback);
  ~JatterHistoryHandler() override;

  void RegisterMessages() override;

 private:
  void HandleChoice(const base::ListValue& args);
  void HandleInitialize(const base::ListValue& args);
  void OnBrowserProfilesLoaded();

  HistoryChoiceCallback choice_callback_;
  std::unique_ptr<ImporterList> importer_list_;

  base::WeakPtrFactory<JatterHistoryHandler> weak_ptr_factory_{this};
};

#endif