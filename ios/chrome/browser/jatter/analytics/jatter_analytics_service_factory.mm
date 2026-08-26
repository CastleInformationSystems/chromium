// Copyright 2024 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ios/chrome/browser/jatter/analytics/jatter_analytics_service_factory.h"

#include "ios/chrome/browser/jatter/analytics/jatter_analytics_service.h"
#include "ios/chrome/browser/shared/model/profile/profile_ios.h"

// static
JatterAnalyticsService* JatterAnalyticsServiceFactory::GetForProfile(
    ProfileIOS* profile) {
  // Uses the modern type-safe getter from the new base class
  return GetInstance()->GetServiceForProfileAs<JatterAnalyticsService>(
      profile, /*create=*/true);
}

// static
JatterAnalyticsServiceFactory* JatterAnalyticsServiceFactory::GetInstance() {
  static base::NoDestructor<JatterAnalyticsServiceFactory> instance;
  return instance.get();
}

JatterAnalyticsServiceFactory::JatterAnalyticsServiceFactory()
    : ProfileKeyedServiceFactoryIOS("JatterAnalyticsService") {
}

JatterAnalyticsServiceFactory::~JatterAnalyticsServiceFactory() = default;

std::unique_ptr<KeyedService>
JatterAnalyticsServiceFactory::BuildServiceInstanceFor(
    ProfileIOS* profile) const {
  return std::make_unique<JatterAnalyticsService>(profile);
}

// Note: The kDefault trait automatically handles returning nullptr for Incognito,
// so GetBrowserStateToUse() is no longer needed.