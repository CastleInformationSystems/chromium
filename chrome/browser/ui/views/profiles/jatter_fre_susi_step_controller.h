#ifndef CHROME_BROWSER_UI_VIEWS_PROFILES_JATTER_FRE_SUSI_STEP_CONTROLLER_H_
#define CHROME_BROWSER_UI_VIEWS_PROFILES_JATTER_FRE_SUSI_STEP_CONTROLLER_H_

#include "base/functional/callback.h"
#include "base/memory/weak_ptr.h"
#include "chrome/browser/ui/views/profiles/profile_management_step_controller.h"
#include "content/public/browser/web_contents_observer.h"

class ProfilePickerWebContentsHost;

class JatterFreSusiStepController : public ProfileManagementStepController,
                                    public content::WebContentsObserver {
 public:
  JatterFreSusiStepController(ProfilePickerWebContentsHost* host,
                              base::OnceClosure step_completed_callback);
  ~JatterFreSusiStepController() override;

  // ProfileManagementStepController:
  void Show(StepSwitchFinishedCallback step_shown_callback,
            bool reset_state) override;
  void OnNavigateBackRequested() override;

  // content::WebContentsObserver:
  void DidStartNavigation(content::NavigationHandle* navigation_handle) override;

 private:
  void OnSusiPromptLoaded(StepSwitchFinishedCallback step_shown_callback);
  void OnSusiChoice(bool sign_in_clicked);
  void ReloadLocalSusiPrompt();

  base::OnceClosure step_completed_callback_;
  base::WeakPtrFactory<JatterFreSusiStepController> weak_ptr_factory_{this};
};

#endif  // CHROME_BROWSER_UI_VIEWS_PROFILES_JATTER_FRE_SUSI_STEP_CONTROLLER_H_