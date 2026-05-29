// Copyright 2024 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_RAG_INGESTION_RAG_INGESTION_SERVICE_H_
#define CHROME_BROWSER_RAG_INGESTION_RAG_INGESTION_SERVICE_H_

#include <map>
#include <string>
#include <vector>
#include <memory>
#include <optional>
#include <set>

#include "base/containers/unique_ptr_adapters.h"
#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "base/task/cancelable_task_tracker.h"
#include "base/timer/timer.h"
#include "base/values.h"
#include "chrome/browser/rag_ingestion/rag_ingestion_network_client.h"
#include "chrome/browser/rag_ingestion/hidden_crawl_job.h"
#include "components/content_settings/core/browser/content_settings_observer.h"
#include "components/content_settings/core/common/content_settings_pattern.h"
#include "components/favicon_base/favicon_types.h"
#include "components/keyed_service/core/keyed_service.h"
#include "content/public/browser/download_manager.h"
#include "crypto/aead.h"
#include "services/network/public/cpp/simple_url_loader.h"
#include "url/gurl.h"

class Profile;

class RagIngestionService : public KeyedService,
                            public content::DownloadManager::Observer,
                            public download::DownloadItem::Observer,
                            public content_settings::Observer {
 public:
  // [RENAME] "PermissionStatus" was too vague. Let's be specific.
  enum class UserPermission { kUndecided, kGranted, kDenied };
  
  // Update the Enum to be more descriptive for the internal flow
  enum class BackendStatus { 
    kKnownAllowed,   // Server has record: "User allowed this before"
    kUnknown,        // Server has no record: "New site"
    kKnownBlocked    // Server has record: "User blocked this"
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

  explicit RagIngestionService(Profile* profile);
  ~RagIngestionService() override;

  // KeyedService implementation.
  void Shutdown() override;

  // --- FLOW A: CONSENT (User & Disk) ---
  
  // Reads from HostContentSettingsMap (Fast, Synchronous)
  UserPermission GetUserPermission(const GURL& url);

  // Writes to HostContentSettingsMap AND notifies Backend (Sync)
  void SetUserPermission(const GURL& url, UserPermission status);

  // void AddIngestionUrl(const GURL& url, base::OnceClosure on_success = base::NullCallback());

  // --- FLOW B: CAPABILITY (Network) ---
  
  // Checks what the backend knows about this profile/domain.
  void CheckBackendStatus(
      const GURL& url,
      base::OnceCallback<void(BackendPermissionInfo)> callback);

  // --- FLOW C: INGESTION ---
  void SetPrivateKey(const std::string& private_key_base64);

  // Starts the 4-step pipeline: Chunk -> Embed -> Encrypt -> Ingest
  void StartPassiveLearningPipeline(const GURL& url, const std::string& inner_text);

  void StartDocumentIngestion(const GURL& url,
                              const std::string& page_title, 
                              const std::string& file_bytes, 
                              const std::string& mime_type,
                              const std::string& filename);

  // The entry point for PDF bytes
  void ExtractAndIngestPdf(const GURL& url, const std::vector<uint8_t>& pdf_bytes);

   // Safely update the local cache without bouncing a network request back to the server
  void SyncLocalSettingFromBackend(const GURL& url, UserPermission status);

  // Catch changes from chrome://settings
  void OnContentSettingChanged(const ContentSettingsPattern& primary_pattern,
                               const ContentSettingsPattern& secondary_pattern,
                               ContentSettingsTypeSet content_type_set) override;

  base::WeakPtr<RagIngestionService> GetWeakPtr() { return weak_factory_.GetWeakPtr(); }

 private:
  // // Starts both the Maintenance Loop (Hours) and the Polling Loop (Seconds).
  // void StartServiceLoops();
  // void StopServiceLoops();

  // --- BACKGROUND WORKERS ---
  
  // // 1. The "Wake Up" Call (Launch & Every N Hours)
  // void TriggerIngestion();
  // void OnTriggerIngestionResponse(std::optional<base::Value> result);

  // // 2. The "Heartbeat" (Every 30 Seconds, conditionally)
  // void StartPolling();
  // void StopPolling();
  // void OnPollTimerFired();
  
  // // 3. The Processor
  // void OnMessagesReceived(std::optional<base::Value> result);
  // void ProcessIngestionJob(const base::DictValue& message_dict);
  
  // // Job Execution
  // void RunHiddenCrawl(const GURL& url, const std::string& message_id);
  // // [SYNCED] Matches HiddenCrawlJob signature: (Text, Links)
  // void OnCrawlComplete(std::string message_id, 
  //                      const std::string& html, 
  //                      const std::string& inner_text,
  //                      bool is_auth_wall);

  // Network Callback wrapper
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

  // Manifest Fetching (Priority 1)
  void FetchManifest(const GURL& original_url, const std::string& manifest_url, const std::string& icon_url, RagSiteMetadata metadata);
  void OnManifestFetched(network::SimpleURLLoader* loader_ptr, 
                         const GURL& original_url, 
                         const std::string& icon_url, 
                         RagSiteMetadata metadata, 
                         std::optional<std::string> response_body);

  // The final step that actually calls the Network Client
  void FinalizePermissionGrant(const GURL& url, RagPermissionStatus status, const RagSiteMetadata& metadata);

  // Parsing Helpers
  std::string ExtractMetaTag(const std::string& html, const std::string& key);
  std::string ExtractIconUrl(const std::string& html, const GURL& root_url);

  // Site Name Priority Extractors (Priorities 2-8)
  std::string ExtractBestSiteName(const std::string& html, const GURL& root_url);
  std::string ExtractManifestUrl(const std::string& html, const GURL& root_url);

  // --- PASSIVE LEARNING PIPELINE CALLBACKS ---
  void OnDocumentChunked(const GURL& url, std::optional<base::Value> result);
  void OnChunksEmbedded(const GURL& url, 
                        std::vector<std::string> chunks, 
                        std::optional<base::Value> result);
  
  // Helper to encrypt the chunks before sending them to ingestDocument
  std::vector<std::string> EncryptChunks(const std::vector<std::string>& chunks);

  void InitializeDownloadObserver();

  // --- DownloadManager::Observer ---
  void OnDownloadCreated(content::DownloadManager* manager,
                         download::DownloadItem* item) override;

  // --- DownloadItem::Observer ---
  void OnDownloadUpdated(download::DownloadItem* item) override;
  void OnDownloadDestroyed(download::DownloadItem* item) override;

  // Strips images, CSS, and other non-HTML assets from the MHTML string.
  // Maintains the multipart boundary structure for backend parsing.
  std::string FilterMhtmlToTextOnly(const std::string& mhtml_data);
  void OnDocumentParsed(const GURL& url, std::optional<base::Value> result);
  std::string EncryptSingleString(const std::string& clear_text);

  base::CancelableTaskTracker favicon_task_tracker_;

  raw_ptr<Profile> profile_;
  
  // Timer 1: Long-running maintenance (e.g., every 4 hours)
  base::RepeatingTimer ingest_trigger_timer_;

  // Timer 2: Short-running poller (e.g., every 5 seconds)
  // Switched to OneShotTimer for recursive scheduling (safer for network).
  base::RepeatingTimer polling_timer_;

  // Loader for metadata/icon fetching (keeps request alive)
  std::set<std::unique_ptr<network::SimpleURLLoader>, base::UniquePtrComparator> metadata_loaders_;
  
  std::unique_ptr<RagIngestionNetworkClient> network_client_;
  // std::map<std::string, std::unique_ptr<HiddenCrawlJob>> active_crawl_jobs_;

  std::string private_key_base64_;

  // Prevents infinite loops when syncing
  bool is_updating_from_backend_ = false; 

  base::WeakPtrFactory<RagIngestionService> weak_factory_{this};
};

#endif  // CHROME_BROWSER_RAG_INGESTION_RAG_INGESTION_SERVICE_H_