#include "chrome/browser/ui/webui/intro/jatter_plus_handler.h"
#include "base/values.h"

JatterPlusHandler::JatterPlusHandler(PlusChoiceCallback choice_callback)
    : choice_callback_(std::move(choice_callback)) {}

JatterPlusHandler::~JatterPlusHandler() = default;

void JatterPlusHandler::RegisterMessages() {
  web_ui()->RegisterMessageCallback(
      "handleJatterPlusChoice",
      base::BindRepeating(&JatterPlusHandler::HandleChoice,
                          base::Unretained(this)));
}

void JatterPlusHandler::HandleChoice(const base::ListValue& args) {
  if (args.empty() || !args[0].is_bool()) return;
  if (choice_callback_) {
    std::move(choice_callback_).Run(args[0].GetBool());
  }
}