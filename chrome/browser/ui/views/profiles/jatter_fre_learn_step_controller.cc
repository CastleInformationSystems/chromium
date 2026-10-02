#include "chrome/browser/ui/views/profiles/jatter_fre_learn_step_controller.h"
#include "chrome/browser/ui/views/profiles/profile_picker_web_contents_host.h"
#include "chrome/browser/ui/webui/intro/jatter_learn_handler.h"
#include "chrome/common/pref_names.h"
#include "components/prefs/pref_service.h"
#include "content/public/browser/web_contents.h"
#include "content/public/browser/web_ui.h"
#include "url/gurl.h"

JatterFreLearnStepController::JatterFreLearnStepController(
    ProfilePickerWebContentsHost* host,
    Profile* profile,
    base::OnceClosure step_completed_callback)
    : ProfileManagementStepController(host),
      profile_(profile),
      step_completed_callback_(std::move(step_completed_callback)) {}

JatterFreLearnStepController::~JatterFreLearnStepController() = default;

void JatterFreLearnStepController::Show(
    StepSwitchFinishedCallback step_shown_callback,
    bool reset_state) {
  host()->ShowScreenInPickerContents(
      GURL("chrome://intro/learn"),
      base::BindOnce(&JatterFreLearnStepController::OnLearnPageLoaded,
                     weak_ptr_factory_.GetWeakPtr(),
                     std::move(step_shown_callback)));
}

void JatterFreLearnStepController::OnLearnPageLoaded(
    StepSwitchFinishedCallback step_shown_callback) {
  std::move(step_shown_callback.value()).Run(true);

  content::WebUI* web_ui = host()->GetPickerContents()->GetWebUI();
  if (web_ui) {
    web_ui->AddMessageHandler(std::make_unique<JatterLearnHandler>(
        base::BindOnce(&JatterFreLearnStepController::OnContinue,
                       weak_ptr_factory_.GetWeakPtr())));
  }
}

void JatterFreLearnStepController::OnContinue(bool always_allow) {
  profile_->GetPrefs()->SetBoolean(prefs::kJatterAlwaysAllowPersonalization, 
                                   always_allow);

  if (step_completed_callback_) {
    std::move(step_completed_callback_).Run();
  }
}

void JatterFreLearnStepController::OnNavigateBackRequested() {
  NavigateBackInternal(host()->GetPickerContents());
}