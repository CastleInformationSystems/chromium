// Copyright 2024 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/rag_ingestion/rag_ingestion_service_factory.h"

#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/rag_ingestion/rag_ingestion_service.h"
#include "components/pref_registry/pref_registry_syncable.h"

// static
RagIngestionService* RagIngestionServiceFactory::GetForProfile(Profile* profile) {
  return static_cast<RagIngestionService*>(
      GetInstance()->GetServiceForBrowserContext(profile, true));
}

// static
RagIngestionServiceFactory* RagIngestionServiceFactory::GetInstance() {
  static base::NoDestructor<RagIngestionServiceFactory> instance;
  return instance.get();
}

RagIngestionServiceFactory::RagIngestionServiceFactory()
    : ProfileKeyedServiceFactory(
          "RagIngestionService",
          ProfileSelections::Builder()
              .WithRegular(ProfileSelection::kOriginalOnly)
              .WithGuest(ProfileSelection::kNone)
              .Build()) {}

RagIngestionServiceFactory::~RagIngestionServiceFactory() = default;

std::unique_ptr<KeyedService>
RagIngestionServiceFactory::BuildServiceInstanceForBrowserContext(
    content::BrowserContext* context) const {
  return std::make_unique<RagIngestionService>(
      Profile::FromBrowserContext(context));
}

bool RagIngestionServiceFactory::ServiceIsCreatedWithBrowserContext() const {
  return true;
}

// [NEW IMPLEMENTATION]
void RagIngestionServiceFactory::RegisterProfilePrefs(
    user_prefs::PrefRegistrySyncable* registry) {
  // Registers the GUID pref with a default empty string value.
  // This prevents the "Preference not registered" crash.
  registry->RegisterStringPref("beacon.rag_ingestion.profile_guid", "");
}