// Copyright 2024 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_JATTER_ANALYTICS_JATTER_ANALYTICS_SERVICE_FACTORY_H_
#define CHROME_BROWSER_JATTER_ANALYTICS_JATTER_ANALYTICS_SERVICE_FACTORY_H_

#include "base/no_destructor.h"
#include "components/keyed_service/content/browser_context_keyed_service_factory.h"

class JatterAnalyticsService;
class Profile;

class JatterAnalyticsServiceFactory : public BrowserContextKeyedServiceFactory {
 public:
  // Returns the JatterAnalyticsService for the given |profile|.
  // If the service does not exist, it will be created.
  static JatterAnalyticsService* GetForProfile(Profile* profile);

  // Returns the singleton instance of the factory.
  static JatterAnalyticsServiceFactory* GetInstance();

  JatterAnalyticsServiceFactory(const JatterAnalyticsServiceFactory&) = delete;
  JatterAnalyticsServiceFactory& operator=(
      const JatterAnalyticsServiceFactory&) = delete;

 private:
  friend base::NoDestructor<JatterAnalyticsServiceFactory>;

  JatterAnalyticsServiceFactory();
  ~JatterAnalyticsServiceFactory() override;

  // BrowserContextKeyedServiceFactory implementation:
  std::unique_ptr<KeyedService> BuildServiceInstanceForBrowserContext(
      content::BrowserContext* context) const override;
      
  // Optional: Override this if you want analytics to run in Incognito.
  // By default, returning nullptr means the service won't be created for OTR profiles.
  content::BrowserContext* GetBrowserContextToUse(
      content::BrowserContext* context) const override;

  bool ServiceIsCreatedWithBrowserContext() const override;
};

#endif  // CHROME_BROWSER_JATTER_ANALYTICS_JATTER_ANALYTICS_SERVICE_FACTORY_H_
