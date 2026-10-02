#include "chrome/browser/ui/webui/intro/jatter_susi_handler.h"
#include "base/values.h"

JatterSusiHandler::JatterSusiHandler(SusiChoiceCallback choice_callback)
    : choice_callback_(std::move(choice_callback)) {}

JatterSusiHandler::~JatterSusiHandler() = default;

void JatterSusiHandler::RegisterMessages() {
  web_ui()->RegisterMessageCallback(
      "handleSusiChoice",
      base::BindRepeating(&JatterSusiHandler::HandleChoice,
                          base::Unretained(this)));
}

void JatterSusiHandler::HandleChoice(const base::ListValue& args) {
  if (args.empty() || !args[0].is_bool()) return;
  
  bool sign_in_clicked = args[0].GetBool();
  if (choice_callback_) {
    std::move(choice_callback_).Run(sign_in_clicked);
  }
}