#ifndef JATTER_PROFILE_OBSERVER_H_
#define JATTER_PROFILE_OBSERVER_H_

#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/profiles/profile_manager_observer.h"

class JatterProfileObserver : public ProfileManagerObserver {
 public:
  JatterProfileObserver();
  ~JatterProfileObserver() override;

  // Called when any Profile is created (async or during startup)
  void OnProfileAdded(Profile* profile) override;
};

#endif  // JATTER_PROFILE_OBSERVER_H_
