// Copyright 2024 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/rag_ingestion/rag_ingestion_network_client.h"

#include "base/hash/md5.h"
#include "base/json/json_reader.h"
#include "base/json/json_writer.h"
#include "base/strings/stringprintf.h"
#include "base/strings/string_util.h"
#include "base/strings/utf_string_conversions.h"
#include "base/uuid.h" // For generating random GUIDs
#include "chrome/browser/jatter/jatter_environment.h"
#include "chrome/browser/jatter/jatter_firebase_client.h"
#include "chrome/browser/jatter/jatter_token_storage.h"
#include "chrome/browser/profiles/profile.h"
#include "components/prefs/pref_service.h" // For storage
#include "content/public/browser/storage_partition.h"
#include "net/base/load_flags.h"
#include "net/http/http_response_headers.h"
#include "net/traffic_annotation/network_traffic_annotation.h"
#include "services/network/public/cpp/resource_request.h"
#include "services/network/public/cpp/shared_url_loader_factory.h"
#include "services/network/public/cpp/simple_url_loader.h"
#include "services/network/public/mojom/url_response_head.mojom.h"
#include "url/origin.h"

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
  return base::MD5String(origin.Serialize());
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
    LOG(INFO) << "[RAG] Generated new Profile GUID: " << guid;
  }

  return guid;
}

// -----------------------------------------------------------------------------
// FLOW A: PERMISSIONS
// -----------------------------------------------------------------------------

void RagIngestionNetworkClient::CheckDomainPermission(const GURL& url, ApiCallback callback) {
  base::Value::Dict payload;
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
  LOG(INFO) << "[RAG] SetIngestionPermission: " << url.spec();

  base::Value::Dict payload;
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

// void RagIngestionNetworkClient::AddIngestionUrl(const GURL& url, base::OnceClosure callback) {
//   base::Value::Dict payload;
//   payload.Set("profileGuid", GetProfileGuid());
//   payload.Set("siteId", GenerateSiteId(url));
//   payload.Set("url", url.spec());
  
//   // NOTE: 'siteName' is removed per PDF spec "ragIngestionAddUrl(url)" logic.

//   // Adapter to convert ApiCallback to OnceClosure
//   auto callback_adapter = base::BindOnce(
//       [](base::OnceClosure cb, std::optional<base::Value> result) {
//         if (cb) std::move(cb).Run();
//       },
//       std::move(callback));

//   MakeAuthorizedRequest(
//       GURL(std::string(jatter::kBaseApiUrl) + "/addIngestionUrl"), 
//       "POST",
//       std::move(payload), 
//       std::move(callback_adapter));
// }

// // [NEW IMPLEMENTATION]
// void RagIngestionNetworkClient::TriggerIngestion(ApiCallback callback) {
//   // API: IngestParams { profileGuid }
//   base::Value::Dict payload;
//   payload.Set("profileGuid", GetProfileGuid());

//   GURL endpoint(std::string(jatter::kBaseApiUrl) + "/ingest");
//   LOG(INFO) << "[RAG] Sending 'ingest()' signal...";
//   MakeAuthorizedRequest(endpoint, "POST", std::move(payload), std::move(callback));
// }

// void RagIngestionNetworkClient::GetIngestionMessages(ApiCallback callback) {
//   // API: GetIngestionMessageParams { profileGuid }
//   base::Value::Dict payload;
//   payload.Set("profileGuid", GetProfileGuid());
  
//   GURL endpoint(std::string(jatter::kBaseApiUrl) + "/getIngestionMessages");
//   // Verbose log only, to avoid spamming console every 5s
//   VLOG(1) << "[RAG] Polling for Ingestion Message...";
//   MakeAuthorizedRequest(endpoint, "POST", std::move(payload), std::move(callback));
// }

// void RagIngestionNetworkClient::ResolveIngestionMessage(
//     const std::string& message_id,
//     const std::string& html,
//     const std::string& inner_text,
//     bool is_authenticated) {
  
//   // API: ResolveIngestionMessageParams { messageId, profileGuid, status, error, html, innerText, responseHeaders }
//   base::Value::Dict payload;
//   payload.Set("messageId", message_id);
//   payload.Set("profileGuid", GetProfileGuid());

//   // Map logic to Status Enum: "ok" | "public"
//   std::string status = is_authenticated ? "ok" : "public";
//   payload.Set("status", status);

//   payload.Set("error", base::Value()); // Explicit null

//   payload.Set("html", html);
//   payload.Set("innerText", inner_text); 
  
//   // Backend expects an empty object if no headers are provided
//   payload.Set("responseHeaders", base::Value::Dict()); 

//   GURL endpoint(std::string(jatter::kBaseApiUrl) + "/resolveIngestionMessage");
//   LOG(INFO) << "[RAG] Resolving Message " << message_id << " (Status: " << status << ")";
//   MakeAuthorizedRequest(endpoint, "POST", std::move(payload), base::DoNothing());
// }

// -----------------------------------------------------------------------------
// FLOW C: PASSIVE LEARNING
// -----------------------------------------------------------------------------

void RagIngestionNetworkClient::ChunkDocument(const GURL& url, 
                                              const std::string& content, 
                                              ApiCallback callback) {
  base::Value::Dict payload;
  payload.Set("profileGuid", GetProfileGuid());
  payload.Set("url", url.spec());
  payload.Set("content", content);

  GURL endpoint(std::string(jatter::kBaseApiUrl) + "/chunkDocument");
  LOG(INFO) << "[RAG] Requesting chunks for document...";
  MakeAuthorizedRequest(endpoint, "POST", std::move(payload), std::move(callback));
}

void RagIngestionNetworkClient::EmbedChunks(const std::vector<std::string>& chunks, ApiCallback callback) {
  base::Value::List chunks_list;
  for (const std::string& chunk : chunks) {
    chunks_list.Append(chunk);
  }

  base::Value::Dict payload;
  payload.Set("chunks", std::move(chunks_list));

  GURL endpoint(std::string(jatter::kBaseApiUrl) + "/embedChunks");
  LOG(INFO) << "[RAG] Requesting embeddings for " << chunks.size() << " chunks...";
  MakeAuthorizedRequest(endpoint, "POST", std::move(payload), std::move(callback));
}

void RagIngestionNetworkClient::IngestDocument(
    const GURL& url,
    const std::vector<std::string>& encrypted_chunks,
    const std::vector<std::vector<double>>& vectors,
    base::OnceClosure callback) {

  base::Value::Dict payload;
  payload.Set("profileGuid", GetProfileGuid());
  payload.Set("url", url.spec());

  // Convert std::vector<std::string> to base::Value::List
  base::Value::List encrypted_chunks_list;
  for (const std::string& chunk : encrypted_chunks) {
    encrypted_chunks_list.Append(chunk);
  }
  payload.Set("encryptedChunks", std::move(encrypted_chunks_list));

  // Convert std::vector<std::vector<double>> to base::Value::List of Lists
  base::Value::List vectors_list;
  for (const std::vector<double>& vec : vectors) {
    base::Value::List single_vector_list;
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
  LOG(INFO) << "[RAG] Ingesting document: " << url.spec();
  MakeAuthorizedRequest(endpoint, "POST", std::move(payload), std::move(callback_adapter));
}

void RagIngestionNetworkClient::MakeAuthorizedRequest(
    const GURL& endpoint,
    const std::string& method,
    std::optional<base::Value::Dict> payload,
    ApiCallback callback) {
  
  // 1. Convert the JSON payload to a string if it exists
  std::string json_body;
  if (payload) {
    base::JSONWriter::Write(*payload, &json_body);
  }

  // 2. Delegate the network request to JatterFirebaseClient
  // This completely solves the Token Refresh bug because Invoke() 
  // automatically handles 401 Unauthorized responses and retries!
  JatterFirebaseClient::GetInstance()->Invoke(
      profile_,
      
      // The Loader Creator Lambda
      [endpoint, method, json_body, profile = profile_]() {
        LOG(INFO) << "[RAG] MakeAuthorizedRequest for URL: " << endpoint.spec();

        auto resource_request = std::make_unique<network::ResourceRequest>();
        resource_request->url = endpoint;
        resource_request->method = method;
        resource_request->headers.SetHeader("Content-Type", "application/json");
        resource_request->credentials_mode = network::mojom::CredentialsMode::kOmit;

        // Use the Firebase client to set the current valid ID token
        JatterFirebaseClient::SetAuthorizationnHeader(profile, resource_request.get());

        auto loader = network::SimpleURLLoader::Create(
            std::move(resource_request), kRagTrafficAnnotation);
            
        loader->SetAllowHttpErrorResults(true);

        // Only attach payload if it's a POST/PUT with a body
        if (!json_body.empty()) {
            loader->AttachStringForUpload(json_body, "application/json");
        }
        return loader;
      },
      
      // The Response Callback Lambda
      base::BindOnce(
          [](ApiCallback cb, std::optional<std::string> response_body) {
            if (!response_body) {
                LOG(ERROR) << "[RAG] Network request failed after retries.";
                std::move(cb).Run(std::nullopt);
                return;
            }

            // Parse the JSON response and send it back to the RAG service
            std::move(cb).Run(base::JSONReader::Read(*response_body, base::JSON_PARSE_CHROMIUM_EXTENSIONS));
          },
          std::move(callback))
  );
}

// void RagIngestionNetworkClient::OnTokenFetched(
//     const GURL& endpoint,
//     const std::string& method,
//     std::optional<base::Value::Dict> payload,
//     ApiCallback client_callback,
//     const std::string& access_token) {

//   if (access_token.empty()) {
//     LOG(ERROR) << "[RAG] Auth token is empty. Aborting request to " << endpoint;
//     std::move(client_callback).Run(std::nullopt);
//     return;
//   }

//   auto resource_request = std::make_unique<network::ResourceRequest>();
//   resource_request->url = endpoint;
//   resource_request->method = method;
//   resource_request->credentials_mode = network::mojom::CredentialsMode::kOmit;
  
//   std::string clean_token;
//   base::TrimWhitespaceASCII(access_token, base::TRIM_ALL, &clean_token);
//   SetAuthorizationHeader(resource_request.get(), clean_token);

//   auto loader = network::SimpleURLLoader::Create(std::move(resource_request),
//                                                  kRagTrafficAnnotation);

//   if (payload) {
//     std::string json_body;
//     base::JSONWriter::Write(*payload, &json_body);
//     loader->AttachStringForUpload(json_body, "application/json");
//     VLOG(1) << "[RAG] Request Payload (" << endpoint << "):\n" << json_body;
//   }

//   loader->SetAllowHttpErrorResults(true);

//   auto* loader_ptr = loader.get();
//   loader_ptr->DownloadToString(
//     profile_->GetDefaultStoragePartition()
//         ->GetURLLoaderFactoryForBrowserProcess()
//         .get(),
//     base::BindOnce(
//         [](std::unique_ptr<network::SimpleURLLoader> loader_owner,
//            ApiCallback cb, std::unique_ptr<std::string> response_body) {
          
//           int response_code = 0;
//           if (loader_owner->ResponseInfo() && loader_owner->ResponseInfo()->headers) {
//               response_code = loader_owner->ResponseInfo()->headers->response_code();
//           }

//           if (!response_body) {
//             // [FIX] response_code is now used in this log statement
//             LOG(ERROR) << "[RAG] Network Error " << loader_owner->NetError() 
//                        << " (HTTP " << response_code << ") on " 
//                        << loader_owner->GetFinalURL();
//             std::move(cb).Run(std::nullopt);
//             return;
//           }

//           // Also useful for non-fatal error logging
//           if (response_code >= 400) {
//               VLOG(1) << "[RAG] API returned error code: " << response_code;
//           }
          
//           std::move(cb).Run(base::JSONReader::Read(*response_body, base::JSON_PARSE_RFC));
//         },
//         std::move(loader), std::move(client_callback)),
//     5 * 1024 * 1024); // 5MB Limit
// }

// void RagIngestionNetworkClient::SetAuthorizationHeader(
//     network::ResourceRequest* request,
//     const std::string& token) {
//   std::string value = base::StringPrintf("Bearer %s", token.c_str());
//   request->headers.SetHeader(net::HttpRequestHeaders::kAuthorization, value);
// }