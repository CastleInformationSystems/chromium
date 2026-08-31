// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef IOS_CHROME_BROWSER_JATTER_RAG_INGESTION_NETWORK_CLIENT_H_
#define IOS_CHROME_BROWSER_JATTER_RAG_INGESTION_NETWORK_CLIENT_H_

#include <optional>
#include <string>
#include <vector>

#include "base/functional/callback.h"
#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "base/values.h"
#include "url/gurl.h"

class ProfileIOS;

extern const char kRagIngestionProfileGuidPref[];

// Data structure matching the 'SetIngestionPermissionParams' TS definition.
struct RagSiteMetadata {
  RagSiteMetadata();
  RagSiteMetadata(const RagSiteMetadata&);
  RagSiteMetadata(RagSiteMetadata&&);
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

// Handles all raw HTTP communication with the RAG Server for iOS.
class RagIngestionNetworkClient {
 public:
  using ApiCallback = base::OnceCallback<void(std::optional<base::Value>)>;

  explicit RagIngestionNetworkClient(ProfileIOS* profile);
  ~RagIngestionNetworkClient();

  // API: POST /getIngestionPermission
  void CheckDomainPermission(const GURL& url, ApiCallback callback);

  // API: POST /setIngestionPermission
  void SetIngestionPermission(const GURL& url, 
                              RagPermissionStatus permission,
                              const RagSiteMetadata& metadata);

  // API: POST /chunkDocument
  void ChunkDocument(const GURL& url, 
                     const std::string& canonical_host,
                     const std::string& title,
                     const std::string& content, 
                     ApiCallback callback);

  // API: POST /embedChunks
  void EmbedChunks(const std::vector<std::string>& chunks, ApiCallback callback);

  // API: POST /ingestResource
  void IngestDocument(const GURL& url,
                      const std::vector<std::string>& encrypted_chunks,
                      const std::vector<std::vector<double>>& vectors,
                      base::OnceClosure callback);

  // API: POST /ingestDocument (Multipart upload)
  void UploadRawDocument(const GURL& url,
                         const std::string& page_title,
                         const std::string& file_bytes,
                         const std::string& mime_type,
                         const std::string& filename,
                         ApiCallback callback);

  // API: POST /ingestEncryptedDocumentText
  void IngestEncryptedDocument(const GURL& url,
                               base::DictValue document_text_json,
                               base::OnceClosure callback);

 private:
  std::string GetProfileGuid() const;
  
  // Base request for JSON
  void MakeAuthorizedRequest(
      const GURL& endpoint,
      const std::string& method,
      std::optional<base::DictValue> payload,
      ApiCallback callback);
  
  // Core request for Strings / Multipart
  void MakeAuthorizedRequest(
      const GURL& endpoint,
      const std::string& method,
      const std::string& payload,
      const std::string& content_type,
      ApiCallback callback);

  raw_ptr<ProfileIOS> profile_;
  base::WeakPtrFactory<RagIngestionNetworkClient> weak_factory_{this};
};

#endif  // IOS_CHROME_BROWSER_JATTER_RAG_INGESTION_NETWORK_CLIENT_H_