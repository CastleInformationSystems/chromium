// Copyright 2024 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/rag_ingestion/rag_ingestion_network_client.h"

#include "base/compiler_specific.h" // For UNSAFE_BUFFERS macro
#include "base/containers/span.h"
#include "base/functional/callback_helpers.h"
#include "base/json/json_reader.h"
#include "base/json/json_writer.h"
#include "base/strings/stringprintf.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/string_util.h"
#include "base/strings/utf_string_conversions.h"
#include "base/uuid.h" // For generating random GUIDs
#include "chrome/browser/jatter/jatter_environment.h"
#include "chrome/browser/jatter/jatter_firebase_client.h"
#include "chrome/browser/jatter/jatter_token_storage.h"
#include "chrome/browser/profiles/profile.h"
#include "components/prefs/pref_service.h" // For storage
#include "content/public/browser/storage_partition.h"
#include "crypto/obsolete/md5.h"
#include "net/base/load_flags.h"
#include "net/http/http_response_headers.h"
#include "net/traffic_annotation/network_traffic_annotation.h"
#include "services/network/public/cpp/resource_request.h"
#include "services/network/public/cpp/shared_url_loader_factory.h"
#include "services/network/public/cpp/simple_url_loader.h"
#include "services/network/public/mojom/url_response_head.mojom.h"
#include "url/origin.h"
#include "third_party/boringssl/src/include/openssl/md5.h"

// Constants
const char kRagIngestionProfileGuidPref[] = "beacon.rag_ingestion.profile_guid";

constexpr net::NetworkTrafficAnnotationTag kRagTrafficAnnotation =
    net::DefineNetworkTrafficAnnotation("rag_ingestion_client", R"(
        semantics {
          sender: "RAG Ingestion Service"
          description: "Communicates with RAG backend for domain permissions and content ingestion."
          trigger: "User navigation or background polling timer."
          data: "URLs, domain names, and page content for approved domains."
          destination: OTHER
        }
        policy {
          cookies_allowed: NO
          setting: "Enterprise policy controlled."
          policy_exception_justification: "Feature is strictly opt-in."
        })");

RagIngestionNetworkClient::RagIngestionNetworkClient(Profile* profile)
    : profile_(profile) {}

RagIngestionNetworkClient::~RagIngestionNetworkClient() = default;

// RagSiteMetadata
RagSiteMetadata::RagSiteMetadata() = default;
RagSiteMetadata::RagSiteMetadata(const RagSiteMetadata&) = default;
RagSiteMetadata::RagSiteMetadata(RagSiteMetadata&&) = default;
RagSiteMetadata::~RagSiteMetadata() = default;
RagSiteMetadata& RagSiteMetadata::operator=(const RagSiteMetadata&) = default;
RagSiteMetadata& RagSiteMetadata::operator=(RagSiteMetadata&&) = default;

// -----------------------------------------------------------------------------
// HELPER: Site ID Generation
// -----------------------------------------------------------------------------
std::string RagIngestionNetworkClient::GenerateSiteId(const GURL& url) const {
  // Matches Server Logic: MD5 of the Serialized Origin
  url::Origin origin = url::Origin::Create(url);
  std::string data = origin.Serialize();

  // 1. Safely convert the string to a byte span
  base::span<const uint8_t> data_span = base::as_byte_span(data);
  
  uint8_t digest[MD5_DIGEST_LENGTH];

  // 2. UNSAFE_BUFFERS explicitly tells the Chromium compiler: 
  // "I know I am passing raw pointers, but I am forced to by a C API."
  UNSAFE_BUFFERS(
      MD5(data_span.data(), data_span.size(), digest)
  );

  // base::HexEncode safely accepts the digest array as an implicit span
  return base::ToLowerASCII(base::HexEncode(digest));
}

std::string RagIngestionNetworkClient::GetProfileGuid() const {
  if (!profile_) return "";

  PrefService* prefs = profile_->GetPrefs();
  if (!prefs) return "";

  // 1. Check if we already have a GUID stored
  std::string guid = prefs->GetString(kRagIngestionProfileGuidPref);

  // 2. If empty, generate a new UUID v4
  if (guid.empty()) {
    guid = base::Uuid::GenerateRandomV4().AsLowercaseString();
    prefs->SetString(kRagIngestionProfileGuidPref, guid);
#ifndef JATTER_PRODUCTION_MODE
    LOG(INFO) << "[RAG] Generated new Profile GUID: " << guid;
#endif
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
#ifndef JATTER_PRODUCTION_MODE                                                        
  LOG(INFO) << "[RAG] SetIngestionPermission: " << url.spec();
#endif
  base::DictValue payload;
  payload.Set("profileGuid", GetProfileGuid());
  
  // 1. URL (Trimmed to 5096)
  std::string url_spec = url.spec();
  if (url_spec.length() > 5096) url_spec = url_spec.substr(0, 5096);
  payload.Set("url", url_spec);

  // 2. Site Name (Trimmed to 32, required)
  std::string safe_site_name = metadata.site_name.empty() ? std::string(url.host()) : metadata.site_name;
  if (safe_site_name.length() > 32) safe_site_name = safe_site_name.substr(0, 32);
  payload.Set("siteName", safe_site_name);

  // 3. Permission Enum (MATCHING TS DEFINITION)
  std::string perm_str;
  switch (permission) {
    case RagPermissionStatus::kAllowed:   perm_str = "allowed"; break;
    case RagPermissionStatus::kDenied:    perm_str = "denied"; break;
    case RagPermissionStatus::kUndecided: perm_str = "undecided"; break;
  }
  payload.Set("permission", perm_str);

  // 4. Metadata
  if (!metadata.title.empty()) payload.Set("title", metadata.title);
  if (!metadata.description.empty()) payload.Set("description", metadata.description);
  if (!metadata.keywords.empty()) payload.Set("keywords", metadata.keywords);
  
  // Icon handling
  if (!metadata.icon_base64.empty()) {
     if (metadata.icon_base64.length() < 140000) {
        payload.Set("icon", metadata.icon_base64);
        if (!metadata.icon_mime_type.empty()) {
          payload.Set("iconMimeType", metadata.icon_mime_type);
        }
     } else {
        LOG(WARNING) << "[RAG] Icon too large, skipping.";
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
                                              const std::string& content, 
                                              ApiCallback callback) {
  base::DictValue payload;
  payload.Set("profileGuid", GetProfileGuid());
  payload.Set("url", url.spec());
  payload.Set("content", content);

  GURL endpoint(std::string(jatter::kBaseApiUrl) + "/chunkDocument");
#ifndef JATTER_PRODUCTION_MODE
  LOG(INFO) << "[RAG] Requesting chunks for document...";
#endif
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
#ifndef JATTER_PRODUCTION_MODE
  LOG(INFO) << "[RAG] Requesting embeddings for " << chunks.size() << " chunks...";
#endif
  MakeAuthorizedRequest(endpoint, "POST", std::move(payload), std::move(callback));
}

//To be deprecated
void RagIngestionNetworkClient::IngestDocument(
    const GURL& url,
    const std::vector<std::string>& encrypted_chunks,
    const std::vector<std::vector<double>>& vectors,
    base::OnceClosure callback) {

  base::DictValue payload;
  payload.Set("profileGuid", GetProfileGuid());
  payload.Set("url", url.spec());

  // Convert std::vector<std::string> to base::ListValue
  base::ListValue encrypted_chunks_list;
  for (const std::string& chunk : encrypted_chunks) {
    encrypted_chunks_list.Append(chunk);
  }
  payload.Set("encryptedChunks", std::move(encrypted_chunks_list));

  // Convert std::vector<std::vector<double>> to base::ListValue of Lists
  base::ListValue vectors_list;
  for (const std::vector<double>& vec : vectors) {
    base::ListValue single_vector_list;
    for (double val : vec) {
      single_vector_list.Append(val);
    }
    vectors_list.Append(std::move(single_vector_list));
  }
  payload.Set("vectors", std::move(vectors_list));

  // Adapter to convert ApiCallback to OnceClosure since response is EMPTY
  auto callback_adapter = base::BindOnce(
      [](base::OnceClosure cb, std::optional<base::Value> result) {
        if (cb) std::move(cb).Run();
      },
      std::move(callback));

  GURL endpoint(std::string(jatter::kBaseApiUrl) + "/ingestResource");
#ifndef JATTER_PRODUCTION_MODE
  LOG(INFO) << "[RAG] Ingesting document: " << url.spec();
#endif
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
  
  // 1. Generate boundary
  std::string boundary = "----WebKitFormBoundary" + base::Uuid::GenerateRandomV4().AsLowercaseString();
  std::string content_type = "multipart/form-data; boundary=" + boundary;

  // 2. Build multipart body
  std::string multipart_body;

  // Add sourceUrl
  multipart_body += "--" + boundary + "\r\n";
  multipart_body += "Content-Disposition: form-data; name=\"sourceUrl\"\r\n\r\n";
  multipart_body += url.spec() + "\r\n";

  // Add profileGuid using the existing class method
  multipart_body += "--" + boundary + "\r\n";
  multipart_body += "Content-Disposition: form-data; name=\"profileGuid\"\r\n\r\n";
  multipart_body += GetProfileGuid() + "\r\n";
  
  // Add pageTitle
  multipart_body += "--" + boundary + "\r\n";
  multipart_body += "Content-Disposition: form-data; name=\"pageTitle\"\r\n\r\n";
  multipart_body += page_title + "\r\n";

  // Add file
  multipart_body += "--" + boundary + "\r\n";
  multipart_body += "Content-Disposition: form-data; name=\"file\"; filename=\"" + filename + "\"\r\n";
  multipart_body += "Content-Type: " + mime_type + "\r\n\r\n";
  multipart_body += file_bytes;
  multipart_body += "\r\n--" + boundary + "--\r\n";

  // 3. Call the overloaded MakeAuthorizedRequest
#ifndef JATTER_PRODUCTION_MODE
  LOG(INFO) << "[RAG] Uploading document via multipart to " << endpoint.spec();
#endif
  MakeAuthorizedRequest(endpoint, "POST", multipart_body, content_type, std::move(callback));
}

void RagIngestionNetworkClient::IngestEncryptedDocument(
    const GURL& url,
    base::DictValue document_text_json,
    base::OnceClosure callback) {

  // Append backend tracking identifiers to the JSON structure
  document_text_json.Set("profileGuid", GetProfileGuid());
  document_text_json.Set("url", url.spec());

  // Adapter to convert ApiCallback to OnceClosure since the expected response is just a 200 OK (empty) [cite: 50]
  auto callback_adapter = base::BindOnce(
      [](base::OnceClosure cb, std::optional<base::Value> result) {
        if (cb) std::move(cb).Run();
      },
      std::move(callback));

  GURL endpoint(std::string(jatter::kBaseApiUrl) + "/ingestEncryptedDocumentText"); // [cite: 45]
#ifndef JATTER_PRODUCTION_MODE
  LOG(INFO) << "[RAG] Ingesting encrypted DocumentText JSON for: " << url.spec();
#endif
  // Safe to use MakeAuthorizedRequest because the payload is application/json [cite: 48]
  MakeAuthorizedRequest(endpoint, "POST", std::move(document_text_json), std::move(callback_adapter)); // [cite: 46, 48]
}

// --- THE NEW CORE NETWORK METHOD ---
void RagIngestionNetworkClient::MakeAuthorizedRequest(
    const GURL& endpoint,
    const std::string& method,
    const std::string& payload,
    const std::string& content_type,
    ApiCallback callback) {
  
  JatterFirebaseClient::GetInstance()->Invoke(
      profile_,
      [endpoint, method, payload, content_type, profile = profile_]() {
#ifndef JATTER_PRODUCTION_MODE
        LOG(INFO) << "[RAG] MakeAuthorizedRequest for URL: " << endpoint.spec();
#endif
        auto resource_request = std::make_unique<network::ResourceRequest>();
        resource_request->url = endpoint;
        resource_request->method = method;
        
        // Dynamically set the content type (handles both JSON and Multipart)
        if (!content_type.empty()) {
            resource_request->headers.SetHeader("Content-Type", content_type);
        }
        
        resource_request->credentials_mode = network::mojom::CredentialsMode::kOmit;

        // Uses your existing Firebase Auth injection
        JatterFirebaseClient::SetAuthorizationnHeader(profile, resource_request.get());

        auto loader = network::SimpleURLLoader::Create(
            std::move(resource_request), kRagTrafficAnnotation);
            
        loader->SetAllowHttpErrorResults(true);

        // Attach the string payload safely
        if (!payload.empty() && !content_type.empty()) {
          loader->AttachStringForUpload(payload, content_type);
        }
        return loader;
      },
      base::BindOnce(
          [](ApiCallback cb, std::optional<std::string> response_body) {
            if (!response_body) {
                LOG(ERROR) << "[RAG] Network request failed after retries.";
                std::move(cb).Run(std::nullopt);
                return;
            }
            std::move(cb).Run(base::JSONReader::Read(*response_body, base::JSON_PARSE_CHROMIUM_EXTENSIONS));
          },
          std::move(callback))
  );
}

// --- THE EXISTING JSON WRAPPER ---
void RagIngestionNetworkClient::MakeAuthorizedRequest(
    const GURL& endpoint,
    const std::string& method,
    std::optional<base::DictValue> payload,
    ApiCallback callback) {
  
  std::string json_body;
  if (payload) {
    base::JSONWriter::Write(*payload, &json_body);
  }

  // Simply route to the new core method!
  MakeAuthorizedRequest(endpoint, method, json_body, "application/json", std::move(callback));
}
