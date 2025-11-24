#include "chrome/browser/jatter/jatter_history_observer_service_factory.h"

#include "chrome/browser/jatter/jatter_history_observer.h"
#include "chrome/browser/profiles/profile.h"
#include "components/keyed_service/content/browser_context_dependency_manager.h"

#include "chrome/browser/history/history_service_factory.h"

JatterHistoryObserverServiceFactory::JatterHistoryObserverServiceFactory()
    : BrowserContextKeyedServiceFactory(
          "JatterHistoryObserver",
          BrowserContextDependencyManager::GetInstance()) {
  DependsOn(HistoryServiceFactory::GetInstance());
}

JatterHistoryObserver* JatterHistoryObserverServiceFactory::GetForProfile(
    Profile* profile) {
  return static_cast<JatterHistoryObserver*>(
      GetInstance()->GetServiceForBrowserContext(profile, /*create=*/true));
}

JatterHistoryObserverServiceFactory*
JatterHistoryObserverServiceFactory::GetInstance() {
  static base::NoDestructor<JatterHistoryObserverServiceFactory> instance;
  return instance.get();
}

std::unique_ptr<KeyedService>
JatterHistoryObserverServiceFactory::BuildServiceInstanceForBrowserContext(
    content::BrowserContext* context) const {
  Profile* profile = Profile::FromBrowserContext(context);
  return std::make_unique<JatterHistoryObserver>(profile);
}
