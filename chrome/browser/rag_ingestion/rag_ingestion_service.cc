// Copyright 2024 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/rag_ingestion/rag_ingestion_service.h"

#include <string_view>

#include "base/auto_reset.h"
#include "base/base64.h"
#include "base/containers/span.h"
#include "base/files/file_util.h"
#include "base/json/json_reader.h"
#include "base/logging.h"
#include "base/numerics/safe_conversions.h"
#include "base/strings/escape.h"
#include "base/strings/string_util.h"
#include "base/strings/utf_string_conversions.h"
#include "base/task/single_thread_task_runner.h"
#include "base/task/thread_pool.h"
#include "base/time/time.h"
#include "chrome/browser/content_settings/host_content_settings_map_factory.h"
#include "chrome/browser/favicon/favicon_service_factory.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/browser_list.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface_iterator.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/browser/ui/rag_ingestion/rag_ingestion_page_action_controller.h"
#include "chrome/services/rag_ingestion_pdf/public/mojom/rag_ingestion_pdf.mojom.h"
#include "components/content_settings/core/browser/host_content_settings_map.h"
#include "components/content_settings/core/common/content_settings_types.h"
#include "components/favicon/core/favicon_service.h"
#include "content/public/browser/download_manager.h"
#include "content/public/browser/service_process_host.h"
#include "content/public/browser/storage_partition.h"
#include "crypto/random.h"
#include "net/traffic_annotation/network_traffic_annotation.h"
#include "sandbox/policy/mojom/sandbox.mojom.h"
#include "services/network/public/cpp/resource_request.h"
#include "services/network/public/cpp/shared_url_loader_factory.h"
#include "services/network/public/mojom/url_response_head.mojom.h"
#include "third_party/boringssl/src/include/openssl/cipher.h"
#include "third_party/boringssl/src/include/openssl/evp.h"
#include "third_party/re2/src/re2/re2.h"
#include "ui/gfx/image/image_skia.h"
#include "ui/gfx/codec/png_codec.h"

namespace content {
template <>
inline sandbox::mojom::Sandbox GetServiceSandboxType<rag_ingestion::mojom::PdfTextExtractor>() {
  return sandbox::mojom::Sandbox::kUtility;
}
}  // namespace content

namespace {

std::string GetIngestionMimeType(download::DownloadItem* item) {
  std::string network_mime = base::ToLowerASCII(item->GetMimeType());
  
  // Always prefer the final target path on disk.
  base::FilePath path = item->GetTargetFilePath();
  if (path.empty()) {
    path = base::FilePath::FromUTF8Unsafe(item->GetSuggestedFilename());
  }

  // path.Extension() returns std::wstring on Windows and std::string on Mac.
  // By wrapping it in base::FilePath(), we can safely call AsUTF8Unsafe() everywhere.
  std::string ext = base::ToLowerASCII(base::FilePath(path.Extension()).AsUTF8Unsafe());

  // Extension is the source of truth, network mime is the fallback
  if (ext == ".pdf" || network_mime == "application/pdf") {
    return "application/pdf";
  } else if (ext == ".docx") {
    return "application/vnd.openxmlformats-officedocument.wordprocessingml.document";
  } else if (ext == ".xlsx") {
    return "application/vnd.openxmlformats-officedocument.spreadsheetml.sheet";
  } else if (ext == ".pptx") {
    return "application/vnd.openxmlformats-officedocument.presentationml.presentation";
  } else if (ext == ".rtf" || network_mime == "application/rtf" || network_mime == "text/rtf") {
    return "application/rtf"; 
  }
  
  return ""; 
}

} // namespace

// // Loop 1: How often to trigger the "Re-crawl check" (The "N hours")
// constexpr base::TimeDelta kIngestTriggerInterval = base::Hours(1);

// // Loop 2: How often to poll for new jobs
// constexpr base::TimeDelta kPollingInterval = base::Seconds(30);

// [ADD] Traffic Annotation for the Metadata Fetcher
constexpr net::NetworkTrafficAnnotationTag kMetadataTrafficAnnotation =
    net::DefineNetworkTrafficAnnotation("rag_ingestion_metadata", R"(
        semantics {
          sender: "RAG Ingestion Service"
          description: "Fetches website root page and favicon to extract visual metadata (title, icon) when the user grants ingestion permission."
          trigger: "User clicks 'Allow' on the RAG Ingestion permission prompt."
          data: "None (Anonymous request)."
          destination: WEBSITE
        }
        policy {
          cookies_allowed: NO
          setting: "This feature is strictly opt-in."
        })");

void RagIngestionService::InitializeDownloadObserver() {
  content::DownloadManager* download_manager = profile_->GetDownloadManager();
  if (download_manager) {
    download_manager->AddObserver(this);
  }
}

RagIngestionService::RagIngestionService(Profile* profile) : profile_(profile) {
  network_client_ = std::make_unique<RagIngestionNetworkClient>(profile);
  // StartServiceLoops();
  if (HostContentSettingsMap* map = HostContentSettingsMapFactory::GetForProfile(profile_)) {
      map->AddObserver(this);
  }

  base::SingleThreadTaskRunner::GetCurrentDefault()->PostTask(
      FROM_HERE,
      base::BindOnce(&RagIngestionService::InitializeDownloadObserver,
                     weak_factory_.GetWeakPtr()));
}

RagIngestionService::~RagIngestionService() = default;

RagIngestionService::BackendPermissionInfo::BackendPermissionInfo() = default;
RagIngestionService::BackendPermissionInfo::BackendPermissionInfo(
    const BackendPermissionInfo&) = default;
RagIngestionService::BackendPermissionInfo::~BackendPermissionInfo() = default;

void RagIngestionService::Shutdown() {
  // StopServiceLoops();

  // Clean up hidden web contents to prevent Use-After-Free
  // active_crawl_jobs_.clear(); 
  
  // Cancel and clean up any pending concurrent network requests
  metadata_loaders_.clear();

  content::DownloadManager* download_manager = profile_->GetDownloadManager();
  if (download_manager) {
    download_manager->RemoveObserver(this);
  }

  if (HostContentSettingsMap* map = HostContentSettingsMapFactory::GetForProfile(profile_)) {
      map->RemoveObserver(this);
  }
}

// ===========================================================================
// Loop Management
// ===========================================================================

// void RagIngestionService::StartServiceLoops() {
//   // 1. Start the Long-Running Timer (N Hours)
//   ingest_trigger_timer_.Start(
//       FROM_HERE, kIngestTriggerInterval,
//       base::BindRepeating(&RagIngestionService::TriggerIngestion, 
//                           weak_factory_.GetWeakPtr()));

//   // 2. "On Launch... call ingest"
//   TriggerIngestion();
//   // LOG(WARNING) << "[RAG DEBUG] Fake crawl job scheduled in 10 seconds...";
//   // base::SingleThreadTaskRunner::GetCurrentDefault()->PostDelayedTask(
//   //     FROM_HERE,
//   //     base::BindOnce(&RagIngestionService::RunHiddenCrawl, 
//   //                    weak_factory_.GetWeakPtr(), 
//   //                    GURL("facebook.com"), // Fast, safe, public page
//   //                    "debug_msg_id_999"),
//   //     base::Seconds(10));
// }

// void RagIngestionService::StopServiceLoops() {
//   ingest_trigger_timer_.Stop();
//   polling_timer_.Stop();
// }

// // ===========================================================================
// // PHASE 1: WAKE UP (TriggerIngestion)
// // ===========================================================================

// void RagIngestionService::TriggerIngestion() {
//   LOG(INFO) << "[RAG] Loop: Waking up (TriggerIngestion)...";
//   if (!network_client_) return;

//   // API: POST /ingest
//   network_client_->TriggerIngestion(
//       base::BindOnce(&RagIngestionService::OnTriggerIngestionResponse,
//                      weak_factory_.GetWeakPtr()));
// }

// void RagIngestionService::OnTriggerIngestionResponse(std::optional<base::Value> result) {
//   LOG(INFO) << "[RAG] Loop: Trigger sent. Starting Heartbeat.";
//   // Protocol: "Every time ingest is called, start the timer again"
//   // We don't really care about the result of /ingest, just that we kicked it off.
//   StartPolling();
// }

// // ===========================================================================
// // PHASE 2: HEARTBEAT (GetIngestionMessages)
// // ===========================================================================

// void RagIngestionService::StartPolling() {
//   // "Start a timer (if not already active)"
//   if (!polling_timer_.IsRunning()) {
//     LOG(INFO) << "[RAG] Heartbeat: STARTED (30s interval)";
//     polling_timer_.Start(
//         FROM_HERE, kPollingInterval,
//         base::BindRepeating(&RagIngestionService::OnPollTimerFired,
//                             weak_factory_.GetWeakPtr()));
    
//     // Optional: Fire immediately so we don't wait 30s for the first batch
//     OnPollTimerFired();
//   }
// }

// void RagIngestionService::StopPolling() {
//   if (polling_timer_.IsRunning()) {
//     LOG(INFO) << "[RAG] Heartbeat: STOPPED (No active jobs)";
//     polling_timer_.Stop();
//   }
// }

// void RagIngestionService::OnPollTimerFired() {
//   if (!network_client_) return;
  
//   // API: POST /getIngestionMessages
//   VLOG(1) << "[RAG] Heartbeat: Checking for messages...";
//   network_client_->GetIngestionMessages(
//       base::BindOnce(&RagIngestionService::OnMessagesReceived,
//                      weak_factory_.GetWeakPtr()));
// }

// void RagIngestionService::OnMessagesReceived(std::optional<base::Value> result) {
//   if (!result || !result->is_dict()) {
//     // Network error? Keep polling just in case, or implement backoff.
//     // For now, we keep the timer running to retry.
//     LOG(WARNING) << "[RAG] Heartbeat: Failed to fetch messages (Network/Parse Error).";
//     return;
//   }

//   const base::DictValue& root = result->GetDict();

//   // 1. PROTOCOL CHECK: "If isIngesting set to false then stop the timer"
//   std::optional<bool> is_ingesting = root.FindBool("isIngesting");
//   if (is_ingesting.has_value() && !is_ingesting.value()) {
//     LOG(INFO) << "[RAG] Heartbeat: Server says isIngesting=FALSE. Going to sleep.";
//     StopPolling();
//     return;
//   }

//   // 2. PROCESS MESSAGES
//   // JSON: { "messages": [ ... ], "isIngesting": true }
//   const base::ListValue* messages = root.FindList("messages");
//   if (messages) {
//     LOG(INFO) << "[RAG] Heartbeat: Processing " << messages->size() << " jobs.";
//     for (const auto& item : *messages) {
//       if (item.is_dict()) {
//         ProcessIngestionJob(item.GetDict());
//       }
//     }
//   }
// }

// // ===========================================================================
// // PHASE 3: EXECUTION
// // ===========================================================================

// void RagIngestionService::ProcessIngestionJob(const base::DictValue& message_item) {
//   // STRUCTURE from ingestion.ts 'IdentifiableIngestionMessage':
//   // {
//   //   "messageId": "...",
//   //   "createdAtMs": 123,
//   //   "target": { 
//   //      "url": "...",
//   //      "siteId": "..."
//   //   }
//   // }

//   const std::string* msg_id = message_item.FindString("messageId");
//   if (!msg_id) return;

//   // Traverse: target -> url
//   const base::DictValue* target_obj = message_item.FindDict("target");
//   if (!target_obj) return;

//   const std::string* url_str = target_obj->FindString("url");
//   if (!url_str) return;

//   // Execute Crawl
//   LOG(INFO) << "[RAG] JOB START: " << *url_str << " (ID: " << *msg_id << ")";
//   RunHiddenCrawl(GURL(*url_str), *msg_id);
// }

// ===========================================================================
// FLOW A: USER PERMISSION (DISK)
// ===========================================================================

RagIngestionService::UserPermission RagIngestionService::GetUserPermission(const GURL& url) {
  HostContentSettingsMap* map = HostContentSettingsMapFactory::GetForProfile(profile_);
  if (!map) return UserPermission::kUndecided;

  // Unwrap blob:/filesystem: URLs and strip paths to get the pure base domain
  // e.g., "blob:https://github.com/123" -> "https://github.com/"
  GURL origin_url = url::Origin::Create(url).GetURL();

  ContentSetting setting = map->GetContentSetting(origin_url, origin_url, ContentSettingsType::RAG_INGESTION);
  
  if (setting == CONTENT_SETTING_ALLOW) return UserPermission::kGranted;
  if (setting == CONTENT_SETTING_BLOCK) return UserPermission::kDenied;
  return UserPermission::kUndecided;
}

void RagIngestionService::SetUserPermission(const GURL& url, UserPermission status) {
  if (!url.is_valid()) return;

  ContentSetting setting = CONTENT_SETTING_DEFAULT;
  switch (status) {
    case UserPermission::kGranted: setting = CONTENT_SETTING_ALLOW; break;
    case UserPermission::kDenied: setting = CONTENT_SETTING_BLOCK; break;
    case UserPermission::kUndecided: setting = CONTENT_SETTING_DEFAULT; break;
  }

  if (HostContentSettingsMap* map = HostContentSettingsMapFactory::GetForProfile(profile_)) {
    // THIS automatically triggers OnContentSettingChanged() synchronously
    map->SetContentSettingDefaultScope(url, url, ContentSettingsType::RAG_INGESTION, setting);
  }
}

// 3. Add the safe-sync helper
void RagIngestionService::SyncLocalSettingFromBackend(const GURL& url, UserPermission status) {
    // Temporarily sets is_updating_from_backend_ to true for the duration of this scope
    base::AutoReset<bool> scope(&is_updating_from_backend_, true);
    SetUserPermission(url, status);
}

// 4. Implement the Observer (Fixes Bug 1 and Bug 2)
void RagIngestionService::OnContentSettingChanged(
    const ContentSettingsPattern& primary_pattern,
    const ContentSettingsPattern& secondary_pattern,
    ContentSettingsTypeSet content_type_set) {

  // Only care about our specific setting
  if (!content_type_set.Contains(ContentSettingsType::RAG_INGESTION)) return;

  // Ignore changes triggered by a backend sync (prevents infinite loop)
  if (is_updating_from_backend_) return;

  GURL url(primary_pattern.ToString());
  if (!url.is_valid()) return;

  UserPermission status = GetUserPermission(url);
  std::string active_tab_title;

  // Loop through all tabs to update UI AND grab title
  for (BrowserWindowInterface* bwi : GetAllBrowserWindowInterfaces()) {
    Browser* browser = bwi->GetBrowserForMigrationOnly();
    for (int i = 0; i < browser->tab_strip_model()->count(); ++i) {
      content::WebContents* wc = browser->tab_strip_model()->GetWebContentsAt(i);
      
      if (wc && wc->GetLastCommittedURL().DeprecatedGetOriginAsURL() == url.DeprecatedGetOriginAsURL()) {
        
        // Grab the title from the renderer (Solves SPA and Auth-Wall issues!)
        // We only need to grab it once, so check if it's empty
        if (active_tab_title.empty()) {
            active_tab_title = base::UTF16ToUTF8(wc->GetTitle());
        }

        // Update the Jatter Popup icon state
        if (auto* controller = RagIngestionPageActionController::FromWebContents(wc)) {
          if (status == UserPermission::kGranted) controller->ShowActiveState();
          else if (status == UserPermission::kDenied) controller->ShowDisabledState();
          else controller->ShowOfferState();
        }
      }
    }
  }

  // Sync to Backend immediately
  if (network_client_) {
    if (status == UserPermission::kGranted) {
      // Pass the safely extracted title down the chain!
      FetchRootPageMetadata(url.DeprecatedGetOriginAsURL(), url, active_tab_title);
    } else {
      RagPermissionStatus net_status = (status == UserPermission::kDenied) 
                                       ? RagPermissionStatus::kDenied 
                                       : RagPermissionStatus::kUndecided;
      FinalizePermissionGrant(url, net_status, {}); 
    }
  }
}

// void RagIngestionService::AddIngestionUrl(const GURL& url, base::OnceClosure on_success) {
//   if (network_client_) {
//     LOG(INFO) << "[RAG] Adding URL to Index: " << url.spec();
//     network_client_->AddIngestionUrl(url, std::move(on_success));
//   } else if (on_success) {
//     std::move(on_success).Run();
//   }
// }

// ===========================================================================
// FLOW B: BACKEND STATUS (NETWORK)
// ===========================================================================

void RagIngestionService::CheckBackendStatus(
    const GURL& url,
    base::OnceCallback<void(BackendPermissionInfo)> callback) {
  
  if (!network_client_) {
    std::move(callback).Run(BackendPermissionInfo()); // Returns default (kUnknown)
    return;
  }

  // Ask Network Client
  network_client_->CheckDomainPermission(
      url,
      base::BindOnce(&RagIngestionService::OnBackendResponse,
                     weak_factory_.GetWeakPtr(), 
                     std::move(callback)));
}

void RagIngestionService::OnBackendResponse(
    base::OnceCallback<void(BackendPermissionInfo)> client_callback,
    std::optional<base::Value> result) {
  
  BackendPermissionInfo info; 

  if (!result || !result->is_dict()) {
    // [REQ 8] Network or parsing error occurred
    info.is_error = true;
    LOG(ERROR) << "[RAG] getIngestionPermission API failed.";
    std::move(client_callback).Run(info);
    return;
  }

  const auto& dict = result->GetDict();
    
  // Parse Status
  const std::string* str = dict.FindString("permission");
  if (str) {
    if (*str == "allowed") info.status = BackendStatus::kKnownAllowed;
    else if (*str == "denied") info.status = BackendStatus::kKnownBlocked;
    else if (*str == "undecided") info.status = BackendStatus::kUnknown; 
  }

  // Parse specific UI keys
  if (const std::string* host = dict.FindString("canonicalHost")) info.canonical_host = *host;
  if (const std::string* name = dict.FindString("siteName")) info.site_name = *name;
    
  info.can_prompt = dict.FindBool("canPrompt").value_or(false);
    
  // 'lastSuccessAtMs' might be parsed as a double or int depending on size
  info.last_success_at_ms = dict.FindDouble("lastSuccessAtMs").value_or(0);
  info.is_learning = dict.FindBool("isLearning").value_or(false);

  std::optional<bool> requires_upgrade = dict.FindBool("requiresUpgrade");
  if (requires_upgrade.has_value()) {
      info.requires_upgrade = requires_upgrade.value();
  } else {
      info.requires_upgrade = false; // Safe default
  }
  
  LOG(INFO) << "[RAG] Backend Status Check: " << (int)info.status;
  std::move(client_callback).Run(info);
}

// // ===========================================================================
// // FLOW C: INGESTION TRIGGER
// // ===========================================================================

// void RagIngestionService::RunHiddenCrawl(const GURL& url, const std::string& message_id) {
//   // if (GetUserPermission(url) != UserPermission::kGranted) {
//   //   LOG(WARNING) << "Aborting background crawl: Permission revoked for " << url;
//   //   return;
//   // }

//   // Prevent duplicate runs if the server accidentally sends the same job twice
//   if (active_crawl_jobs_.find(message_id) != active_crawl_jobs_.end()) {
//       return; 
//   }

//   // Create the job and store it in the map using its message_id
//   active_crawl_jobs_[message_id] = std::make_unique<HiddenCrawlJob>(
//       profile_, 
//       url, 
//       base::BindOnce(&RagIngestionService::OnCrawlComplete, 
//                      weak_factory_.GetWeakPtr(),
//                      message_id));
                     
//   active_crawl_jobs_[message_id]->Start();
// }

// void RagIngestionService::OnCrawlComplete(std::string message_id, 
//                                           const std::string& html, 
//                                           const std::string& inner_text,
//                                           bool is_auth_wall) {
                                          
//   std::string status = is_auth_wall ? "ok" : "public";
            
//   LOG(INFO) << "[RAG] JOB COMPLETE: ID " << message_id 
//             << " | Status: " << status;
            
//   if (network_client_) {
//     network_client_->ResolveIngestionMessage(message_id, html, inner_text, is_auth_wall);
//   }

//   // CLEANUP: Erase the job from the map to destroy it and free memory
//   base::SingleThreadTaskRunner::GetCurrentDefault()->PostTask(
//       FROM_HERE,
//       base::BindOnce(
//           [](base::WeakPtr<RagIngestionService> service, std::string id) {
//             if (service) {
//               service->active_crawl_jobs_.erase(id);
//             }
//           },
//           weak_factory_.GetWeakPtr(), message_id));
// }

// ===========================================================================
// Metadata Extraction
// ===========================================================================

void RagIngestionService::FetchRootPageMetadata(const GURL& root_url, 
                                                const GURL& original_url,
                                                const std::string& active_tab_title) {
  auto resource_request = std::make_unique<network::ResourceRequest>();
  resource_request->url = root_url;
  resource_request->credentials_mode = network::mojom::CredentialsMode::kOmit;

  auto url_loader_factory = profile_->GetDefaultStoragePartition()
                                ->GetURLLoaderFactoryForBrowserProcess();

  auto loader = network::SimpleURLLoader::Create(std::move(resource_request), 
                                                 kMetadataTrafficAnnotation);
                                                 
  // Grab the raw pointer before moving ownership into the set
  network::SimpleURLLoader* loader_ptr = loader.get();
  metadata_loaders_.insert(std::move(loader));
  
  loader_ptr->DownloadToString(
    url_loader_factory.get(),
    base::BindOnce(&RagIngestionService::OnRootPageFetched,
                   weak_factory_.GetWeakPtr(),
                   loader_ptr, // Pass the pointer to the callback for cleanup
                   original_url,
                   active_tab_title),
    1024 * 1024);
}

void RagIngestionService::OnRootPageFetched(network::SimpleURLLoader* loader_ptr,
                                            const GURL& original_url,
                                            const std::string& active_tab_title, 
                                            std::optional<std::string> response_body) {
  RagSiteMetadata metadata;
  
  // 1. THE BROWSER IS THE SOURCE OF TRUTH. Use it first!
  metadata.title = active_tab_title;
  metadata.site_name = active_tab_title;

  std::string icon_url_str;
  std::string manifest_url_str;

  if (response_body) {
    std::string html = *response_body;

    // 2. Only scrape if the tab didn't have a good title
    if (metadata.title.empty()) {
      metadata.title = ExtractMetaTag(html, "og:title");
      if (metadata.title.empty()) {
        metadata.title = ExtractMetaTag(html, "title");
      }
      if (metadata.title.empty()) {
        metadata.title = ExtractBestSiteName(html, original_url.DeprecatedGetOriginAsURL());
      }
    }

    metadata.description = ExtractMetaTag(html, "og:description");
    if (metadata.description.empty()) metadata.description = ExtractMetaTag(html, "description");

    metadata.keywords = ExtractMetaTag(html, "keywords");

    icon_url_str = ExtractIconUrl(html, original_url.DeprecatedGetOriginAsURL());
    manifest_url_str = ExtractManifestUrl(html, original_url.DeprecatedGetOriginAsURL());
  } else {
    // 3. FALLBACK: No HTML. Guess the standard paths using the Origin.
    if (metadata.site_name.empty()) {
      metadata.site_name = original_url.host();
    }
    
    GURL origin = original_url.DeprecatedGetOriginAsURL();
    manifest_url_str = origin.Resolve("manifest.json").spec();
    icon_url_str = origin.Resolve("favicon.ico").spec();
  }

  // Clean up this root loader
  auto it = metadata_loaders_.find(loader_ptr);
  if (it != metadata_loaders_.end()) {
    metadata_loaders_.erase(it);
  }

  // 4. ROUTING: Always try to fetch Manifest -> Favicon -> Finalize
  // If a manifest exists (or we guessed one), fetch it to check Priority #1
  if (!manifest_url_str.empty()) {
    FetchManifest(original_url, manifest_url_str, icon_url_str, std::move(metadata));
    return; 
  } 
  // Otherwise, skip straight to the Favicon
  else if (!icon_url_str.empty()) {
    FetchFavicon(original_url, GURL(icon_url_str), std::move(metadata));
    return; 
  }

  // Failsafe if everything was somehow empty
  FinalizePermissionGrant(original_url, RagPermissionStatus::kAllowed, std::move(metadata));
}

void RagIngestionService::FetchFavicon(const GURL& original_url, 
                                       const GURL& icon_url, 
                                       RagSiteMetadata metadata) {
  favicon::FaviconService* favicon_service =
      FaviconServiceFactory::GetForProfile(profile_, ServiceAccessType::EXPLICIT_ACCESS);

  if (!favicon_service) {
      // Fallback if the service is unavailable (e.g., Incognito in some forks)
      FinalizePermissionGrant(original_url, RagPermissionStatus::kAllowed, std::move(metadata));
      return;
  }

  // Ask Chromium for the largest available icon it has cached for this page
  favicon_service->GetFaviconImageForPageURL(
      original_url,
      base::BindOnce(&RagIngestionService::OnNativeFaviconFetched,
                     weak_factory_.GetWeakPtr(), 
                     original_url, 
                     std::move(metadata)),
      &favicon_task_tracker_);
}

void RagIngestionService::OnNativeFaviconFetched(const GURL& original_url,
                                                 RagSiteMetadata metadata,
                                                 const favicon_base::FaviconImageResult& result) {
  if (!result.image.IsEmpty()) {
      // Chromium hands us a decoded gfx::Image. We encode it to PNG for your backend.
      scoped_refptr<base::RefCountedMemory> png_bytes = result.image.As1xPNGBytes();
      
      if (png_bytes && png_bytes->size() > 0) {
          metadata.icon_base64 = base::Base64Encode(*png_bytes);
          metadata.icon_mime_type = "image/png";
      }
  } else {
      LOG(WARNING) << "[RAG] FaviconService had no cached icon for " << original_url.spec();
      // Optional: If you want an absolute fallback, you could trigger your old 
      // network::SimpleURLLoader here pointing to root_url.Resolve("/favicon.ico")
  }

  // Done!
  FinalizePermissionGrant(original_url, RagPermissionStatus::kAllowed, std::move(metadata));
}

// void RagIngestionService::FetchFavicon(const GURL& original_url, 
//                                        const GURL& icon_url, 
//                                        RagSiteMetadata metadata) {
//   auto resource_request = std::make_unique<network::ResourceRequest>();
//   resource_request->url = icon_url;
//   resource_request->credentials_mode = network::mojom::CredentialsMode::kOmit;

//   auto url_loader_factory = profile_->GetDefaultStoragePartition()
//                                 ->GetURLLoaderFactoryForBrowserProcess();

//   auto loader = network::SimpleURLLoader::Create(std::move(resource_request), 
//                                                  kMetadataTrafficAnnotation);
                                                 
//   network::SimpleURLLoader* loader_ptr = loader.get();
//   metadata_loaders_.insert(std::move(loader));
  
//   loader_ptr->DownloadToString(
//     url_loader_factory.get(),
//     base::BindOnce(&RagIngestionService::OnFaviconFetched,
//                    weak_factory_.GetWeakPtr(),
//                    loader_ptr, 
//                    original_url, 
//                    icon_url,
//                    std::move(metadata)),
//     1024 * 1024);
// }

void RagIngestionService::OnFaviconFetched(network::SimpleURLLoader* loader_ptr,
                                           const GURL& original_url, 
                                           const GURL& icon_url,
                                           RagSiteMetadata metadata, 
                                           std::optional<std::string> response_body) {
  
  std::string network_mime_type;
  if (loader_ptr && loader_ptr->ResponseInfo()) {
    network_mime_type = loader_ptr->ResponseInfo()->mime_type;
  }

  // --- Start MIME Type Derivation ---
  std::string final_mime_type = "image/x-icon"; // Default (Rule 4)
  std::string filename = base::ToLowerASCII(icon_url.path());

  if (!network_mime_type.empty() && network_mime_type != "application/octet-stream") {
      final_mime_type = network_mime_type;
  } else if (base::EndsWith(filename, ".png", base::CompareCase::SENSITIVE)) {
      final_mime_type = "image/png"; // Rule 3
  } else if (base::EndsWith(filename, ".jpg", base::CompareCase::SENSITIVE) || 
             base::EndsWith(filename, ".jpeg", base::CompareCase::SENSITIVE)) {
      final_mime_type = "image/jpeg"; // Rule 3
  } else if (base::EndsWith(filename, ".svg", base::CompareCase::SENSITIVE)) {
      final_mime_type = "image/svg+xml"; 
  }

  // Rule 1: "If it's an ICO then use the type image/x-icon"
  // Catch alternate server-provided ICO types or explicit extensions
  if (final_mime_type == "image/vnd.microsoft.icon" || 
      final_mime_type == "image/ico" ||
      base::EndsWith(filename, ".ico", base::CompareCase::SENSITIVE)) {
      final_mime_type = "image/x-icon";
  }
  // --- End MIME Type Derivation ---

  // Check if it actually resembles an image
  bool is_valid_image = (final_mime_type.find("image/") != std::string::npos);

  if (is_valid_image && response_body) {
    std::string base64_icon = base::Base64Encode(*response_body);
    if (base64_icon.length() <= 135000) {
      metadata.icon_base64 = std::move(base64_icon);
      metadata.icon_mime_type = final_mime_type;
    }
  } else {
    LOG(WARNING) << "[RAG] Favicon fetch returned HTML or invalid format. Skipping icon.";
  }
  
  FinalizePermissionGrant(original_url, RagPermissionStatus::kAllowed, std::move(metadata));
  
  // Clean up the loader
  auto it = metadata_loaders_.find(loader_ptr);
  if (it != metadata_loaders_.end()) metadata_loaders_.erase(it);
}

void RagIngestionService::FetchManifest(const GURL& original_url, 
                                        const std::string& manifest_url, 
                                        const std::string& icon_url, 
                                        RagSiteMetadata metadata) {
  auto request = std::make_unique<network::ResourceRequest>();
  request->url = GURL(manifest_url);
  request->credentials_mode = network::mojom::CredentialsMode::kOmit;

  auto url_loader_factory = profile_->GetDefaultStoragePartition()
                                ->GetURLLoaderFactoryForBrowserProcess();

  auto loader = network::SimpleURLLoader::Create(std::move(request), kMetadataTrafficAnnotation);
  network::SimpleURLLoader* loader_ptr = loader.get();
  metadata_loaders_.insert(std::move(loader));
  
  loader_ptr->DownloadToString(
    url_loader_factory.get(),
    base::BindOnce(&RagIngestionService::OnManifestFetched,
                   weak_factory_.GetWeakPtr(),
                   loader_ptr, original_url, icon_url, std::move(metadata)),
    512 * 1024); // 512KB max for a manifest JSON
}

void RagIngestionService::OnManifestFetched(network::SimpleURLLoader* loader_ptr,
                                            const GURL& original_url, 
                                            const std::string& icon_url,
                                            RagSiteMetadata metadata, 
                                            std::optional<std::string> response_body) {
  // Priority 1: name or short_name from link[rel="manifest"]
  if (response_body) {
    std::optional<base::Value> parsed = base::JSONReader::Read(*response_body, 
        base::JSON_PARSE_CHROMIUM_EXTENSIONS);

    if (parsed && parsed->is_dict()) {
      const auto& dict = parsed->GetDict();
      const std::string* name = dict.FindString("name");
      const std::string* short_name = dict.FindString("short_name");
      
      // If we find them, they override the HTML fallbacks
      if (name && !name->empty()) {
        metadata.site_name = *name;
      } else if (short_name && !short_name->empty()) {
        metadata.site_name = *short_name;
      }

      if (icon_url.empty()) {
        const base::ListValue* icons = dict.FindList("icons");
        if (icons && !icons->empty() && icons->front().is_dict()) {
          const std::string* src = icons->front().GetDict().FindString("src");
          if (src) {
            // Resolve the manifest icon relative to the manifest's URL
            FetchFavicon(original_url, original_url.Resolve(*src), std::move(metadata));
            return; // Exit early so we don't hit the bottom fallback
          }
        }
      }
    }
  }

  // Clean up the manifest loader
  auto it = metadata_loaders_.find(loader_ptr);
  if (it != metadata_loaders_.end()) metadata_loaders_.erase(it);

  // Resume the chain
  if (!icon_url.empty()) {
    FetchFavicon(original_url, GURL(icon_url), std::move(metadata));
  } else {
    FinalizePermissionGrant(original_url, RagPermissionStatus::kAllowed, std::move(metadata));
  }
}

void RagIngestionService::FinalizePermissionGrant(const GURL& url, 
                                                  RagPermissionStatus status, 
                                                  const RagSiteMetadata& metadata) {
  if (network_client_) {
    network_client_->SetIngestionPermission(url, status, metadata);
  }
}

// --- Parsing Helpers (Simple String Search) ---

std::string RagIngestionService::ExtractBestSiteName(const std::string& html, const GURL& root_url) {
  std::string site_name;

  // Priority 2: <meta property="og:site_name" content="The Rock" />
  site_name = ExtractMetaTag(html, "og:site_name");
  if (!site_name.empty()) return site_name;

  // Priority 3: meta[name="apple-mobile-web-app-title"]
  site_name = ExtractMetaTag(html, "apple-mobile-web-app-title");
  if (!site_name.empty()) return site_name;

  // Priority 4: meta[name="application-name"]
  site_name = ExtractMetaTag(html, "application-name");
  if (!site_name.empty()) return site_name;

  // Priority 5: <meta property="og:title" content="The Rock" />
  site_name = ExtractMetaTag(html, "og:title");
  if (!site_name.empty()) return site_name;

  // Priority 6: <title>The Rock (1996)</title>
  std::string raw_title;
  if (re2::RE2::PartialMatch(html, "(?is)<title[^>]*>(.*?)</title>", &raw_title)) {
      // Clean up HTML entities (like &amp;) and trim whitespace
      raw_title = base::UTF16ToUTF8(base::UnescapeForHTML(base::UTF8ToUTF16(raw_title)));
      base::TrimWhitespaceASCII(raw_title, base::TRIM_ALL, &raw_title);
      
      if (!raw_title.empty()) return raw_title;
  }

  // Priority 7: meta[name="title"]
  site_name = ExtractMetaTag(html, "title"); 
  if (!site_name.empty()) return site_name;

  // Priority 8: Fallback to host
  return std::string(root_url.host());
}

std::string RagIngestionService::ExtractManifestUrl(const std::string& html, const GURL& root_url) {
  size_t rel_pos = html.find("rel=\"manifest\"");
  if (rel_pos != std::string::npos) {
    size_t link_start = html.rfind("<link", rel_pos);
    if (link_start != std::string::npos) {
      size_t href_pos = html.find("href=\"", link_start);
      if (href_pos != std::string::npos && href_pos < html.find(">", link_start)) {
        href_pos += 6; // Skip past href="
        size_t href_end = html.find("\"", href_pos);
        if (href_end != std::string::npos) {
          std::string href_val = html.substr(href_pos, href_end - href_pos);
          return root_url.Resolve(href_val).spec();
        }
      }
    }
  }
  return "";
}

std::string RagIngestionService::ExtractMetaTag(const std::string& html, const std::string& key) {
  std::string value;
  
  // Matches <meta name="key" content="value"> AND <meta property="key" content="value">
  std::string pattern1 = "(?i)<meta[^>]+(?:name|property)\\s*=\\s*['\"]" + key + "['\"][^>]+content\\s*=\\s*['\"]([^'\"]+)['\"]";
  if (re2::RE2::PartialMatch(html, pattern1, &value)) {
      return base::UTF16ToUTF8(base::UnescapeForHTML(base::UTF8ToUTF16(value)));
  }

  // Matches <meta content="value" name="key"> (Flipped order)
  std::string pattern2 = "(?i)<meta[^>]+content\\s*=\\s*['\"]([^'\"]+)['\"][^>]+(?:name|property)\\s*=\\s*['\"]" + key + "['\"]";
  if (re2::RE2::PartialMatch(html, pattern2, &value)) {
      return base::UTF16ToUTF8(base::UnescapeForHTML(base::UTF8ToUTF16(value)));
  }

  return "";
}

std::string RagIngestionService::ExtractIconUrl(const std::string& html, const GURL& root_url) {
  std::string href_val;

  // Pattern 1: The 'rel' attribute appears BEFORE the 'href' attribute
  // Matches: <link rel="icon" href="/icon.ico"> or <link rel='shortcut icon' href='foo.png'>
  if (re2::RE2::PartialMatch(html, 
          "(?i)<link[^>]*rel\\s*=\\s*['\"](?:shortcut\\s+)?icon['\"][^>]*href\\s*=\\s*['\"]([^'\"]+)['\"]", 
          &href_val)) {
      return root_url.Resolve(href_val).spec();
  }

  // Pattern 2: The 'href' attribute appears BEFORE the 'rel' attribute
  // Matches: <link href="/icon.ico" type="image/png" rel="icon">
  if (re2::RE2::PartialMatch(html, 
          "(?i)<link[^>]*href\\s*=\\s*['\"]([^'\"]+)['\"][^>]*rel\\s*=\\s*['\"](?:shortcut\\s+)?icon['\"]", 
          &href_val)) {
      return root_url.Resolve(href_val).spec();
  }

  // Fallback: Guessing the standard location at the root of the domain
  return root_url.Resolve("/favicon.ico").spec();
}

// ===========================================================================
// FLOW C: PASSIVE LEARNING PIPELINE
// ===========================================================================

void RagIngestionService::SetPrivateKey(const std::string& private_key_base64) {
  if (!private_key_base64.empty()) {
    LOG(INFO) << "[RAG] Private Key set.";
  }
  private_key_base64_ = private_key_base64;
}

std::string RagIngestionService::FilterMhtmlToTextOnly(const std::string& mhtml) {
  if (mhtml.empty()) return mhtml;

  // Use string_view for zero-copy parsing of the massive input string
  std::string_view mhtml_view(mhtml);

  // 1. Extract the multipart boundary marker from the global headers
  std::string boundary_marker = "boundary=\"";
  size_t boundary_start = mhtml_view.find(boundary_marker);
  std::string_view boundary;

  if (boundary_start != std::string_view::npos) {
    boundary_start += boundary_marker.length();
    size_t boundary_end = mhtml_view.find("\"", boundary_start);
    if (boundary_end != std::string_view::npos) {
      boundary = mhtml_view.substr(boundary_start, boundary_end - boundary_start);
    }
  } else {
    // Fallback: Sometimes boundaries aren't wrapped in quotes
    boundary_marker = "boundary=";
    boundary_start = mhtml_view.find(boundary_marker);
    if (boundary_start != std::string_view::npos) {
      boundary_start += boundary_marker.length();
      size_t boundary_end = mhtml_view.find("\r\n", boundary_start);
      if (boundary_end != std::string_view::npos) {
        boundary = mhtml_view.substr(boundary_start, boundary_end - boundary_start);
      }
    }
  }

  if (boundary.empty()) {
    LOG(WARNING) << "[RAG] Failed to parse MHTML boundary. Sending full file.";
    return mhtml;
  }

  std::string part_separator = "--" + std::string(boundary);
  
  // Pre-allocate memory for the filtered string to prevent reallocations.
  // We don't know the exact size, but reserving a fraction of the original is safe.
  std::string filtered_mhtml;
  filtered_mhtml.reserve(mhtml.size() / 4); 

  size_t pos = 0;
  size_t next_pos = mhtml_view.find(part_separator, pos);

  if (next_pos == std::string_view::npos) {
    return mhtml; // Malformed MHTML, bail out
  }

  // 2. Preserve the global MIME headers
  filtered_mhtml += mhtml_view.substr(0, next_pos);

  // 3. Iterate through every MIME part safely using string_view
  while (next_pos != std::string_view::npos) {
    pos = next_pos + part_separator.length();

    if (pos < mhtml_view.length() && mhtml_view[pos] == '-' && mhtml_view[pos + 1] == '-') {
      break; 
    }

    next_pos = mhtml_view.find(part_separator, pos);
    size_t part_end = (next_pos != std::string_view::npos) ? next_pos : mhtml_view.length();
    
    // ZERO COPY: We just create a window looking at the original string
    std::string_view part = mhtml_view.substr(pos, part_end - pos);

    size_t headers_end = part.find("\r\n\r\n");
    if (headers_end == std::string_view::npos) {
        headers_end = part.find("\n\n"); 
    }
    
    if (headers_end != std::string_view::npos) {
      std::string_view headers = part.substr(0, headers_end);
      
      // Convert only the small headers to a lower-case std::string for the search, 
      // rather than the entire multi-megabyte base64 payload.
      std::string headers_lower = base::ToLowerASCII(headers);

      // Spec Requirement: Filter out any part that is NOT text/html
      if (headers_lower.find("content-type: text/html") != std::string::npos) {
        // ONLY NOW do we copy the bytes into our new string
        filtered_mhtml += part_separator;
        filtered_mhtml += part;
      }
    }
  }

  // 4. Properly seal the MIME structure
  filtered_mhtml += part_separator + "--\r\n";
  
  // Re-shrink the capacity of the string to free up unused reserved memory
  filtered_mhtml.shrink_to_fit();

  LOG(INFO) << "[RAG] MHTML filtered from " << mhtml.size() 
            << " bytes to " << filtered_mhtml.size() << " bytes.";
            
  return filtered_mhtml;
}

void RagIngestionService::StartDocumentIngestion(const GURL& url,
                                                 const std::string& page_title,
                                                 const std::string& file_bytes, 
                                                 const std::string& mime_type,
                                                 const std::string& filename) {
  // 1. HARD CEILING: Prevent OOM crashes on massive MHTML blobs (e.g., > 50MB)
  // Even before filtering, we shouldn't regex/split infinitely large strings.
  constexpr size_t kMaxProcessableBytes = 50 * 1024 * 1024; 
  if (file_bytes.size() > kMaxProcessableBytes) {
    LOG(WARNING) << "[RAG] File catastrophically large (" << file_bytes.size() 
                 << " bytes). Skipping to prevent memory exhaustion.";
    return;
  }

  // 2. FILTERING
  std::string filtered_bytes = file_bytes;
  if (mime_type == "multipart/related") {
    filtered_bytes = FilterMhtmlToTextOnly(file_bytes); 
  }

  // 3. SPEC REQUIREMENT: 10MB Limit on the final payload
  if (filtered_bytes.size() > 10 * 1024 * 1024) {
    LOG(WARNING) << "[RAG] Filtered text still too large (" << filtered_bytes.size() 
                 << " bytes). Skipping upload.";
    return;
  }

  if (!network_client_) return;

  // Step 4: Upload to ingestDocument
  network_client_->UploadRawDocument(
      url, page_title, filtered_bytes, mime_type, filename,
      base::BindOnce(&RagIngestionService::OnDocumentParsed,
                     weak_factory_.GetWeakPtr(), url));
}

void RagIngestionService::OnDocumentParsed(const GURL& url, std::optional<base::Value> result) {
  if (!result || !result->is_dict()) return;

  base::DictValue& document_text = result->GetDict();
  base::ListValue* text_records = document_text.FindList("textRecords");
  if (!text_records) return;

  // Iterate through records and encrypt the "text" field
  for (auto& item : *text_records) {
    if (item.is_dict()) {
      base::DictValue& record = item.GetDict();
      const std::string* clear_text = record.FindString("text");
      
      if (clear_text) {
        // Use your existing BoringSSL AES-CTR logic here!
        std::string encrypted_text = EncryptSingleString(*clear_text); 
        record.Set("text", encrypted_text); // Replace clear text with encrypted
      }
    }
  }

  // Step 2: Upload the encrypted JSON back to ingestEncryptedDocumentText
  network_client_->IngestEncryptedDocument(url, std::move(document_text), base::DoNothing());
}

void RagIngestionService::StartPassiveLearningPipeline(const GURL& url, 
                                                       const std::string& inner_text) {
  if (!network_client_) return;
  if (private_key_base64_.empty()) {
    LOG(WARNING) << "[RAG] Cannot start passive learning: Private key is missing.";
    return;
  }

  LOG(INFO) << "[RAG] Starting Passive Learning for: " << url.spec();

  // Step 1: Send text to backend to be chunked
  network_client_->ChunkDocument(
      url,
      inner_text,
      base::BindOnce(&RagIngestionService::OnDocumentChunked,
                     weak_factory_.GetWeakPtr(), 
                     url));
}

void RagIngestionService::OnDocumentChunked(const GURL& url, std::optional<base::Value> result) {
  if (!result || !result->is_dict()) {
    LOG(ERROR) << "[RAG] chunkDocument API failed.";
    return;
  }

  const base::ListValue* chunks_list = result->GetDict().FindList("chunks");
  if (!chunks_list || chunks_list->empty()) {
    LOG(WARNING) << "[RAG] chunkDocument returned no chunks.";
    return;
  }

  // Extract chunks
  std::vector<std::string> chunks;
  for (const auto& item : *chunks_list) {
    if (item.is_string()) {
      chunks.push_back(item.GetString());
    }
  }

  // Step 2: Send chunks back to get embeddings (vectors)
  network_client_->EmbedChunks(
      chunks,
      base::BindOnce(&RagIngestionService::OnChunksEmbedded,
                     weak_factory_.GetWeakPtr(), 
                     url, 
                     chunks)); // Pass the chunks along to the next step!
}

void RagIngestionService::OnChunksEmbedded(const GURL& url, 
                                           std::vector<std::string> chunks, 
                                           std::optional<base::Value> result) {
  if (!result || !result->is_dict()) {
    LOG(ERROR) << "[RAG] embedChunks API failed.";
    return;
  }

  const base::ListValue* vectors_list = result->GetDict().FindList("vectors");
  if (!vectors_list || vectors_list->size() != chunks.size()) {
    LOG(ERROR) << "[RAG] embedChunks returned mismatched or empty vectors.";
    return;
  }

  // Extract vectors
  std::vector<std::vector<double>> vectors;
  for (const auto& item : *vectors_list) {
    std::vector<double> single_vector;
    if (item.is_list()) {
      for (const auto& num : item.GetList()) {
        if (num.is_double() || num.is_int()) {
          single_vector.push_back(num.GetDouble());
        }
      }
    }
    vectors.push_back(std::move(single_vector));
  }

  // Step 3: Encrypt the chunks locally
  std::vector<std::string> encrypted_chunks = EncryptChunks(chunks);
  if (encrypted_chunks.empty()) {
     LOG(ERROR) << "[RAG] Encryption failed. Aborting ingestion.";
     return;
  }

  // Step 4: Final Ingestion
  network_client_->IngestDocument(
      url, 
      encrypted_chunks, 
      vectors,
      base::BindOnce([]() {
        LOG(INFO) << "[RAG] Passive Learning pipeline completed successfully.";
      }));
}

// --- Local Encryption Helper ---
std::vector<std::string> RagIngestionService::EncryptChunks(
    const std::vector<std::string>& chunks) {
  
  std::vector<std::string> encrypted_chunks;

  // 1. Decode the Base64 Private Key
  std::string raw_key;
  if (!base::Base64Decode(private_key_base64_, &raw_key)) {
    LOG(ERROR) << "[RAG] Failed to decode base64 private key.";
    return encrypted_chunks;
  }

  // Your Dart code uses a 32-byte (256-bit) derived key
  if (raw_key.size() != 32) { 
    LOG(ERROR) << "[RAG] Invalid key length. Expected 32 bytes, got " << raw_key.size();
    return encrypted_chunks;
  }

  // 2. Encrypt each chunk using BoringSSL
  for (const std::string& chunk : chunks) {
    
    // Generate a 16-byte random IV to match Dart's IV.fromLength(16)
    std::vector<uint8_t> iv_bytes(16);
    crypto::RandBytes(iv_bytes);
    std::string iv(iv_bytes.begin(), iv_bytes.end());

    // Dart's package:encrypt defaults to AESMode.sic (CTR mode) WITH PKCS7 padding.
    // Because CTR is a stream cipher, BoringSSL doesn't pad it automatically, 
    // so we apply the PKCS7 padding manually before encryption.
    size_t block_size = 16;
    uint8_t pad_val = block_size - (chunk.length() % block_size);
    std::string padded_chunk = chunk;
    padded_chunk.append(pad_val, static_cast<char>(pad_val));

    // Initialize BoringSSL EVP Context for AES-256-CTR
    bssl::ScopedEVP_CIPHER_CTX ctx;

    if (!EVP_EncryptInit_ex(ctx.get(), EVP_aes_256_ctr(), nullptr, 
                            reinterpret_cast<const uint8_t*>(raw_key.data()), 
                            reinterpret_cast<const uint8_t*>(iv.data()))) {
        LOG(ERROR) << "[RAG] Failed to initialize AES-CTR encryption.";
        return std::vector<std::string>(); // Memory cleans up automatically!
    }

    if (!ctx.get()) {
        LOG(ERROR) << "[RAG] Failed to create BoringSSL context.";
        return std::vector<std::string>(); // Return empty on failure
    }

    // Allocate output buffer (safely sized for the padded input)
    std::vector<uint8_t> ciphertext(padded_chunk.size() + block_size);
    int out_len1 = 0;
    int out_len2 = 0;

    // Encrypt the payload
    EVP_EncryptUpdate(ctx.get(), ciphertext.data(), &out_len1,
                      reinterpret_cast<const uint8_t*>(padded_chunk.data()),
                      padded_chunk.size());
    EVP_EncryptFinal_ex(ctx.get(), 
                        base::span(ciphertext).subspan(base::checked_cast<size_t>(out_len1)).data(), 
                        &out_len2);

    // Slice the exact written length to avoid trailing null bytes
    std::string cipher_str(ciphertext.begin(), ciphertext.begin() + out_len1 + out_len2);

    // Format exactly as Dart expects: "Base64(IV):Base64(Ciphertext)"
    std::string combined = base::Base64Encode(iv) + ":" + base::Base64Encode(cipher_str);
    encrypted_chunks.push_back(combined);
  }

  return encrypted_chunks;
}

std::string RagIngestionService::EncryptSingleString(const std::string& clear_text) {
  if (clear_text.empty()) {
    return "";
  }

  // 1. Decode the Base64 Private Key
  std::string raw_key;
  if (!base::Base64Decode(private_key_base64_, &raw_key)) {
    LOG(ERROR) << "[RAG] Failed to decode base64 private key.";
    return "";
  }

  // The Dart backend uses a 32-byte (256-bit) derived key
  if (raw_key.size() != 32) { 
    LOG(ERROR) << "[RAG] Invalid key length. Expected 32 bytes, got " << raw_key.size();
    return "";
  }

  // 2. Generate a 16-byte random IV to match Dart's IV.fromLength(16)
  std::vector<uint8_t> iv_bytes(16);
  crypto::RandBytes(iv_bytes);
  std::string iv(iv_bytes.begin(), iv_bytes.end());

  // 3. Apply PKCS7 Padding
  // Dart's package:encrypt defaults to AESMode.sic (CTR mode) WITH PKCS7 padding.
  // Because CTR is a stream cipher, BoringSSL doesn't pad it automatically, 
  // so we apply the PKCS7 padding manually before encryption.
  size_t block_size = 16;
  uint8_t pad_val = block_size - (clear_text.length() % block_size);
  std::string padded_text = clear_text;
  padded_text.append(pad_val, static_cast<char>(pad_val));

  // 4. Initialize BoringSSL EVP Context for AES-256-CTR
  bssl::ScopedEVP_CIPHER_CTX ctx;
  if (!EVP_EncryptInit_ex(ctx.get(), EVP_aes_256_ctr(), nullptr, 
                          reinterpret_cast<const uint8_t*>(raw_key.data()), 
                          reinterpret_cast<const uint8_t*>(iv.data()))) {
    LOG(ERROR) << "[RAG] Failed to initialize AES-CTR encryption.";
    return ""; 
  }

  if (!ctx.get()) {
    LOG(ERROR) << "[RAG] Failed to create BoringSSL context.";
    return "";
  }

  // 5. Allocate output buffer (safely sized for the padded input)
  std::vector<uint8_t> ciphertext(padded_text.size() + block_size);
  int out_len1 = 0;
  int out_len2 = 0;

  // 6. Encrypt the payload
  EVP_EncryptUpdate(ctx.get(), ciphertext.data(), &out_len1,
                    reinterpret_cast<const uint8_t*>(padded_text.data()),
                    padded_text.size());
                    
  EVP_EncryptFinal_ex(ctx.get(), 
                      base::span(ciphertext).subspan(base::checked_cast<size_t>(out_len1)).data(), 
                      &out_len2);

  // Slice the exact written length to avoid trailing null bytes
  std::string cipher_str(ciphertext.begin(), ciphertext.begin() + out_len1 + out_len2);

  // 7. Format exactly as Dart expects: "Base64(IV):Base64(Ciphertext)"
  return base::Base64Encode(iv) + ":" + base::Base64Encode(cipher_str);
}

// ===========================================================================
// DOCUMENT INTERCEPTION & EXTRACTION
// ===========================================================================

void RagIngestionService::OnDownloadCreated(content::DownloadManager* manager,
                                            download::DownloadItem* item) {
  // Ignore historical downloads loaded from the database at startup
  if (!manager->IsManagerInitialized() && 
      (item->GetState() == download::DownloadItem::COMPLETE || 
       item->GetState() == download::DownloadItem::CANCELLED ||
       item->GetState() == download::DownloadItem::INTERRUPTED)) {
      return; 
  }

  // Google Drive and other complex web apps often use 'application/octet-stream' 
  // and delay filename resolution. We cannot judge the file type accurately here.
  // ALWAYS observe the item, and make the decision when the download completes.
  item->AddObserver(this);
  
  // Race Condition Check: Was it so fast that it's already done?
  if (item->GetState() == download::DownloadItem::COMPLETE) {
      OnDownloadUpdated(item); 
  }
}

void RagIngestionService::OnDownloadDestroyed(download::DownloadItem* item) {
  item->RemoveObserver(this);
}

void RagIngestionService::OnDownloadUpdated(download::DownloadItem* item) {
  if (item->GetState() == download::DownloadItem::IN_PROGRESS) {
      return; 
  }

  if (item->GetState() == download::DownloadItem::COMPLETE) {
      item->RemoveObserver(this); // Unhook immediately

      std::string mime_type = GetIngestionMimeType(item);

      if (!mime_type.empty()) {
          
          // [CROSS-ORIGIN FIX] Traverse the origin chain to find the authorized domain
          GURL granted_origin;
          std::vector<GURL> candidate_urls = {
              item->GetTabUrl(),          // The URL of the tab
              item->GetTabReferrerUrl(),  // The page that opened the tab
              item->GetReferrerUrl(),     // The HTTP Referrer of the download request
              item->GetOriginalUrl()      // The first URL in the redirect chain
          };

          for (const GURL& candidate : candidate_urls) {
              if (candidate.is_valid() && !candidate.is_empty()) {
                  GURL origin = url::Origin::Create(candidate).GetURL();
                  // If ANY hop in the chain is authorized, we accept the download!
                  if (GetUserPermission(origin) == UserPermission::kGranted) {
                      granted_origin = origin;
                      break;
                  }
              }
          }

          if (!granted_origin.is_empty()) {
              LOG(INFO) << "[RAG] Intercepted Download (" << mime_type << ") linked to granted site: " << granted_origin.spec();
              
              std::string filename = item->GetTargetFilePath().BaseName().AsUTF8Unsafe();
              if (filename.empty()) filename = "downloaded_document";

              base::ThreadPool::PostTaskAndReplyWithResult(
                  FROM_HERE, {base::MayBlock(), base::TaskPriority::BEST_EFFORT},
                  base::BindOnce([](const base::FilePath& path) {
                      std::string file_content;
                      base::ReadFileToString(path, &file_content);
                      return file_content;
                  }, item->GetTargetFilePath()),
                  
                  base::BindOnce(
                      [](base::WeakPtr<RagIngestionService> svc, GURL url, std::string mime, std::string fname, std::string bytes) {
                          if (svc && !bytes.empty()) {
                              svc->StartDocumentIngestion(url, fname, bytes, mime, fname);
                          }
                      }, weak_factory_.GetWeakPtr(), granted_origin, mime_type, filename) // Pass the original granted origin to the backend!
              );
          } else {
              LOG(INFO) << "[RAG] Ignored downloaded document: No authorized domain found in the download chain.";
          }
      } else {
          VLOG(1) << "[RAG] Ignored completed download: Unsupported format.";
      }
  } 
  else if (item->GetState() == download::DownloadItem::CANCELLED || 
           item->GetState() == download::DownloadItem::INTERRUPTED) {
      item->RemoveObserver(this);
  }
}

void RagIngestionService::ExtractAndIngestPdf(const GURL& url, const std::vector<uint8_t>& pdf_bytes) {
  if (pdf_bytes.empty()) return;

  // Updated namespace to rag_ingestion::mojom::PdfTextExtractor
  mojo::Remote<rag_ingestion::mojom::PdfTextExtractor> pdf_extractor =
      content::ServiceProcessHost::Launch<rag_ingestion::mojom::PdfTextExtractor>(
          content::ServiceProcessHost::Options()
              .WithDisplayName("RAG Ingestion PDF Extractor") // Updated display name
              .Pass());

  auto* raw_extractor = pdf_extractor.get();
  raw_extractor->ExtractText(
      pdf_bytes,
      base::BindOnce(
          [](base::WeakPtr<RagIngestionService> service, 
             GURL original_url,
             mojo::Remote<rag_ingestion::mojom::PdfTextExtractor> keep_alive, 
             const std::string& extracted_text) {
              
              if (!service) return;

              if (extracted_text.empty()) {
                  LOG(INFO) << "[RAG] Ignored PDF: Image-only or parsing failed.";
                  return;
              }

              LOG(INFO) << "[RAG] PDF Text extracted successfully. Sending to passive learning pipeline.";
              service->StartPassiveLearningPipeline(original_url, extracted_text);
              
          }, weak_factory_.GetWeakPtr(), url, std::move(pdf_extractor))
  );
}
