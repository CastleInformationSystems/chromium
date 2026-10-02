#ifndef CHROME_BROWSER_UI_VIEWS_PROFILES_JATTER_FRE_HISTORY_STEP_CONTROLLER_H_
#define CHROME_BROWSER_UI_VIEWS_PROFILES_JATTER_FRE_HISTORY_STEP_CONTROLLER_H_

#include "base/functional/callback.h"
#include "base/memory/weak_ptr.h"
#include "chrome/browser/ui/views/profiles/profile_management_step_controller.h"
#include "components/user_data_importer/common/importer_data_types.h"
#include <string>

class ProfilePickerWebContentsHost;

class JatterFreHistoryStepController : public ProfileManagementStepController {
 public:
  JatterFreHistoryStepController(ProfilePickerWebContentsHost* host,
                                 base::OnceClosure step_completed_callback);
  ~JatterFreHistoryStepController() override;

  void Show(StepSwitchFinishedCallback step_shown_callback, bool reset_state) override;
  void OnNavigateBackRequested() override;

 private:
  void OnHistoryPageLoaded(StepSwitchFinishedCallback step_shown_callback);
  void OnHistoryChoice(bool should_import, const user_data_importer::SourceProfile& source_profile);

  base::OnceClosure step_completed_callback_;
  base::WeakPtrFactory<JatterFreHistoryStepController> weak_ptr_factory_{this};
};

#endif