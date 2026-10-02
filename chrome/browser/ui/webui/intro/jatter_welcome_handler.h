#ifndef CHROME_BROWSER_UI_WEBUI_INTRO_JATTER_WELCOME_HANDLER_H_
#define CHROME_BROWSER_UI_WEBUI_INTRO_JATTER_WELCOME_HANDLER_H_

#include "base/functional/callback.h"
#include "content/public/browser/web_ui_message_handler.h"

class JatterWelcomeHandler : public content::WebUIMessageHandler {
 public:
  using ContinueCallback = base::OnceCallback<void(bool set_as_default)>;

  explicit JatterWelcomeHandler(ContinueCallback continue_callback);
  ~JatterWelcomeHandler() override;

  // content::WebUIMessageHandler:
  void RegisterMessages() override;

 private:
  void HandleContinue(const base::ListValue& args);

  ContinueCallback continue_callback_;
};

#endif  // CHROME_BROWSER_UI_WEBUI_INTRO_JATTER_WELCOME_HANDLER_H_