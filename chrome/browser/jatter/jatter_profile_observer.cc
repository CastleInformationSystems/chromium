#include "chrome/browser/jatter/jatter_profile_observer.h"

#include "chrome/browser/browser_process.h"
#include "chrome/browser/jatter/jatter_history_observer_service_factory.h"
#include "chrome/browser/profiles/profile_manager.h"

JatterProfileObserver::JatterProfileObserver() {
  g_browser_process->profile_manager()->AddObserver(this);

  // Optional: initialize for already-loaded profiles
  for (Profile* profile :
       g_browser_process->profile_manager()->GetLoadedProfiles()) {
    OnProfileAdded(profile);
  }
}

JatterProfileObserver::~JatterProfileObserver() {
  g_browser_process->profile_manager()->RemoveObserver(this);
}

void JatterProfileObserver::OnProfileAdded(Profile* profile) {
  if (!profile->IsOffTheRecord()) {
    JatterHistoryObserverServiceFactory::GetForProfile(profile);
  }
}
