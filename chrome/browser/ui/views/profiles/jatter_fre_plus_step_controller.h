#ifndef CHROME_BROWSER_UI_VIEWS_PROFILES_JATTER_FRE_PLUS_STEP_CONTROLLER_H_
#define CHROME_BROWSER_UI_VIEWS_PROFILES_JATTER_FRE_PLUS_STEP_CONTROLLER_H_

#include "base/functional/callback.h"
#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "chrome/browser/ui/views/profiles/profile_management_step_controller.h"

class ProfilePickerWebContentsHost;
class Profile;

class JatterFrePlusStepController : public ProfileManagementStepController {
 public:
  JatterFrePlusStepController(ProfilePickerWebContentsHost* host,
                              Profile* profile,
                              base::OnceClosure step_completed_callback);
  ~JatterFrePlusStepController() override;

  void Show(StepSwitchFinishedCallback step_shown_callback, bool reset_state) override;
  void OnNavigateBackRequested() override;

 private:
  void OnPlusPageLoaded(StepSwitchFinishedCallback step_shown_callback);
  void OnPlusChoice(bool get_plus_clicked);

  raw_ptr<Profile> profile_;
  base::OnceClosure step_completed_callback_;
  base::WeakPtrFactory<JatterFrePlusStepController> weak_ptr_factory_{this};
};

#endif