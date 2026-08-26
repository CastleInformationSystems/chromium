// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#import "ios/chrome/browser/jatter/rag_ingestion_service.h"

#import <string_view>

#import "base/auto_reset.h"
#import "base/base64.h"
#import "base/containers/span.h"
#import "base/files/file_util.h"
#import "base/functional/callback_helpers.h"
#import "base/json/json_reader.h"
#import "base/logging.h"
#import "base/numerics/safe_conversions.h"
#import "base/strings/escape.h"
#import "base/strings/string_util.h"
#import "base/strings/utf_string_conversions.h"
#import "base/task/single_thread_task_runner.h"
#import "base/task/thread_pool.h"
#import "components/content_settings/core/browser/host_content_settings_map.h"
#import "components/content_settings/core/common/content_settings_types.h"
#import "components/favicon/core/favicon_service.h"
#import "components/keyed_service/core/service_access_type.h"
#import "crypto/random.h"
#import "ios/chrome/browser/content_settings/model/host_content_settings_map_factory.h"
#import "ios/chrome/browser/favicon/model/favicon_service_factory.h"
#import "ios/chrome/browser/shared/model/browser/browser.h"
#import "ios/chrome/browser/shared/model/browser/browser_list.h"
#import "ios/chrome/browser/shared/model/browser/browser_list_factory.h"
#import "ios/chrome/browser/shared/model/profile/profile_ios.h"
#import "ios/chrome/browser/shared/model/web_state_list/web_state_list.h"
#import "ios/web/public/web_state.h"
#import "net/traffic_annotation/network_traffic_annotation.h"
#import "services/network/public/cpp/resource_request.h"
#import "services/network/public/cpp/shared_url_loader_factory.h"
#import "services/network/public/mojom/url_response_head.mojom.h"
#import "third_party/boringssl/src/include/openssl/cipher.h"
#import "third_party/boringssl/src/include/openssl/evp.h"
#import "third_party/re2/src/re2/re2.h"
#import "ui/gfx/codec/png_codec.h"
#import "ui/gfx/image/image_skia.h"

constexpr net::NetworkTrafficAnnotationTag kMetadataTrafficAnnotation =
    net::DefineNetworkTrafficAnnotation("rag_ingestion_metadata_ios", R"(
        semantics {
          sender: "RAG Ingestion Service iOS"
          description: "Fetches website root page and favicon to extract visual metadata (title, icon) when the user grants ingestion permission."
          trigger: "User clicks 'Allow' on the RAG Ingestion permission prompt."
          data: "None (Anonymous request)."
          destination: WEBSITE
        }
        policy {
          cookies_allowed: NO
          setting: "This feature is strictly opt-in."
        })");

RagIngestionService::RagIngestionService(ProfileIOS* profile) : profile_(profile) {
  network_client_ = std::make_unique<RagIngestionNetworkClient>(profile);
  if (HostContentSettingsMap* map = ios::HostContentSettingsMapFactory::GetForProfile(profile_)) {
      map->AddObserver(this);
  }
}

RagIngestionService::~RagIngestionService() = default;

RagIngestionService::BackendPermissionInfo::BackendPermissionInfo() = default;
RagIngestionService::BackendPermissionInfo::BackendPermissionInfo(
    const BackendPermissionInfo&) = default;
RagIngestionService::BackendPermissionInfo::~BackendPermissionInfo() = default;

void RagIngestionService::Shutdown() {
  metadata_loaders_.clear();
  if (HostContentSettingsMap* map = ios::HostContentSettingsMapFactory::GetForProfile(profile_)) {
      map->RemoveObserver(this);
  }
}

RagIngestionService::UserPermission RagIngestionService::GetUserPermission(const GURL& url) {
  HostContentSettingsMap* map = ios::HostContentSettingsMapFactory::GetForProfile(profile_);
  if (!map) return UserPermission::kUndecided;

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

  if (HostContentSettingsMap* map = ios::HostContentSettingsMapFactory::GetForProfile(profile_)) {
    map->SetContentSettingDefaultScope(url, url, ContentSettingsType::RAG_INGESTION, setting);
  }
}

void RagIngestionService::SyncLocalSettingFromBackend(const GURL& url, UserPermission status) {
    base::AutoReset<bool> scope(&is_updating_from_backend_, true);
    SetUserPermission(url, status);
}

void RagIngestionService::OnContentSettingChanged(
    const ContentSettingsPattern& primary_pattern,
    const ContentSettingsPattern& secondary_pattern,
    ContentSettingsTypeSet content_type_set) {

  if (!content_type_set.Contains(ContentSettingsType::RAG_INGESTION)) return;
  if (is_updating_from_backend_) return;

  GURL url(primary_pattern.ToString());
  if (!url.is_valid()) return;

  UserPermission status = GetUserPermission(url);
  std::string active_tab_title;

  // iOS WebState Tab Iteration
  BrowserList* browser_list = BrowserListFactory::GetForProfile(profile_);
  for (Browser* browser : browser_list->BrowsersOfType(BrowserList::BrowserType::kRegular)) {
    WebStateList* web_state_list = browser->GetWebStateList();
    for (int i = 0; i < web_state_list->count(); ++i) {
      web::WebState* web_state = web_state_list->GetWebStateAt(i);
      
      if (web_state && web_state->GetVisibleURL().DeprecatedGetOriginAsURL() == url.DeprecatedGetOriginAsURL()) {
        if (active_tab_title.empty()) {
            active_tab_title = base::UTF16ToUTF8(web_state->GetTitle());
        }
        
        // TODO (Phase 2): Inject/Update Icon State in UI
        // if (status == UserPermission::kGranted) controller->ShowActiveState();
      }
    }
  }

  if (network_client_) {
    if (status == UserPermission::kGranted) {
      FetchRootPageMetadata(url.DeprecatedGetOriginAsURL(), url, active_tab_title);
    } else {
      RagPermissionStatus net_status = (status == UserPermission::kDenied) 
                                       ? RagPermissionStatus::kDenied 
                                       : RagPermissionStatus::kUndecided;
      FinalizePermissionGrant(url, net_status, {}); 
    }
  }
}

void RagIngestionService::CheckBackendStatus(
    const GURL& url,
    base::OnceCallback<void(BackendPermissionInfo)> callback) {
  
  if (!network_client_) {
    std::move(callback).Run(BackendPermissionInfo());
    return;
  }

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
    info.is_error = true;
    LOG(ERROR) << "[RAG-iOS] getIngestionPermission API failed.";
    std::move(client_callback).Run(info);
    return;
  }

  const auto& dict = result->GetDict();
    
  const std::string* str = dict.FindString("permission");
  if (str) {
    if (*str == "allowed") info.status = BackendStatus::kKnownAllowed;
    else if (*str == "denied") info.status = BackendStatus::kKnownBlocked;
    else if (*str == "undecided") info.status = BackendStatus::kUnknown; 
  }

  if (const std::string* host = dict.FindString("canonicalHost")) info.canonical_host = *host;
  if (const std::string* name = dict.FindString("siteName")) info.site_name = *name;
    
  info.can_prompt = dict.FindBool("canPrompt").value_or(false);
  info.last_success_at_ms = dict.FindDouble("lastSuccessAtMs").value_or(0);
  info.is_learning = dict.FindBool("isLearning").value_or(false);
  info.requires_upgrade = dict.FindBool("requiresUpgrade").value_or(false);
  
  std::move(client_callback).Run(info);
}

void RagIngestionService::FetchRootPageMetadata(const GURL& root_url, 
                                                const GURL& original_url,
                                                const std::string& active_tab_title) {
  auto resource_request = std::make_unique<network::ResourceRequest>();
  resource_request->url = root_url;
  resource_request->credentials_mode = network::mojom::CredentialsMode::kOmit;

  auto loader = network::SimpleURLLoader::Create(std::move(resource_request), 
                                                 kMetadataTrafficAnnotation);
                                                 
  network::SimpleURLLoader* loader_ptr = loader.get();
  metadata_loaders_.insert(std::move(loader));
  
  loader_ptr->DownloadToString(
    profile_->GetSharedURLLoaderFactory().get(),
    base::BindOnce(&RagIngestionService::OnRootPageFetched,
                   weak_factory_.GetWeakPtr(),
                   loader_ptr,
                   original_url,
                   active_tab_title),
    1024 * 1024);
}

void RagIngestionService::OnRootPageFetched(network::SimpleURLLoader* loader_ptr,
                                            const GURL& original_url,
                                            const std::string& active_tab_title, 
                                            std::optional<std::string> response_body) {
  RagSiteMetadata metadata;
  metadata.title = active_tab_title;
  metadata.site_name = active_tab_title;

  std::string icon_url_str;
  std::string manifest_url_str;

  if (response_body) {
    std::string html = *response_body;

    if (metadata.title.empty()) {
      metadata.title = ExtractMetaTag(html, "og:title");
      if (metadata.title.empty()) metadata.title = ExtractMetaTag(html, "title");
      if (metadata.title.empty()) metadata.title = ExtractBestSiteName(html, original_url.DeprecatedGetOriginAsURL());
    }

    metadata.description = ExtractMetaTag(html, "og:description");
    if (metadata.description.empty()) metadata.description = ExtractMetaTag(html, "description");
    metadata.keywords = ExtractMetaTag(html, "keywords");

    icon_url_str = ExtractIconUrl(html, original_url.DeprecatedGetOriginAsURL());
    manifest_url_str = ExtractManifestUrl(html, original_url.DeprecatedGetOriginAsURL());
  } else {
    if (metadata.site_name.empty()) metadata.site_name = original_url.host();
    
    GURL origin = original_url.DeprecatedGetOriginAsURL();
    manifest_url_str = origin.Resolve("manifest.json").spec();
    icon_url_str = origin.Resolve("favicon.ico").spec();
  }

  auto it = metadata_loaders_.find(loader_ptr);
  if (it != metadata_loaders_.end()) metadata_loaders_.erase(it);

  if (!manifest_url_str.empty()) {
    FetchManifest(original_url, manifest_url_str, icon_url_str, std::move(metadata));
    return; 
  } else if (!icon_url_str.empty()) {
    FetchFavicon(original_url, GURL(icon_url_str), std::move(metadata));
    return; 
  }

  FinalizePermissionGrant(original_url, RagPermissionStatus::kAllowed, std::move(metadata));
}

void RagIngestionService::FetchFavicon(const GURL& original_url, 
                                       const GURL& icon_url, 
                                       RagSiteMetadata metadata) {
  favicon::FaviconService* favicon_service =
      ios::FaviconServiceFactory::GetForProfile(profile_, ServiceAccessType::EXPLICIT_ACCESS);

  if (!favicon_service) {
      FinalizePermissionGrant(original_url, RagPermissionStatus::kAllowed, std::move(metadata));
      return;
  }

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
      scoped_refptr<base::RefCountedMemory> png_bytes = result.image.As1xPNGBytes();
      if (png_bytes && png_bytes->size() > 0) {
          metadata.icon_base64 = base::Base64Encode(*png_bytes);
          metadata.icon_mime_type = "image/png";
      }
  }

  FinalizePermissionGrant(original_url, RagPermissionStatus::kAllowed, std::move(metadata));
}

void RagIngestionService::OnFaviconFetched(network::SimpleURLLoader* loader_ptr,
                                           const GURL& original_url, 
                                           const GURL& icon_url,
                                           RagSiteMetadata metadata, 
                                           std::optional<std::string> response_body) {
  
  std::string network_mime_type;
  if (loader_ptr && loader_ptr->ResponseInfo()) {
    network_mime_type = loader_ptr->ResponseInfo()->mime_type;
  }

  std::string final_mime_type = "image/x-icon";
  std::string filename = base::ToLowerASCII(icon_url.path());

  if (!network_mime_type.empty() && network_mime_type != "application/octet-stream") {
      final_mime_type = network_mime_type;
  } else if (base::EndsWith(filename, ".png", base::CompareCase::SENSITIVE)) {
      final_mime_type = "image/png";
  } else if (base::EndsWith(filename, ".jpg", base::CompareCase::SENSITIVE) || 
             base::EndsWith(filename, ".jpeg", base::CompareCase::SENSITIVE)) {
      final_mime_type = "image/jpeg";
  } else if (base::EndsWith(filename, ".svg", base::CompareCase::SENSITIVE)) {
      final_mime_type = "image/svg+xml"; 
  }

  if (final_mime_type == "image/vnd.microsoft.icon" || 
      final_mime_type == "image/ico" ||
      base::EndsWith(filename, ".ico", base::CompareCase::SENSITIVE)) {
      final_mime_type = "image/x-icon";
  }

  bool is_valid_image = (final_mime_type.find("image/") != std::string::npos);

  if (is_valid_image && response_body) {
    std::string base64_icon = base::Base64Encode(*response_body);
    if (base64_icon.length() <= 135000) {
      metadata.icon_base64 = std::move(base64_icon);
      metadata.icon_mime_type = final_mime_type;
    }
  }
  
  FinalizePermissionGrant(original_url, RagPermissionStatus::kAllowed, std::move(metadata));
  
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

  auto loader = network::SimpleURLLoader::Create(std::move(request), kMetadataTrafficAnnotation);
  network::SimpleURLLoader* loader_ptr = loader.get();
  metadata_loaders_.insert(std::move(loader));
  
  loader_ptr->DownloadToString(
    profile_->GetSharedURLLoaderFactory().get(),
    base::BindOnce(&RagIngestionService::OnManifestFetched,
                   weak_factory_.GetWeakPtr(),
                   loader_ptr, original_url, icon_url, std::move(metadata)),
    512 * 1024);
}

void RagIngestionService::OnManifestFetched(network::SimpleURLLoader* loader_ptr,
                                            const GURL& original_url, 
                                            const std::string& icon_url,
                                            RagSiteMetadata metadata, 
                                            std::optional<std::string> response_body) {
  if (response_body) {
    std::optional<base::Value> parsed = base::JSONReader::Read(*response_body, 
        base::JSON_PARSE_CHROMIUM_EXTENSIONS);

    if (parsed && parsed->is_dict()) {
      const auto& dict = parsed->GetDict();
      const std::string* name = dict.FindString("name");
      const std::string* short_name = dict.FindString("short_name");
      
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
            FetchFavicon(original_url, original_url.Resolve(*src), std::move(metadata));
            return;
          }
        }
      }
    }
  }

  auto it = metadata_loaders_.find(loader_ptr);
  if (it != metadata_loaders_.end()) metadata_loaders_.erase(it);

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

std::string RagIngestionService::ExtractBestSiteName(const std::string& html, const GURL& root_url) {
  std::string site_name;

  site_name = ExtractMetaTag(html, "og:site_name");
  if (!site_name.empty()) return site_name;

  site_name = ExtractMetaTag(html, "apple-mobile-web-app-title");
  if (!site_name.empty()) return site_name;

  site_name = ExtractMetaTag(html, "application-name");
  if (!site_name.empty()) return site_name;

  site_name = ExtractMetaTag(html, "og:title");
  if (!site_name.empty()) return site_name;

  std::string raw_title;
  if (re2::RE2::PartialMatch(html, "(?is)<title[^>]*>(.*?)</title>", &raw_title)) {
      raw_title = base::UTF16ToUTF8(base::UnescapeForHTML(base::UTF8ToUTF16(raw_title)));
      base::TrimWhitespaceASCII(raw_title, base::TRIM_ALL, &raw_title);
      if (!raw_title.empty()) return raw_title;
  }

  site_name = ExtractMetaTag(html, "title"); 
  if (!site_name.empty()) return site_name;

  return std::string(root_url.host());
}

std::string RagIngestionService::ExtractManifestUrl(const std::string& html, const GURL& root_url) {
  size_t rel_pos = html.find("rel=\"manifest\"");
  if (rel_pos != std::string::npos) {
    size_t link_start = html.rfind("<link", rel_pos);
    if (link_start != std::string::npos) {
      size_t href_pos = html.find("href=\"", link_start);
      if (href_pos != std::string::npos && href_pos < html.find(">", link_start)) {
        href_pos += 6;
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
  
  std::string pattern1 = "(?i)<meta[^>]+(?:name|property)\\s*=\\s*['\"]" + key + "['\"][^>]+content\\s*=\\s*['\"]([^'\"]+)['\"]";
  if (re2::RE2::PartialMatch(html, pattern1, &value)) {
      return base::UTF16ToUTF8(base::UnescapeForHTML(base::UTF8ToUTF16(value)));
  }

  std::string pattern2 = "(?i)<meta[^>]+content\\s*=\\s*['\"]([^'\"]+)['\"][^>]+(?:name|property)\\s*=\\s*['\"]" + key + "['\"]";
  if (re2::RE2::PartialMatch(html, pattern2, &value)) {
      return base::UTF16ToUTF8(base::UnescapeForHTML(base::UTF8ToUTF16(value)));
  }

  return "";
}

std::string RagIngestionService::ExtractIconUrl(const std::string& html, const GURL& root_url) {
  std::string href_val;

  if (re2::RE2::PartialMatch(html, 
          "(?i)<link[^>]*rel\\s*=\\s*['\"](?:shortcut\\s+)?icon['\"][^>]*href\\s*=\\s*['\"]([^'\"]+)['\"]", 
          &href_val)) {
      return root_url.Resolve(href_val).spec();
  }

  if (re2::RE2::PartialMatch(html, 
          "(?i)<link[^>]*href\\s*=\\s*['\"]([^'\"]+)['\"][^>]*rel\\s*=\\s*['\"](?:shortcut\\s+)?icon['\"]", 
          &href_val)) {
      return root_url.Resolve(href_val).spec();
  }

  return root_url.Resolve("/favicon.ico").spec();
}

// ===========================================================================
// FLOW C: PASSIVE LEARNING PIPELINE
// ===========================================================================

void RagIngestionService::SetPrivateKey(const std::string& private_key_base64) {
  if (!private_key_base64.empty()) {
    LOG(INFO) << "[RAG-iOS] Private Key set.";
  }
  private_key_base64_ = private_key_base64;
}

std::string RagIngestionService::FilterMhtmlToTextOnly(const std::string& mhtml) {
  if (mhtml.empty()) return mhtml;

  std::string_view mhtml_view(mhtml);

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
    LOG(WARNING) << "[RAG-iOS] Failed to parse MHTML boundary. Sending full file.";
    return mhtml;
  }

  std::string part_separator = "--" + std::string(boundary);
  std::string filtered_mhtml;
  filtered_mhtml.reserve(mhtml.size() / 4); 

  size_t pos = 0;
  size_t next_pos = mhtml_view.find(part_separator, pos);

  if (next_pos == std::string_view::npos) {
    return mhtml;
  }

  filtered_mhtml += mhtml_view.substr(0, next_pos);

  while (next_pos != std::string_view::npos) {
    pos = next_pos + part_separator.length();

    if (pos < mhtml_view.length() && mhtml_view[pos] == '-' && mhtml_view[pos + 1] == '-') {
      break; 
    }

    next_pos = mhtml_view.find(part_separator, pos);
    size_t part_end = (next_pos != std::string_view::npos) ? next_pos : mhtml_view.length();
    
    std::string_view part = mhtml_view.substr(pos, part_end - pos);

    size_t headers_end = part.find("\r\n\r\n");
    if (headers_end == std::string_view::npos) {
        headers_end = part.find("\n\n"); 
    }
    
    if (headers_end != std::string_view::npos) {
      std::string_view headers = part.substr(0, headers_end);
      std::string headers_lower = base::ToLowerASCII(headers);

      if (headers_lower.find("content-type: text/html") != std::string::npos) {
        filtered_mhtml += part_separator;
        filtered_mhtml += part;
      }
    }
  }

  filtered_mhtml += part_separator + "--\r\n";
  filtered_mhtml.shrink_to_fit();

  return filtered_mhtml;
}

void RagIngestionService::StartDocumentIngestion(const GURL& url,
                                                 const std::string& page_title,
                                                 const std::string& file_bytes, 
                                                 const std::string& mime_type,
                                                 const std::string& filename) {
  constexpr size_t kMaxProcessableBytes = 50 * 1024 * 1024; 
  if (file_bytes.size() > kMaxProcessableBytes) {
    return;
  }

  std::string filtered_bytes = file_bytes;
  if (mime_type == "multipart/related") {
    filtered_bytes = FilterMhtmlToTextOnly(file_bytes); 
  }

  if (filtered_bytes.size() > 10 * 1024 * 1024) {
    return;
  }

  if (!network_client_) return;

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

  for (auto& item : *text_records) {
    if (item.is_dict()) {
      base::DictValue& record = item.GetDict();
      const std::string* clear_text = record.FindString("text");
      
      if (clear_text) {
        std::string encrypted_text = EncryptSingleString(*clear_text); 
        record.Set("text", encrypted_text);
      }
    }
  }

  network_client_->IngestEncryptedDocument(url, std::move(document_text), base::DoNothing());
}

void RagIngestionService::StartPassiveLearningPipeline(const GURL& url, 
                                                       const std::string& canonical_host,
                                                       const std::string& page_title,
                                                       const std::string& inner_text) {
  if (!network_client_) return;
  if (private_key_base64_.empty()) {
    LOG(WARNING) << "[RAG-iOS] Cannot start passive learning: Private key is missing.";
    return;
  }

  LOG(INFO) << "[RAG-iOS] Starting Passive Learning for: " << url.spec();

  network_client_->ChunkDocument(
      url,
      canonical_host,
      page_title,
      inner_text,
      base::BindOnce(&RagIngestionService::OnDocumentChunked,
                     weak_factory_.GetWeakPtr(), 
                     url));
}

void RagIngestionService::OnDocumentChunked(const GURL& url, std::optional<base::Value> result) {
  if (!result || !result->is_dict()) {
    LOG(ERROR) << "[RAG-iOS] chunkDocument API failed.";
    return;
  }

  const base::ListValue* chunks_list = result->GetDict().FindList("chunks");
  if (!chunks_list || chunks_list->empty()) {
    LOG(WARNING) << "[RAG-iOS] chunkDocument returned no chunks.";
    return;
  }

  std::vector<std::string> chunks;
  for (const auto& item : *chunks_list) {
    if (item.is_string()) {
      chunks.push_back(item.GetString());
    }
  }

  network_client_->EmbedChunks(
      chunks,
      base::BindOnce(&RagIngestionService::OnChunksEmbedded,
                     weak_factory_.GetWeakPtr(), 
                     url, 
                     chunks));
}

void RagIngestionService::OnChunksEmbedded(const GURL& url, 
                                           std::vector<std::string> chunks, 
                                           std::optional<base::Value> result) {
  if (!result || !result->is_dict()) {
    LOG(ERROR) << "[RAG-iOS] embedChunks API failed.";
    return;
  }

  const base::ListValue* vectors_list = result->GetDict().FindList("vectors");
  if (!vectors_list || vectors_list->size() != chunks.size()) {
    LOG(ERROR) << "[RAG-iOS] embedChunks returned mismatched or empty vectors.";
    return;
  }

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

  std::vector<std::string> encrypted_chunks = EncryptChunks(chunks);
  if (encrypted_chunks.empty()) {
     LOG(ERROR) << "[RAG-iOS] Encryption failed. Aborting ingestion.";
     return;
  }

  network_client_->IngestDocument(
      url, 
      encrypted_chunks, 
      vectors,
      base::BindOnce([]() {
        LOG(INFO) << "[RAG-iOS] Passive Learning pipeline completed successfully.";
      }));
}

std::vector<std::string> RagIngestionService::EncryptChunks(
    const std::vector<std::string>& chunks) {
  
  std::vector<std::string> encrypted_chunks;

  std::string raw_key;
  if (!base::Base64Decode(private_key_base64_, &raw_key)) {
    return encrypted_chunks;
  }

  if (raw_key.size() != 32) { 
    return encrypted_chunks;
  }

  for (const std::string& chunk : chunks) {
    std::vector<uint8_t> iv_bytes(16);
    crypto::RandBytes(iv_bytes);
    std::string iv(iv_bytes.begin(), iv_bytes.end());

    size_t block_size = 16;
    uint8_t pad_val = block_size - (chunk.length() % block_size);
    std::string padded_chunk = chunk;
    padded_chunk.append(pad_val, static_cast<char>(pad_val));

    bssl::ScopedEVP_CIPHER_CTX ctx;

    if (!EVP_EncryptInit_ex(ctx.get(), EVP_aes_256_ctr(), nullptr, 
                            reinterpret_cast<const uint8_t*>(raw_key.data()), 
                            reinterpret_cast<const uint8_t*>(iv.data()))) {
        return std::vector<std::string>(); 
    }

    if (!ctx.get()) {
        return std::vector<std::string>();
    }

    std::vector<uint8_t> ciphertext(padded_chunk.size() + block_size);
    int out_len1 = 0;
    int out_len2 = 0;

    EVP_EncryptUpdate(ctx.get(), ciphertext.data(), &out_len1,
                      reinterpret_cast<const uint8_t*>(padded_chunk.data()),
                      padded_chunk.size());
    EVP_EncryptFinal_ex(ctx.get(), 
                        base::span(ciphertext).subspan(base::checked_cast<size_t>(out_len1)).data(), 
                        &out_len2);

    std::string cipher_str(ciphertext.begin(), ciphertext.begin() + out_len1 + out_len2);
    std::string combined = base::Base64Encode(iv) + ":" + base::Base64Encode(cipher_str);
    encrypted_chunks.push_back(combined);
  }

  return encrypted_chunks;
}

std::string RagIngestionService::EncryptSingleString(const std::string& clear_text) {
  if (clear_text.empty()) {
    return "";
  }

  std::string raw_key;
  if (!base::Base64Decode(private_key_base64_, &raw_key)) {
    return "";
  }

  if (raw_key.size() != 32) { 
    return "";
  }

  std::vector<uint8_t> iv_bytes(16);
  crypto::RandBytes(iv_bytes);
  std::string iv(iv_bytes.begin(), iv_bytes.end());

  size_t block_size = 16;
  uint8_t pad_val = block_size - (clear_text.length() % block_size);
  std::string padded_text = clear_text;
  padded_text.append(pad_val, static_cast<char>(pad_val));

  bssl::ScopedEVP_CIPHER_CTX ctx;
  if (!EVP_EncryptInit_ex(ctx.get(), EVP_aes_256_ctr(), nullptr, 
                          reinterpret_cast<const uint8_t*>(raw_key.data()), 
                          reinterpret_cast<const uint8_t*>(iv.data()))) {
    return ""; 
  }

  if (!ctx.get()) {
    return "";
  }

  std::vector<uint8_t> ciphertext(padded_text.size() + block_size);
  int out_len1 = 0;
  int out_len2 = 0;

  EVP_EncryptUpdate(ctx.get(), ciphertext.data(), &out_len1,
                    reinterpret_cast<const uint8_t*>(padded_text.data()),
                    padded_text.size());
                    
  EVP_EncryptFinal_ex(ctx.get(), 
                      base::span(ciphertext).subspan(base::checked_cast<size_t>(out_len1)).data(), 
                      &out_len2);

  std::string cipher_str(ciphertext.begin(), ciphertext.begin() + out_len1 + out_len2);
  return base::Base64Encode(iv) + ":" + base::Base64Encode(cipher_str);
}