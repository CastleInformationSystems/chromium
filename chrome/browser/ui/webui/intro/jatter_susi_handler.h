#ifndef CHROME_BROWSER_UI_WEBUI_INTRO_JATTER_SUSI_HANDLER_H_
#define CHROME_BROWSER_UI_WEBUI_INTRO_JATTER_SUSI_HANDLER_H_

#include "base/functional/callback.h"
#include "content/public/browser/web_ui_message_handler.h"

class JatterSusiHandler : public content::WebUIMessageHandler {
 public:
  using SusiChoiceCallback = base::OnceCallback<void(bool sign_in_clicked)>;

  explicit JatterSusiHandler(SusiChoiceCallback choice_callback);
  ~JatterSusiHandler() override;

  // content::WebUIMessageHandler:
  void RegisterMessages() override;

 private:
  void HandleChoice(const base::ListValue& args);

  SusiChoiceCallback choice_callback_;
};

#endif  // CHROME_BROWSER_UI_WEBUI_INTRO_JATTER_SUSI_HANDLER_H_