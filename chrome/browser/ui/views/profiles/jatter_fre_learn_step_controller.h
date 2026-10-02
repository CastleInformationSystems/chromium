#ifndef CHROME_BROWSER_UI_VIEWS_PROFILES_JATTER_FRE_LEARN_STEP_CONTROLLER_H_
#define CHROME_BROWSER_UI_VIEWS_PROFILES_JATTER_FRE_LEARN_STEP_CONTROLLER_H_

#include "base/functional/callback.h"
#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "chrome/browser/ui/views/profiles/profile_management_step_controller.h"

class ProfilePickerWebContentsHost;
class Profile;

class JatterFreLearnStepController : public ProfileManagementStepController {
 public:
  JatterFreLearnStepController(ProfilePickerWebContentsHost* host,
                               Profile* profile,
                               base::OnceClosure step_completed_callback);
  ~JatterFreLearnStepController() override;

  void Show(StepSwitchFinishedCallback step_shown_callback,
            bool reset_state) override;
  void OnNavigateBackRequested() override;

 private:
  void OnLearnPageLoaded(StepSwitchFinishedCallback step_shown_callback);
  void OnContinue(bool always_allow);

  raw_ptr<Profile> profile_;
  base::OnceClosure step_completed_callback_;
  base::WeakPtrFactory<JatterFreLearnStepController> weak_ptr_factory_{this};
};

#endif