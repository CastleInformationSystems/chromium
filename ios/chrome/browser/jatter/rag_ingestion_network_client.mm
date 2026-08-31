// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#import "ios/chrome/browser/jatter/rag_ingestion_network_client.h"

#import "base/functional/callback_helpers.h"
#import "base/json/json_reader.h"
#import "base/json/json_writer.h"
#import "base/logging.h"
#import "base/strings/string_number_conversions.h"
#import "base/strings/string_util.h"
#import "base/strings/sys_string_conversions.h"
#import "base/uuid.h"
#import "components/prefs/pref_service.h"
#import "ios/chrome/browser/jatter/jatter_environment.h"
#import "ios/chrome/browser/jatter/jatter_firebase_client.h"
#import "ios/chrome/browser/shared/model/profile/profile_ios.h"
#import "net/traffic_annotation/network_traffic_annotation.h"
#import "services/network/public/cpp/resource_request.h"
#import "services/network/public/cpp/shared_url_loader_factory.h"
#import "services/network/public/cpp/simple_url_loader.h"

// Constants
const char kRagIngestionProfileGuidPref[] = "beacon.rag_ingestion.profile_guid";

constexpr net::NetworkTrafficAnnotationTag kRagTrafficAnnotation =
    net::DefineNetworkTrafficAnnotation("rag_ingestion_client_ios", R"(
        semantics {
          sender: "RAG Ingestion Service iOS"
          description: "Communicates with RAG backend for domain permissions and content ingestion."
          trigger: "User navigation or permission grant."
          data: "URLs, domain names, and page content for approved domains."
          destination: OTHER
        }
        policy {
          cookies_allowed: NO
          setting: "Feature is strictly opt-in."
        })");

RagIngestionNetworkClient::RagIngestionNetworkClient(ProfileIOS* profile)
    : profile_(profile) {}

RagIngestionNetworkClient::~RagIngestionNetworkClient() = default;

// RagSiteMetadata Implementation
RagSiteMetadata::RagSiteMetadata() = default;
RagSiteMetadata::RagSiteMetadata(const RagSiteMetadata&) = default;
RagSiteMetadata::RagSiteMetadata(RagSiteMetadata&&) = default;
RagSiteMetadata::~RagSiteMetadata() = default;
RagSiteMetadata& RagSiteMetadata::operator=(const RagSiteMetadata&) = default;
RagSiteMetadata& RagSiteMetadata::operator=(RagSiteMetadata&&) = default;

std::string RagIngestionNetworkClient::GetProfileGuid() const {
  if (!profile_) return "";

  PrefService* prefs = profile_->GetPrefs();
  if (!prefs) return "";

  std::string guid = prefs->GetString(kRagIngestionProfileGuidPref);

  if (guid.empty()) {
    guid = base::Uuid::GenerateRandomV4().AsLowercaseString();
    prefs->SetString(kRagIngestionProfileGuidPref, guid);
  }
  return guid;
}

// -----------------------------------------------------------------------------
// FLOW A: PERMISSIONS
// -----------------------------------------------------------------------------

void RagIngestionNetworkClient::CheckDomainPermission(const GURL& url, ApiCallback callback) {
  base::DictValue payload;
  payload.Set("profileGuid", GetProfileGuid());
  payload.Set("url", url.spec());

  MakeAuthorizedRequest(
      GURL(std::string(jatter::kBaseApiUrl) + "/getIngestionPermission"), 
      "POST",
      std::move(payload), 
      std::move(callback));
}

void RagIngestionNetworkClient::SetIngestionPermission(const GURL& url, 
                                                       RagPermissionStatus permission,
                                                       const RagSiteMetadata& metadata) {
  base::DictValue payload;
  payload.Set("profileGuid", GetProfileGuid());
  
  std::string url_spec = url.spec();
  if (url_spec.length() > 5096) url_spec = url_spec.substr(0, 5096);
  payload.Set("url", url_spec);

  std::string safe_site_name = metadata.site_name.empty() ? std::string(url.host()) : metadata.site_name;
  if (safe_site_name.length() > 32) safe_site_name = safe_site_name.substr(0, 32);
  payload.Set("siteName", safe_site_name);

  std::string perm_str;
  switch (permission) {
    case RagPermissionStatus::kAllowed:   perm_str = "allowed"; break;
    case RagPermissionStatus::kDenied:    perm_str = "denied"; break;
    case RagPermissionStatus::kUndecided: perm_str = "undecided"; break;
  }
  payload.Set("permission", perm_str);

  if (!metadata.title.empty()) payload.Set("title", metadata.title);
  if (!metadata.description.empty()) payload.Set("description", metadata.description);
  if (!metadata.keywords.empty()) payload.Set("keywords", metadata.keywords);
  
  if (!metadata.icon_base64.empty()) {
     if (metadata.icon_base64.length() < 140000) {
        payload.Set("icon", metadata.icon_base64);
        if (!metadata.icon_mime_type.empty()) {
          payload.Set("iconMimeType", metadata.icon_mime_type);
        }
     } else {
        LOG(WARNING) << "[RAG-iOS] Icon too large, skipping.";
     }
  }

  MakeAuthorizedRequest(
      GURL(std::string(jatter::kBaseApiUrl) + "/setIngestionPermission"), 
      "POST",
      std::move(payload), 
      base::DoNothing());
}

// -----------------------------------------------------------------------------
// FLOW C: PASSIVE LEARNING
// -----------------------------------------------------------------------------

void RagIngestionNetworkClient::ChunkDocument(const GURL& url, 
                                              const std::string& canonical_host,
                                              const std::string& title,
                                              const std::string& content, 
                                              ApiCallback callback) {
  base::DictValue payload;
  payload.Set("profileGuid", GetProfileGuid());
  payload.Set("url", url.spec());
  payload.Set("content", content);
  
  std::string safe_host = canonical_host.empty() ? std::string(url.host()) : canonical_host;

  // Set them at the root level...
  payload.Set("canonicalHost", safe_host);
  payload.Set("pageTitle", title);

  // ...and inside a metadata block to guarantee the backend finds it regardless of structure
  base::DictValue metadata;
  metadata.Set("canonicalHost", safe_host);
  metadata.Set("title", title);
  payload.Set("metadata", std::move(metadata));

  GURL endpoint(std::string(jatter::kBaseApiUrl) + "/chunkDocument");
  MakeAuthorizedRequest(endpoint, "POST", std::move(payload), std::move(callback));
}

void RagIngestionNetworkClient::EmbedChunks(const std::vector<std::string>& chunks, ApiCallback callback) {
  base::ListValue chunks_list;
  for (const std::string& chunk : chunks) {
    chunks_list.Append(chunk);
  }

  base::DictValue payload;
  payload.Set("chunks", std::move(chunks_list));

  GURL endpoint(std::string(jatter::kBaseApiUrl) + "/embedChunks");
  MakeAuthorizedRequest(endpoint, "POST", std::move(payload), std::move(callback));
}

void RagIngestionNetworkClient::IngestDocument(
    const GURL& url,
    const std::vector<std::string>& encrypted_chunks,
    const std::vector<std::vector<double>>& vectors,
    base::OnceClosure callback) {

  base::DictValue payload;
  payload.Set("profileGuid", GetProfileGuid());
  payload.Set("url", url.spec());

  base::ListValue encrypted_chunks_list;
  for (const std::string& chunk : encrypted_chunks) {
    encrypted_chunks_list.Append(chunk);
  }
  payload.Set("encryptedChunks", std::move(encrypted_chunks_list));

  base::ListValue vectors_list;
  for (const std::vector<double>& vec : vectors) {
    base::ListValue single_vector_list;
    for (double val : vec) {
      single_vector_list.Append(val);
    }
    vectors_list.Append(std::move(single_vector_list));
  }
  payload.Set("vectors", std::move(vectors_list));

  auto callback_adapter = base::BindOnce(
      [](base::OnceClosure cb, std::optional<base::Value> result) {
        if (cb) std::move(cb).Run();
      },
      std::move(callback));

  GURL endpoint(std::string(jatter::kBaseApiUrl) + "/ingestResource");
  MakeAuthorizedRequest(endpoint, "POST", std::move(payload), std::move(callback_adapter));
}

void RagIngestionNetworkClient::UploadRawDocument(
    const GURL& url,
    const std::string& page_title,
    const std::string& file_bytes,
    const std::string& mime_type,
    const std::string& filename,
    ApiCallback callback) {

  GURL endpoint(std::string(jatter::kBaseApiUrl) + "/ingestDocument");
  
  std::string boundary = "----WebKitFormBoundary" + base::Uuid::GenerateRandomV4().AsLowercaseString();
  std::string content_type = "multipart/form-data; boundary=" + boundary;

  std::string multipart_body;
  multipart_body += "--" + boundary + "\r\n";
  multipart_body += "Content-Disposition: form-data; name=\"sourceUrl\"\r\n\r\n";
  multipart_body += url.spec() + "\r\n";
  multipart_body += "--" + boundary + "\r\n";
  multipart_body += "Content-Disposition: form-data; name=\"profileGuid\"\r\n\r\n";
  multipart_body += GetProfileGuid() + "\r\n";
  multipart_body += "--" + boundary + "\r\n";
  multipart_body += "Content-Disposition: form-data; name=\"pageTitle\"\r\n\r\n";
  multipart_body += page_title + "\r\n";
  multipart_body += "--" + boundary + "\r\n";
  multipart_body += "Content-Disposition: form-data; name=\"file\"; filename=\"" + filename + "\"\r\n";
  multipart_body += "Content-Type: " + mime_type + "\r\n\r\n";
  multipart_body += file_bytes;
  multipart_body += "\r\n--" + boundary + "--\r\n";

  MakeAuthorizedRequest(endpoint, "POST", multipart_body, content_type, std::move(callback));
}

void RagIngestionNetworkClient::IngestEncryptedDocument(
    const GURL& url,
    base::DictValue document_text_json,
    base::OnceClosure callback) {

  document_text_json.Set("profileGuid", GetProfileGuid());
  document_text_json.Set("url", url.spec());

  auto callback_adapter = base::BindOnce(
      [](base::OnceClosure cb, std::optional<base::Value> result) {
        if (cb) std::move(cb).Run();
      },
      std::move(callback));

  GURL endpoint(std::string(jatter::kBaseApiUrl) + "/ingestEncryptedDocumentText");
  MakeAuthorizedRequest(endpoint, "POST", std::move(document_text_json), std::move(callback_adapter));
}

// --- CORE NETWORK METHOD ---
void RagIngestionNetworkClient::MakeAuthorizedRequest(
    const GURL& endpoint,
    const std::string& method,
    const std::string& payload,
    const std::string& content_type,
    ApiCallback callback) {

  ProfileIOS* profile = profile_;

  JatterFirebaseClient::GetInstance()->Invoke(
      profile_,
      [endpoint, method, payload, content_type, profile]() -> std::unique_ptr<network::SimpleURLLoader> {
#ifndef JATTER_PRODUCTION_MODE
        LOG(INFO) << "[RAG-iOS] MakeAuthorizedRequest for URL: " << endpoint.spec();
#endif
        auto resource_request = std::make_unique<network::ResourceRequest>();
        resource_request->url = endpoint;
        resource_request->method = method;

        if (!content_type.empty()) {
          resource_request->headers.SetHeader("Content-Type", content_type);
        }

        resource_request->credentials_mode = network::mojom::CredentialsMode::kOmit;

        JatterFirebaseClient::SetAuthorizationHeader(profile, resource_request.get());

        auto loader = network::SimpleURLLoader::Create(
            std::move(resource_request), kRagTrafficAnnotation);

        loader->SetAllowHttpErrorResults(true);

        if (!payload.empty() && !content_type.empty()) {
          loader->AttachStringForUpload(payload, content_type);
        }
        return loader;
      },
      base::BindOnce(
          [](ApiCallback cb, std::optional<std::string> response_body) {
            if (!response_body) {
              LOG(ERROR) << "[RAG-iOS] Network request failed after retries.";
              std::move(cb).Run(std::nullopt);
              return;
            }
            std::move(cb).Run(base::JSONReader::Read(*response_body, base::JSON_PARSE_CHROMIUM_EXTENSIONS));
          },
          std::move(callback)));
}

// --- JSON WRAPPER ---
void RagIngestionNetworkClient::MakeAuthorizedRequest(
    const GURL& endpoint,
    const std::string& method,
    std::optional<base::DictValue> payload,
    ApiCallback callback) {
  
  std::string json_body;
  if (payload) {
    base::JSONWriter::Write(*payload, &json_body);
  }

  MakeAuthorizedRequest(endpoint, method, json_body, "application/json", std::move(callback));
}