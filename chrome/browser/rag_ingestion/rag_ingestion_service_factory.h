// Copyright 2024 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_RAG_INGESTION_RAG_INGESTION_SERVICE_FACTORY_H_
#define CHROME_BROWSER_RAG_INGESTION_RAG_INGESTION_SERVICE_FACTORY_H_

#include "base/no_destructor.h"
#include "chrome/browser/profiles/profile_keyed_service_factory.h"

class RagIngestionService;

class RagIngestionServiceFactory : public ProfileKeyedServiceFactory {
 public:
  static RagIngestionService* GetForProfile(Profile* profile);
  static RagIngestionServiceFactory* GetInstance();

  RagIngestionServiceFactory(const RagIngestionServiceFactory&) = delete;
  RagIngestionServiceFactory& operator=(const RagIngestionServiceFactory&) = delete;

 private:
  friend base::NoDestructor<RagIngestionServiceFactory>;

  RagIngestionServiceFactory();
  ~RagIngestionServiceFactory() override;

  // BrowserContextKeyedServiceFactory:
  std::unique_ptr<KeyedService> BuildServiceInstanceForBrowserContext(
      content::BrowserContext* context) const override;
      
  bool ServiceIsCreatedWithBrowserContext() const override;

  // [NEW] Add this override declaration
  void RegisterProfilePrefs(user_prefs::PrefRegistrySyncable* registry) override;
};

#endif  // CHROME_BROWSER_RAG_INGESTION_RAG_INGESTION_SERVICE_FACTORY_H_