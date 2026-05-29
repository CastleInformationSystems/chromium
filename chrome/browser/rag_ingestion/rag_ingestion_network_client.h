// Copyright 2024 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_RAG_INGESTION_RAG_INGESTION_NETWORK_CLIENT_H_
#define CHROME_BROWSER_RAG_INGESTION_RAG_INGESTION_NETWORK_CLIENT_H_

#include <optional>
#include <string>
#include <vector>

#include "base/functional/callback.h"
#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "base/values.h"
#include "url/gurl.h"

namespace network {
class SimpleURLLoader;
struct ResourceRequest;
}  // namespace network

class Profile;

// Data structure matching the 'SetIngestionPermissionParams' TS definition.
struct RagSiteMetadata {
  // [ADD] Explicit declarations
  RagSiteMetadata();
  RagSiteMetadata(const RagSiteMetadata&); // Copy constructor
  RagSiteMetadata(RagSiteMetadata&&);      // Move constructor
  ~RagSiteMetadata();

  RagSiteMetadata& operator=(const RagSiteMetadata&);
  RagSiteMetadata& operator=(RagSiteMetadata&&);

  std::string site_name;
  std::string title;
  std::string description;
  std::string keywords;
  std::string icon_base64;
  std::string icon_mime_type;
};

// Enum matching 'IngestionPermission' TS definition.
enum class RagPermissionStatus {
  kAllowed,
  kDenied,
  kUndecided
};
// Handles all raw HTTP communication with the RAG Server.
// Uses JatterTokenStorage for Auth headers and PrefService for persistent device ID.
class RagIngestionNetworkClient {
 public:
  using ApiCallback = base::OnceCallback<void(std::optional<base::Value>)>;

  explicit RagIngestionNetworkClient(Profile* profile);
  ~RagIngestionNetworkClient();

  // --- Flow A APIs (Discovery & Permissions) ---
  
  // [UPDATED] Takes GURL to calculate 'siteId'
  // API: POST /getIngestionPermission
  void CheckDomainPermission(const GURL& url, ApiCallback callback);

  // API: POST /setIngestionPermission
  // Sends the user's decision + metadata to the backend.
  void SetIngestionPermission(const GURL& url, 
                              RagPermissionStatus permission,
                              const RagSiteMetadata& metadata);

  // //  Matches PDF "ragIngestionAddUrl"
  // // Replaces RegisterPageDiscovery.
  // void AddIngestionUrl(const GURL& url, base::OnceClosure callback);

  // // --- Flow B APIs (Background Ingestion) ---

  // //  Handshake signal called at startup and periodically.
  // // API: POST /ingest
  // void TriggerIngestion(ApiCallback callback);
  
  // // Polls for a new job.
  // // API: POST /GetIngestionMessages
  // void GetIngestionMessages(ApiCallback callback);
  
  // // [UPDATED] Matches server 'ResolveIngestionMessageParams'
  // // API: POST /resolveIngestionMessage
  // void ResolveIngestionMessage(const std::string& message_id, 
  //                              const std::string& html,
  //                              const std::string& inner_text,
  //                              bool is_authenticated);

  // --- Flow C APIs (Passive Learning) ---

  // API: POST /chunkDocument
  // Sends the scraped inner text to the backend to be split into chunks.
  void ChunkDocument(const GURL& url, const std::string& content, ApiCallback callback);

  // API: POST /embedChunks
  // Sends the text chunks to be converted into numerical vectors.
  void EmbedChunks(const std::vector<std::string>& chunks, ApiCallback callback);

  // API: POST /ingestDocument
  // Submits the final encrypted chunks and their corresponding vectors to the backend.
  void IngestDocument(const GURL& url,
                      const std::vector<std::string>& encrypted_chunks,
                      const std::vector<std::vector<double>>& vectors,
                      base::OnceClosure callback);
  
  // API: POST /ingestDocument
  // Submits the raw file (MHTML, PDF, DOCX, etc.) as multipart/form-data.
  void UploadRawDocument(const GURL& url,
                         const std::string& page_title,
                         const std::string& file_bytes,
                         const std::string& mime_type,
                         const std::string& filename,
                         ApiCallback callback);

  // API: POST /ingestEncryptedDocumentText
  // Submits the modified DocumentText JSON containing the encrypted text records.
  void IngestEncryptedDocument(const GURL& url,
                               base::DictValue document_text_json,
                               base::OnceClosure callback);

 private:
  // Helper to get or generate the persistent GUID for this profile/device.
  std::string GetProfileGuid() const;

  // Helper to match server's siteId logic (MD5 of Origin)
  std::string GenerateSiteId(const GURL& url) const;

  void MakeAuthorizedRequest(
      const GURL& endpoint,
      const std::string& method,
      std::optional<base::DictValue> payload,
      ApiCallback callback);
  
  // Core Overload: Accepts raw string payloads and custom Content-Types
  void MakeAuthorizedRequest(
      const GURL& endpoint,
      const std::string& method,
      const std::string& payload,
      const std::string& content_type,
      ApiCallback callback);

  // void OnTokenFetched(
  //     const GURL& endpoint,
  //     const std::string& method,
  //     std::optional<base::DictValue> payload,
  //     ApiCallback client_callback,
  //     const std::string& access_token);

  // void SetAuthorizationHeader(network::ResourceRequest* request, 
  //                             const std::string& token);

  raw_ptr<Profile> profile_;
  base::WeakPtrFactory<RagIngestionNetworkClient> weak_factory_{this};
};

#endif  // CHROME_BROWSER_RAG_INGESTION_RAG_INGESTION_NETWORK_CLIENT_H_