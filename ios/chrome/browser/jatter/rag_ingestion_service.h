// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef IOS_CHROME_BROWSER_JATTER_RAG_INGESTION_SERVICE_H_
#define IOS_CHROME_BROWSER_JATTER_RAG_INGESTION_SERVICE_H_

#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <vector>

#include "base/containers/unique_ptr_adapters.h"
#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "base/task/cancelable_task_tracker.h"
#include "base/timer/timer.h"
#include "base/values.h"
#include "components/content_settings/core/browser/content_settings_observer.h"
#include "components/content_settings/core/common/content_settings_pattern.h"
#include "components/favicon_base/favicon_types.h"
#include "components/keyed_service/core/keyed_service.h"
#include "ios/chrome/browser/jatter/rag_ingestion_network_client.h"
#include "services/network/public/cpp/simple_url_loader.h"
#include "url/gurl.h"

class ProfileIOS;

class RagIngestionService : public KeyedService,
                            public content_settings::Observer {
 public:
  enum class UserPermission { kUndecided, kGranted, kDenied };
  
  enum class BackendStatus { 
    kKnownAllowed,
    kUnknown,
    kKnownBlocked
  };

  struct BackendPermissionInfo {
    BackendPermissionInfo();
    BackendPermissionInfo(const BackendPermissionInfo&);
    ~BackendPermissionInfo();
    
    BackendStatus status = BackendStatus::kUnknown;
    std::string canonical_host;
    std::string site_name;
    bool can_prompt = false;
    double last_success_at_ms = 0;
    bool is_learning = false;
    bool is_error = false;
    bool requires_upgrade = false;
  };

  explicit RagIngestionService(ProfileIOS* profile);
  ~RagIngestionService() override;

  void Shutdown() override;

  // --- FLOW A: CONSENT ---
  UserPermission GetUserPermission(const GURL& url);
  void SetUserPermission(const GURL& url, UserPermission status);

  // --- FLOW B: CAPABILITY ---
  void CheckBackendStatus(
      const GURL& url,
      base::OnceCallback<void(BackendPermissionInfo)> callback);

  // --- FLOW C: INGESTION ---
  void SetPrivateKey(const std::string& private_key_base64);

  // Starts the 4-step pipeline: Chunk -> Embed -> Encrypt -> Ingest
  void StartPassiveLearningPipeline(const GURL& url, 
                                    const std::string& canonical_host,
                                    const std::string& page_title,
                                    const std::string& inner_text);

  void StartDocumentIngestion(const GURL& url,
                              const std::string& page_title, 
                              const std::string& file_bytes, 
                              const std::string& mime_type,
                              const std::string& filename);

  void SyncLocalSettingFromBackend(const GURL& url, UserPermission status);

  void OnContentSettingChanged(const ContentSettingsPattern& primary_pattern,
                               const ContentSettingsPattern& secondary_pattern,
                               ContentSettingsTypeSet content_type_set) override;

  base::WeakPtr<RagIngestionService> GetWeakPtr() { return weak_factory_.GetWeakPtr(); }

 private:
  void OnBackendResponse(
      base::OnceCallback<void(BackendPermissionInfo)> client_callback,
      std::optional<base::Value> result);

  void FetchRootPageMetadata(const GURL& root_url, 
                             const GURL& original_url,
                             const std::string& active_tab_title);
                             
  void OnRootPageFetched(network::SimpleURLLoader* loader_ptr, 
                         const GURL& original_url, 
                         const std::string& active_tab_title,
                         std::optional<std::string> response_body);

  void FetchFavicon(const GURL& original_url, const GURL& icon_url, RagSiteMetadata metadata);
  
  void OnFaviconFetched(network::SimpleURLLoader* loader_ptr,
                        const GURL& original_url, 
                        const GURL& icon_url,
                        RagSiteMetadata metadata, 
                        std::optional<std::string> response_body);
                        
  void OnNativeFaviconFetched(const GURL& original_url,
                              RagSiteMetadata metadata,
                              const favicon_base::FaviconImageResult& result);

  void FetchManifest(const GURL& original_url, const std::string& manifest_url, const std::string& icon_url, RagSiteMetadata metadata);
  
  void OnManifestFetched(network::SimpleURLLoader* loader_ptr, 
                         const GURL& original_url, 
                         const std::string& icon_url, 
                         RagSiteMetadata metadata, 
                         std::optional<std::string> response_body);

  void FinalizePermissionGrant(const GURL& url, RagPermissionStatus status, const RagSiteMetadata& metadata);

  std::string ExtractMetaTag(const std::string& html, const std::string& key);
  std::string ExtractIconUrl(const std::string& html, const GURL& root_url);
  std::string ExtractBestSiteName(const std::string& html, const GURL& root_url);
  std::string ExtractManifestUrl(const std::string& html, const GURL& root_url);

  void OnDocumentChunked(const GURL& url, std::optional<base::Value> result);
  void OnChunksEmbedded(const GURL& url, 
                        std::vector<std::string> chunks, 
                        std::optional<base::Value> result);
  
  std::vector<std::string> EncryptChunks(const std::vector<std::string>& chunks);
  std::string FilterMhtmlToTextOnly(const std::string& mhtml_data);
  void OnDocumentParsed(const GURL& url, std::optional<base::Value> result);
  std::string EncryptSingleString(const std::string& clear_text);

  base::CancelableTaskTracker favicon_task_tracker_;
  raw_ptr<ProfileIOS> profile_;
  
  std::set<std::unique_ptr<network::SimpleURLLoader>, base::UniquePtrComparator> metadata_loaders_;
  std::unique_ptr<RagIngestionNetworkClient> network_client_;
  
  std::string private_key_base64_;
  bool is_updating_from_backend_ = false; 

  base::WeakPtrFactory<RagIngestionService> weak_factory_{this};
};

#endif  // IOS_CHROME_BROWSER_JATTER_RAG_INGESTION_SERVICE_H_