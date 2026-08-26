// Copyright 2024 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef IOS_CHROME_BROWSER_JATTER_ANALYTICS_JATTER_ANALYTICS_SERVICE_FACTORY_H_
#define IOS_CHROME_BROWSER_JATTER_ANALYTICS_JATTER_ANALYTICS_SERVICE_FACTORY_H_

#include "base/no_destructor.h"
#include "ios/chrome/browser/shared/model/profile/profile_keyed_service_factory_ios.h"

class JatterAnalyticsService;
class ProfileIOS;

class JatterAnalyticsServiceFactory : public ProfileKeyedServiceFactoryIOS {
 public:
  static JatterAnalyticsService* GetForProfile(ProfileIOS* profile);
  static JatterAnalyticsServiceFactory* GetInstance();

  JatterAnalyticsServiceFactory(const JatterAnalyticsServiceFactory&) = delete;
  JatterAnalyticsServiceFactory& operator=(const JatterAnalyticsServiceFactory&) = delete;

 private:
  friend class base::NoDestructor<JatterAnalyticsServiceFactory>;

  JatterAnalyticsServiceFactory();
  ~JatterAnalyticsServiceFactory() override;

  // ProfileKeyedServiceFactoryIOS implementation:
  std::unique_ptr<KeyedService> BuildServiceInstanceFor(
      ProfileIOS* profile) const override;
};

#endif  // IOS_CHROME_BROWSER_JATTER_ANALYTICS_JATTER_ANALYTICS_SERVICE_FACTORY_H_