#ifndef CHROME_BROWSER_UI_VIEWS_PROFILES_JATTER_FRE_WELCOME_STEP_CONTROLLER_H_
#define CHROME_BROWSER_UI_VIEWS_PROFILES_JATTER_FRE_WELCOME_STEP_CONTROLLER_H_

#include "base/functional/callback.h"
#include "base/memory/weak_ptr.h"
#include "chrome/browser/ui/views/profiles/profile_management_step_controller.h"
#include "url/gurl.h"

class ProfilePickerWebContentsHost;

class JatterFreWelcomeStepController : public ProfileManagementStepController {
 public:
  JatterFreWelcomeStepController(ProfilePickerWebContentsHost* host,
                                base::OnceClosure step_completed_callback);
  ~JatterFreWelcomeStepController() override;

  // ProfileManagementStepController:
  void Show(StepSwitchFinishedCallback step_shown_callback,
            bool reset_state) override;
  void OnNavigateBackRequested() override;

 private:
  void OnContinue(bool set_as_default);
  void OnWelcomePageLoaded(StepSwitchFinishedCallback step_shown_callback);

  base::OnceClosure step_completed_callback_;
  base::WeakPtrFactory<JatterFreWelcomeStepController> weak_ptr_factory_{this};
};

#endif  // CHROME_BROWSER_UI_VIEWS_PROFILES_JATTER_FRE_WELCOME_STEP_CONTROLLER_H_