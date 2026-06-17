// Copyright 2024 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/jatter/analytics/jatter_analytics_service_factory.h"

#include "chrome/browser/jatter/analytics/jatter_analytics_service.h"
#include "chrome/browser/profiles/profile.h"
#include "components/keyed_service/content/browser_context_dependency_manager.h"

// static
JatterAnalyticsService* JatterAnalyticsServiceFactory::GetForProfile(
    Profile* profile) {
  return static_cast<JatterAnalyticsService*>(
      GetInstance()->GetServiceForBrowserContext(profile, true));
}

// static
JatterAnalyticsServiceFactory* JatterAnalyticsServiceFactory::GetInstance() {
  static base::NoDestructor<JatterAnalyticsServiceFactory> instance;
  return instance.get();
}

JatterAnalyticsServiceFactory::JatterAnalyticsServiceFactory()
    : BrowserContextKeyedServiceFactory(
          "JatterAnalyticsService",
          BrowserContextDependencyManager::GetInstance()) {
  // If your analytics service relies on other KeyedServices, declare them here.
  // For example, if you depend on HostContentSettingsMap:
  // DependsOn(HostContentSettingsMapFactory::GetInstance());
}

JatterAnalyticsServiceFactory::~JatterAnalyticsServiceFactory() = default;

std::unique_ptr<KeyedService>
JatterAnalyticsServiceFactory::BuildServiceInstanceForBrowserContext(
    content::BrowserContext* context) const {
  Profile* profile = Profile::FromBrowserContext(context);
  return std::make_unique<JatterAnalyticsService>(profile);
}

content::BrowserContext* JatterAnalyticsServiceFactory::GetBrowserContextToUse(
    content::BrowserContext* context) const {
  // Return nullptr if we do NOT want to track telemetry in Incognito mode.
  // (This is standard practice for privacy-respecting telemetry).
  if (context->IsOffTheRecord()) {
    return nullptr;
  }
  return context;
}

bool JatterAnalyticsServiceFactory::ServiceIsCreatedWithBrowserContext() const {
  // Returning true forces Chromium to build this service immediately 
  // when the browser launches, bypassing lazy-loading!
  return true;
}
