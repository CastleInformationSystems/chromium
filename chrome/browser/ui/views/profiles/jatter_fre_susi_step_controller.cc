#include "chrome/browser/ui/views/profiles/jatter_fre_susi_step_controller.h"

#include "base/task/sequenced_task_runner.h"
#include "chrome/browser/jatter/jatter_environment.h"
#include "chrome/browser/ui/views/profiles/profile_picker_web_contents_host.h"
#include "chrome/browser/ui/webui/intro/jatter_susi_handler.h"
#include "content/public/browser/navigation_controller.h"
#include "content/public/browser/navigation_handle.h"
#include "content/public/browser/web_contents.h"
#include "content/public/browser/web_ui.h"
#include "url/gurl.h"

JatterFreSusiStepController::JatterFreSusiStepController(
    ProfilePickerWebContentsHost* host,
    base::OnceClosure step_completed_callback)
    : ProfileManagementStepController(host),
      step_completed_callback_(std::move(step_completed_callback)) {}

JatterFreSusiStepController::~JatterFreSusiStepController() = default;

void JatterFreSusiStepController::Show(
    StepSwitchFinishedCallback step_shown_callback,
    bool reset_state) {
  // Start observing the WebContents to catch the live URL redirect later
  Observe(host()->GetPickerContents());

  // Load the local WebUI prompt first
  host()->ShowScreenInPickerContents(
      GURL("chrome://intro/susi"),
      base::BindOnce(&JatterFreSusiStepController::OnSusiPromptLoaded,
                     weak_ptr_factory_.GetWeakPtr(),
                     std::move(step_shown_callback)));
}

void JatterFreSusiStepController::OnSusiPromptLoaded(
    StepSwitchFinishedCallback step_shown_callback) {
  std::move(step_shown_callback.value()).Run(true);

  content::WebUI* web_ui = host()->GetPickerContents()->GetWebUI();
  if (web_ui) {
    web_ui->AddMessageHandler(std::make_unique<JatterSusiHandler>(
        base::BindOnce(&JatterFreSusiStepController::OnSusiChoice,
                       weak_ptr_factory_.GetWeakPtr())));
  }
}

void JatterFreSusiStepController::OnSusiChoice(bool sign_in_clicked) {
  if (!sign_in_clicked) {
    if (step_completed_callback_) {
      std::move(step_completed_callback_).Run();
    }
    return;
  }

  // Construct the base URL and safely append the path and query
  GURL base_url(jatter::kAppUrl);
  GURL sign_in_url = base_url.Resolve("/sign_in?fre=true");

  content::NavigationController::LoadURLParams params(sign_in_url);
  host()->GetPickerContents()->GetController().LoadURLWithParams(params);
}

void JatterFreSusiStepController::DidStartNavigation(
    content::NavigationHandle* navigation_handle) {
  if (!navigation_handle->IsInPrimaryMainFrame()) return;

  GURL target_url = navigation_handle->GetURL();
  GURL base_url(jatter::kAppUrl);

  if (target_url.host() == base_url.host()) {
    
    // Success: Advance to Step 3
    if (target_url.path() == "/sign_in/complete") {
      host()->GetPickerContents()->Stop();
      if (step_completed_callback_) {
        // Safely escape the observer stack before advancing
        base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
            FROM_HERE, std::move(step_completed_callback_));
      }
    } 
    // Cancel: Go back to the native Step 2 prompt
    else if (target_url.path() == "/sign_in/cancel") {
      host()->GetPickerContents()->Stop();
      
      // Safely escape the observer stack before reloading the WebUI
      base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
          FROM_HERE,
          base::BindOnce(&JatterFreSusiStepController::ReloadLocalSusiPrompt,
                         weak_ptr_factory_.GetWeakPtr()));
    }
  }
}

void JatterFreSusiStepController::ReloadLocalSusiPrompt() {
  host()->ShowScreenInPickerContents(
      GURL("chrome://intro/susi"),
      base::BindOnce(&JatterFreSusiStepController::OnSusiPromptLoaded,
                     weak_ptr_factory_.GetWeakPtr(),
                     // Pass a dummy callback so OnSusiPromptLoaded still runs
                     // and properly reattaches the WebUI message handler.
                     StepSwitchFinishedCallback(base::BindOnce([](bool) {}))));
}

void JatterFreSusiStepController::OnNavigateBackRequested() {
  // Return to Step 1
  NavigateBackInternal(host()->GetPickerContents());
}