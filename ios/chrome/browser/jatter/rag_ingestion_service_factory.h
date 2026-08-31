// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef IOS_CHROME_BROWSER_JATTER_RAG_INGESTION_SERVICE_FACTORY_H_
#define IOS_CHROME_BROWSER_JATTER_RAG_INGESTION_SERVICE_FACTORY_H_

#include "base/no_destructor.h"
#include "ios/chrome/browser/shared/model/profile/profile_keyed_service_factory_ios.h"

namespace user_prefs {
class PrefRegistrySyncable;
}

class ProfileIOS;
class RagIngestionService;

// Singleton that owns all RagIngestionServices and associates them with
// ProfileIOS.
class RagIngestionServiceFactory : public ProfileKeyedServiceFactoryIOS {
 public:
  static RagIngestionService* GetForProfile(ProfileIOS* profile);
  static RagIngestionServiceFactory* GetInstance();

 private:
  friend class base::NoDestructor<RagIngestionServiceFactory>;

  RagIngestionServiceFactory();
  ~RagIngestionServiceFactory() override;

  // BrowserStateKeyedServiceFactory implementation.
  std::unique_ptr<KeyedService> BuildServiceInstanceFor(
      ProfileIOS* profile) const override;

  void RegisterProfilePrefs(
      user_prefs::PrefRegistrySyncable* registry) override;
};

#endif  // IOS_CHROME_BROWSER_JATTER_RAG_INGESTION_SERVICE_FACTORY_H_