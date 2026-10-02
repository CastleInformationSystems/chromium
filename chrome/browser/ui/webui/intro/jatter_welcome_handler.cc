#include "chrome/browser/ui/webui/intro/jatter_welcome_handler.h"
#include "base/values.h"

JatterWelcomeHandler::JatterWelcomeHandler(ContinueCallback continue_callback)
    : continue_callback_(std::move(continue_callback)) {}

JatterWelcomeHandler::~JatterWelcomeHandler() = default;

void JatterWelcomeHandler::RegisterMessages() {
  web_ui()->RegisterMessageCallback(
      "continueFromWelcome",
      base::BindRepeating(&JatterWelcomeHandler::HandleContinue,
                          base::Unretained(this)));
}

void JatterWelcomeHandler::HandleContinue(const base::ListValue& args) {
  // args[0] is a boolean: make Jatter default browser
  bool set_as_default = !args.empty() && args[0].is_bool() && args[0].GetBool();

  if (continue_callback_) {
    std::move(continue_callback_).Run(set_as_default);
  }
}