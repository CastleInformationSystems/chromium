#ifndef CHROME_BROWSER_UI_WEBUI_INTRO_JATTER_PLUS_HANDLER_H_
#define CHROME_BROWSER_UI_WEBUI_INTRO_JATTER_PLUS_HANDLER_H_

#include "base/functional/callback.h"
#include "content/public/browser/web_ui_message_handler.h"

class JatterPlusHandler : public content::WebUIMessageHandler {
 public:
  using PlusChoiceCallback = base::OnceCallback<void(bool get_plus_clicked)>;

  explicit JatterPlusHandler(PlusChoiceCallback choice_callback);
  ~JatterPlusHandler() override;

  void RegisterMessages() override;

 private:
  void HandleChoice(const base::ListValue& args);
  PlusChoiceCallback choice_callback_;
};

#endif