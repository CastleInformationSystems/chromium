// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#import "ios/chrome/browser/jatter/rag_ingestion_service_factory.h"

#import "components/keyed_service/ios/browser_state_dependency_manager.h"
#import "components/pref_registry/pref_registry_syncable.h"
#import "ios/chrome/browser/content_settings/model/host_content_settings_map_factory.h"
#import "ios/chrome/browser/jatter/rag_ingestion_network_client.h"
#import "ios/chrome/browser/jatter/rag_ingestion_service.h"
#import "ios/chrome/browser/shared/model/profile/profile_ios.h"


// static
RagIngestionService* RagIngestionServiceFactory::GetForProfile(
    ProfileIOS* profile) {
  return GetInstance()->GetServiceForProfileAs<RagIngestionService>(
      profile, /*create=*/true);
}

// static
RagIngestionServiceFactory* RagIngestionServiceFactory::GetInstance() {
  static base::NoDestructor<RagIngestionServiceFactory> instance;
  return instance.get();
}

RagIngestionServiceFactory::RagIngestionServiceFactory()
    : ProfileKeyedServiceFactoryIOS("RagIngestionService",
                                    ProfileSelection::kRedirectedInIncognito) {
  // Declare dependencies so the KeyedService system initializes them in the correct order.
  DependsOn(ios::HostContentSettingsMapFactory::GetInstance());
}

RagIngestionServiceFactory::~RagIngestionServiceFactory() = default;

std::unique_ptr<KeyedService> RagIngestionServiceFactory::BuildServiceInstanceFor(
    ProfileIOS* profile) const {
  return std::make_unique<RagIngestionService>(profile);
}

void RagIngestionServiceFactory::RegisterProfilePrefs(
    user_prefs::PrefRegistrySyncable* registry) {
  registry->RegisterStringPref(kRagIngestionProfileGuidPref, std::string());
}