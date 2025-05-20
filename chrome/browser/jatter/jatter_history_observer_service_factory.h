#ifndef JATTER_HISTORY_OBSERVER_SERVICE_FACTORY_H_
#define JATTER_HISTORY_OBSERVER_SERVICE_FACTORY_H_

#include "base/no_destructor.h"
#include "components/keyed_service/content/browser_context_keyed_service_factory.h"

class Profile;
class JatterHistoryObserver;

class JatterHistoryObserverServiceFactory
    : public BrowserContextKeyedServiceFactory {
 public:
  static JatterHistoryObserver* GetForProfile(Profile* profile);

  static JatterHistoryObserverServiceFactory* GetInstance();

 private:
  friend base::NoDestructor<JatterHistoryObserverServiceFactory>;

  JatterHistoryObserverServiceFactory();
  ~JatterHistoryObserverServiceFactory() override = default;

  // BrowserContextKeyedServiceFactory:
  std::unique_ptr<KeyedService> BuildServiceInstanceForBrowserContext(
      content::BrowserContext* context) const override;
  content::BrowserContext* GetBrowserContextToUse(
      content::BrowserContext* context) const override;
};

#endif  // JATTER_HISTORY_OBSERVER_SERVICE_FACTORY_H_
