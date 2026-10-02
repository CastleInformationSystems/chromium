#include "chrome/browser/ui/views/profiles/jatter_fre_welcome_step_controller.h"

#include "base/functional/bind.h"
#include "chrome/browser/shell_integration.h"
#include "chrome/browser/ui/views/profiles/profile_picker_web_contents_host.h"
#include "chrome/browser/ui/webui/intro/intro_ui.h"
#include "chrome/browser/ui/webui/intro/jatter_welcome_handler.h"
#include "chrome/common/webui_url_constants.h"
#include "content/public/browser/web_contents.h"
#include "content/public/browser/web_ui.h"

JatterFreWelcomeStepController::JatterFreWelcomeStepController(
    ProfilePickerWebContentsHost* host,
    base::OnceClosure step_completed_callback)
    : ProfileManagementStepController(host),
      step_completed_callback_(std::move(step_completed_callback)) {}

JatterFreWelcomeStepController::~JatterFreWelcomeStepController() = default;

void JatterFreWelcomeStepController::Show(
    StepSwitchFinishedCallback step_shown_callback,
    bool reset_state) {
  // Can load chrome://intro or custom data source URL
  host()->ShowScreenInPickerContents(
      GURL(chrome::kChromeUIIntroURL),
      base::BindOnce(&JatterFreWelcomeStepController::OnWelcomePageLoaded,
                     weak_ptr_factory_.GetWeakPtr(),
                     std::move(step_shown_callback)));
}

void JatterFreWelcomeStepController::OnWelcomePageLoaded(
    StepSwitchFinishedCallback step_shown_callback) {
  std::move(step_shown_callback.value()).Run(/*success=*/true);

  content::WebUI* web_ui = host()->GetPickerContents()->GetWebUI();
  if (web_ui) {
    web_ui->AddMessageHandler(std::make_unique<JatterWelcomeHandler>(
        base::BindOnce(&JatterFreWelcomeStepController::OnContinue,
                       weak_ptr_factory_.GetWeakPtr())));
  }
}

void JatterFreWelcomeStepController::OnNavigateBackRequested() {
  // Step 1 is root; back navigation is a no-op
}

void JatterFreWelcomeStepController::OnContinue(bool set_as_default) {
  if (set_as_default && shell_integration::CanSetAsDefaultBrowser()) {
    // Fire-and-forget: SetAsDefault runs asynchronously on worker threads
    base::MakeRefCounted<shell_integration::DefaultBrowserWorker>()
        ->StartSetAsDefault(base::DoNothing());
  }

  if (step_completed_callback_) {
    std::move(step_completed_callback_).Run();
  }
}