#include "chrome/browser/jatter/jatter_profile_observer.h"

#include "chrome/browser/browser_process.h"
#include "chrome/browser/jatter/jatter_history_observer_service_factory.h"
#include "chrome/browser/profiles/profile_manager.h"

JatterProfileObserver::JatterProfileObserver() {
  profile_manager_observer_.Observe(g_browser_process->profile_manager());
}

JatterProfileObserver::~JatterProfileObserver() = default;

void JatterProfileObserver::OnProfileAdded(Profile* profile) {
  if (!profile->IsOffTheRecord()) {
    JatterHistoryObserverServiceFactory::GetForProfile(profile);
  }
}

void JatterProfileObserver::OnProfileManagerDestroying() {
  profile_manager_observer_.Reset();
}
