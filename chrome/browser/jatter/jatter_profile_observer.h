#ifndef JATTER_PROFILE_OBSERVER_H_
#define JATTER_PROFILE_OBSERVER_H_

#include "base/scoped_observation.h"
#include "chrome/browser/profiles/profile_manager_observer.h"

class ProfileManager;
class Profile;

class JatterProfileObserver : public ProfileManagerObserver {
 public:
  JatterProfileObserver();
  ~JatterProfileObserver() override;

 private:
  // ProfileManagerObserver:
  void OnProfileAdded(Profile* profile) override;
  void OnProfileManagerDestroying() override;

  base::ScopedObservation<ProfileManager, ProfileManagerObserver>
      profile_manager_observer_{this};
};

#endif  // JATTER_PROFILE_OBSERVER_H_
