#include "chrome/browser/jatter/jatter_firebase_client_factory.h"
#include "chrome/browser/history/history_service_factory.h"
#include "chrome/browser/jatter/jatter_firebase_client.h"
#include "chrome/browser/profiles/profile.h"
#include "components/pref_registry/pref_registry_syncable.h"

// static
JatterFirebaseClient* JatterFirebaseClientFactory::GetForProfile(Profile* profile) {
  return static_cast<JatterFirebaseClient*>(
      GetInstance()->GetServiceForBrowserContext(profile, true));
}

// static
JatterFirebaseClientFactory* JatterFirebaseClientFactory::GetInstance() {
  static base::NoDestructor<JatterFirebaseClientFactory> instance;
  return instance.get();
}

JatterFirebaseClientFactory::JatterFirebaseClientFactory()
    : ProfileKeyedServiceFactory(
          "JatterFirebaseClient",
          // Only create for regular profiles, never Incognito or Guest
          ProfileSelections::Builder()
              .WithRegular(ProfileSelection::kOriginalOnly)
              .Build()) {
  DependsOn(HistoryServiceFactory::GetInstance());
}

JatterFirebaseClientFactory::~JatterFirebaseClientFactory() = default;

std::unique_ptr<KeyedService> JatterFirebaseClientFactory::BuildServiceInstanceForBrowserContext(
    content::BrowserContext* context) const {
  Profile* profile = Profile::FromBrowserContext(context);
  return std::make_unique<JatterFirebaseClient>(profile);
}

void JatterFirebaseClientFactory::RegisterProfilePrefs(user_prefs::PrefRegistrySyncable* registry) {
  JatterFirebaseClient::RegisterProfilePrefs(registry);
}
