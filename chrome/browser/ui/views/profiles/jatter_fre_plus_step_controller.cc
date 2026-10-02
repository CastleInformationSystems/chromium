#include "chrome/browser/ui/views/profiles/jatter_fre_plus_step_controller.h"
#include "chrome/browser/ui/views/profiles/profile_picker_web_contents_host.h"
#include "chrome/browser/ui/webui/intro/intro_ui.h"
#include "chrome/browser/ui/webui/intro/jatter_plus_handler.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/common/pref_names.h"
#include "components/prefs/pref_service.h"
#include "content/public/browser/web_contents.h"
#include "content/public/browser/web_ui.h"
#include "url/gurl.h"
#include "base/logging.h"

JatterFrePlusStepController::JatterFrePlusStepController(
    ProfilePickerWebContentsHost* host,
    Profile* profile,
    base::OnceClosure step_completed_callback)
    : ProfileManagementStepController(host),
      profile_(profile),
      step_completed_callback_(std::move(step_completed_callback)) {}

JatterFrePlusStepController::~JatterFrePlusStepController() = default;

void JatterFrePlusStepController::Show(StepSwitchFinishedCallback step_shown_callback, bool reset_state) {
  host()->ShowScreenInPickerContents(
      GURL("chrome://intro/plus"),
      base::BindOnce(&JatterFrePlusStepController::OnPlusPageLoaded,
                     weak_ptr_factory_.GetWeakPtr(),
                     std::move(step_shown_callback)));
}

void JatterFrePlusStepController::OnPlusPageLoaded(StepSwitchFinishedCallback step_shown_callback) {
  std::move(step_shown_callback.value()).Run(true);
  content::WebUI* web_ui = host()->GetPickerContents()->GetWebUI();
  if (web_ui) {
    web_ui->AddMessageHandler(std::make_unique<JatterPlusHandler>(
        base::BindOnce(&JatterFrePlusStepController::OnPlusChoice,
                       weak_ptr_factory_.GetWeakPtr())));
  }
}

void JatterFrePlusStepController::OnPlusChoice(bool get_plus_clicked) {
  if (get_plus_clicked) {
    // Flag the profile so the browser injects the Plus URL when it builds the tab strip
    profile_->GetPrefs()->SetBoolean(prefs::kJatterShowPlusUpgradeOnStartup, true);
  }

  if (step_completed_callback_) {
    // This closes the FRE window and triggers the main browser window to open
    std::move(step_completed_callback_).Run();
  }
}

void JatterFrePlusStepController::OnNavigateBackRequested() {
  NavigateBackInternal(host()->GetPickerContents());
}