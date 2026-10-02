#ifndef CHROME_BROWSER_UI_WEBUI_INTRO_JATTER_LEARN_HANDLER_H_
#define CHROME_BROWSER_UI_WEBUI_INTRO_JATTER_LEARN_HANDLER_H_

#include "base/functional/callback.h"
#include "content/public/browser/web_ui_message_handler.h"

class JatterLearnHandler : public content::WebUIMessageHandler {
 public:
  using ContinueCallback = base::OnceCallback<void(bool always_allow)>;

  explicit JatterLearnHandler(ContinueCallback continue_callback);
  ~JatterLearnHandler() override;

  void RegisterMessages() override;

 private:
  void HandleContinue(const base::ListValue& args);
  ContinueCallback continue_callback_;
};

#endif