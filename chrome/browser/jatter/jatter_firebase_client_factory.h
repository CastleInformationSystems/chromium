#ifndef CHROME_BROWSER_JATTER_JATTER_FIREBASE_CLIENT_FACTORY_H_
#define CHROME_BROWSER_JATTER_JATTER_FIREBASE_CLIENT_FACTORY_H_

#include "base/no_destructor.h"
#include "chrome/browser/profiles/profile_keyed_service_factory.h"

class Profile;
class JatterFirebaseClient;

namespace user_prefs {
class PrefRegistrySyncable;
}

class JatterFirebaseClientFactory : public ProfileKeyedServiceFactory {
 public:
  static JatterFirebaseClient* GetForProfile(Profile* profile);
  static JatterFirebaseClientFactory* GetInstance();

  JatterFirebaseClientFactory(const JatterFirebaseClientFactory&) = delete;
  JatterFirebaseClientFactory& operator=(const JatterFirebaseClientFactory&) = delete;

 private:
  friend base::NoDestructor<JatterFirebaseClientFactory>;

  JatterFirebaseClientFactory();
  ~JatterFirebaseClientFactory() override;

  // BrowserContextKeyedServiceFactory:
  std::unique_ptr<KeyedService> BuildServiceInstanceForBrowserContext(
      content::BrowserContext* context) const override;
  
  void RegisterProfilePrefs(user_prefs::PrefRegistrySyncable* registry) override;
};

#endif  // CHROME_BROWSER_JATTER_JATTER_FIREBASE_CLIENT_FACTORY_H_