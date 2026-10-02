#include "chrome/browser/ui/webui/intro/jatter_learn_handler.h"
#include "base/values.h"

JatterLearnHandler::JatterLearnHandler(ContinueCallback continue_callback)
    : continue_callback_(std::move(continue_callback)) {}

JatterLearnHandler::~JatterLearnHandler() = default;

void JatterLearnHandler::RegisterMessages() {
  web_ui()->RegisterMessageCallback(
      "continueFromLearn",
      base::BindRepeating(&JatterLearnHandler::HandleContinue,
                          base::Unretained(this)));
}

void JatterLearnHandler::HandleContinue(const base::ListValue& args) {
  bool always_allow = !args.empty() && args[0].is_bool() && args[0].GetBool();
  if (continue_callback_) {
    std::move(continue_callback_).Run(always_allow);
  }
}