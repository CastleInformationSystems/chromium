#include "chrome/browser/jatter/jatter_firebase_client.h"

#include "base/i18n/time_formatting.h"
#include "base/json/json_reader.h"
#include "base/json/json_writer.h"
#include "base/memory/singleton.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/string_util.h"
#include "base/strings/utf_string_conversions.h"
#include "base/time/time.h"
#include "base/uuid.h"
#include "chrome/browser/browser_process.h"
#include "chrome/browser/history/history_service_factory.h"
#include "chrome/browser/jatter/jatter_environment.h"
#include "chrome/browser/jatter/jatter_token_storage.h"
#include "chrome/browser/jatter/encryption/jatter_encryptor.h"
#include "chrome/browser/profiles/profile.h"
#include "components/history/core/browser/history_service.h"
#include "components/prefs/pref_service.h"
#include "components/pref_registry/pref_registry_syncable.h"
#include "content/public/browser/browser_context.h"
#include "content/public/browser/render_process_host.h"
#include "content/public/browser/storage_partition.h"
#include "net/http/http_response_headers.h"
#include "net/traffic_annotation/network_traffic_annotation.h"
#include "services/network/public/cpp/resource_request.h"
#include "services/network/public/mojom/url_response_head.mojom.h"

static const net::NetworkTrafficAnnotationTag kFirebaseFunctionAnnotation =
    net::DefineNetworkTrafficAnnotation("firebase_function_call",
                                        R"(
      semantics {
        sender: "Jatter Firebase function caller"
        description: "Invokes Firebase functions through https endpoints"
        trigger: "User navigates to a web page"
        data: "URL the user naviaged to"
        destination: GOOGLE_OWNED_SERVICE
      }
      policy {
        cookies_allowed: NO
        setting: "Not user-configurable"
        policy_exception_justification: "Required for browser history RAG."
      }
    )");

const char kRagIngestionProfileGuidPref[] = "rag_ingestion.profile_guid";

// static
void JatterFirebaseClient::RegisterProfilePrefs(user_prefs::PrefRegistrySyncable* registry) {
  // Registers the pref with an empty string as the default value
  registry->RegisterStringPref(kRagIngestionProfileGuidPref, "");
}

// Helper to construct the JSON payload per the API spec, fully utilizing HISTORY DB columns
base::DictValue BuildVisitDict(int64_t visit_id,
                                 const std::string& url_string, 
                                 const std::string& title,
                                 int visit_count,
                                 int typed_count,
                                 double last_visit_time_micros,
                                 double visit_time_micros,
                                 int64_t from_visit,
                                 const std::string& external_referrer_url,
                                 double visit_duration_micros,
                                 base::DictValue metadata,
                                 const std::optional<std::string>& private_key) {
  
  base::DictValue url_dict;
  url_dict.Set("url", url_string);
  url_dict.Set("title", title);
  url_dict.Set("visitCount", visit_count);
  url_dict.Set("typedCount", typed_count);
  url_dict.Set("lastVisitTime", last_visit_time_micros);
  url_dict.Set("metadata", metadata.Clone());

  if (private_key) {
    std::string unencrypted_url_json;
    base::JSONWriter::Write(url_dict, &unencrypted_url_json);
    url_dict.Set("encrypted", JatterEncryptor::Encrypt(unencrypted_url_json, *private_key));
  }

  base::DictValue visit_dict;
  visit_dict.Set("id", base::NumberToString(visit_id));
  visit_dict.Set("url", std::move(url_dict));
  visit_dict.Set("visitTime", visit_time_micros);
  
  std::string from_visit_str = (from_visit != 0) ? base::NumberToString(from_visit) : "";
  visit_dict.Set("fromVisit", from_visit_str);
  visit_dict.Set("external_referrer_url", external_referrer_url);
  visit_dict.Set("visitDuration", visit_duration_micros);
  
  if (private_key) {
    base::DictValue visit_encrypt_payload;
    visit_encrypt_payload.Set("url", url_string);
    visit_encrypt_payload.Set("external_referrer_url", external_referrer_url);
    
    std::string unencrypted_visit_json;
    base::JSONWriter::Write(visit_encrypt_payload, &unencrypted_visit_json);
    visit_dict.Set("encrypted", JatterEncryptor::Encrypt(unencrypted_visit_json, *private_key));
  }

  return visit_dict;
}

JatterFirebaseClient::JatterFirebaseClient(Profile* profile) 
    : profile_(profile) {
  
  // Check if auth is already established from a previous session
  JatterTokenStorage* storage = JatterTokenStorage::GetOrCreate(profile_);
  if (!storage->GetIdToken().empty() || !storage->GetRefreshToken().empty()) {
    is_auth_ready_ = true;
  }

  // Automatically start observing history the moment the Profile creates this service
  history::HistoryService* history_service = HistoryServiceFactory::GetForProfile(
      profile_, ServiceAccessType::EXPLICIT_ACCESS);
  if (history_service && !history_observation_.IsObserving()) {
    history_observation_.Observe(history_service);
  }
}

JatterFirebaseClient::~JatterFirebaseClient() = default;

void JatterFirebaseClient::SetAuthReady() {
  is_auth_ready_ = true;
  
  // Flush all parked requests
  for (auto& task : pending_network_tasks_) {
    std::move(task).Run();
  }
  pending_network_tasks_.clear();
}

void JatterFirebaseClient::ExecuteNetworkRequest(Profile* profile, std::string json_body) {
  JatterFirebaseClient::Invoke(
      profile,
      [profile, json_body]() {
        GURL endpoint(jatter::kLogWebPageVisitUrl);
        auto request = std::make_unique<network::ResourceRequest>();
        request->url = endpoint;
        request->method = "POST";
        request->headers.SetHeader("Content-Type", "application/json");
        JatterFirebaseClient::SetAuthorizationnHeader(profile, request.get());

        auto loader = network::SimpleURLLoader::Create(std::move(request), kFirebaseFunctionAnnotation);
        loader->SetAllowHttpErrorResults(true);
        loader->AttachStringForUpload(json_body, "application/json");
        return loader;
      },
      // Callback is empty now because RefreshPersonalizedAssistance was removed
      base::BindOnce([](std::optional<std::string> response_body) {}));
}

void JatterFirebaseClient::OnURLVisited(history::HistoryService* history_service,
                                        const history::VisitedURLInfo& visited_url_info) {
  const history::URLRow& url_row = visited_url_info.url_row;
  const history::VisitRow& visit_row = visited_url_info.visit_row;

  // 1. Ignore subframes
  if (!ui::PageTransitionIsMainFrame(visit_row.transition)) {
    return;
  }

  // 2. Ignore internal Jatter/NTP pages
  if (base::StartsWith(url_row.url().spec(), jatter::kAppUrl, 
                       base::CompareCase::INSENSITIVE_ASCII)) {
    return;
  }

  Profile* profile = profile_;
  if (!profile) return;
  
  std::string profile_guid;
  PrefService* prefs = profile->GetPrefs();
  if (prefs) {
    profile_guid = prefs->GetString(kRagIngestionProfileGuidPref);
    if (profile_guid.empty()) {
      profile_guid = base::Uuid::GenerateRandomV4().AsLowercaseString();
      prefs->SetString(kRagIngestionProfileGuidPref, profile_guid);
    }
  }

  JatterTokenStorage* storage = JatterTokenStorage::GetOrCreate(profile);
  std::optional<std::string> private_key = storage->GetPrivateKey();
  std::optional<std::string> signature = storage->GetPrivateKeySignature();

  double visit_time_micros = static_cast<double>((visit_row.visit_time - base::Time::UnixEpoch()).InMicroseconds());
  double last_visit_time_micros = static_cast<double>((url_row.last_visit() - base::Time::UnixEpoch()).InMicroseconds());
  double visit_duration_micros = static_cast<double>(visit_row.visit_duration.InMicroseconds());

  base::ListValue visits_list;
  visits_list.Append(BuildVisitDict(
      visit_row.visit_id, 
      url_row.url().spec(), 
      base::UTF16ToUTF8(url_row.title()), 
      url_row.visit_count(), 
      url_row.typed_count(), 
      last_visit_time_micros, 
      visit_time_micros, 
      visit_row.referring_visit, 
      visit_row.external_referrer_url.spec(),    
      visit_duration_micros,                     
      base::DictValue(),
      private_key
  ));

  base::DictValue body;
  body.Set("profileGuid", profile_guid);
  if (signature) {
    body.Set("keySignature", *signature);
  }
  body.Set("visits", std::move(visits_list));

  // Serialize JSON *before* the lambda to keep the lambda entirely copyable
  std::string json_body;
  base::JSONWriter::Write(body, &json_body);

  ExecuteNetworkRequest(profile, std::move(json_body));
}

void JatterFirebaseClient::LogBulkWebPageVisits(Profile* profile, base::ListValue visits_list) {
  if (visits_list.empty()) return;

  std::string profile_guid;
  PrefService* prefs = profile->GetPrefs();
  if (prefs) {
    profile_guid = prefs->GetString(kRagIngestionProfileGuidPref);
    if (profile_guid.empty()) {
      profile_guid = base::Uuid::GenerateRandomV4().AsLowercaseString();
      prefs->SetString(kRagIngestionProfileGuidPref, profile_guid);
    }
  }

  JatterTokenStorage* storage = JatterTokenStorage::GetOrCreate(profile);
  std::optional<std::string> signature = storage->GetPrivateKeySignature();
  std::optional<std::string> private_key = storage->GetPrivateKey();

  // Enforce 2,000 maximum batch limit per spec
  const size_t kMaxBatchSize = 2000;
  base::ListValue current_batch;
  
  for (auto& visit_val : visits_list) {
    if (!visit_val.is_dict()) continue;
    
    // Grab the mutable dictionary reference
    base::DictValue& visit_dict = visit_val.GetDict();
    
    if (private_key) {
      // 1. Encrypt the inner "url" dict
      base::DictValue* url_dict = visit_dict.FindDict("url");
      if (url_dict) {
        std::string unencrypted_url_json;
        base::JSONWriter::Write(*url_dict, &unencrypted_url_json);
        url_dict->Set("encrypted", JatterEncryptor::Encrypt(unencrypted_url_json, *private_key));
      }

      // 2. Encrypt the visit itself
      base::DictValue visit_encrypt_payload;
      
      if (url_dict) {
        if (const std::string* visit_url = url_dict->FindString("url")) {
          visit_encrypt_payload.Set("url", *visit_url);
        }
      }
      
      if (const std::string* ext_ref = visit_dict.FindString("external_referrer_url")) {
        visit_encrypt_payload.Set("external_referrer_url", *ext_ref);
      }
      
      std::string unencrypted_visit_json;
      base::JSONWriter::Write(visit_encrypt_payload, &unencrypted_visit_json);
      visit_dict.Set("encrypted", JatterEncryptor::Encrypt(unencrypted_visit_json, *private_key));
    }

    current_batch.Append(std::move(visit_val));
    
    if (current_batch.size() >= kMaxBatchSize) {
      base::DictValue body;
      body.Set("profileGuid", profile_guid);
      if (signature) {
        body.Set("keySignature", *signature);
      }
      body.Set("visits", std::move(current_batch));

      std::string json_body;
      base::JSONWriter::Write(body, &json_body);

      base::OnceClosure task = base::BindOnce(
          &JatterFirebaseClient::ExecuteNetworkRequest,
          base::Unretained(this), profile, std::move(json_body));

      if (is_auth_ready_) std::move(task).Run();
      else pending_network_tasks_.push_back(std::move(task));

      current_batch = base::ListValue();
    }
  }

  // Flush remaining visits
  if (!current_batch.empty()) {
    base::DictValue body;
    body.Set("profileGuid", profile_guid);
    if (signature) {
      body.Set("keySignature", *signature);
    }
    body.Set("visits", std::move(current_batch));

    std::string json_body;
    base::JSONWriter::Write(body, &json_body);

    base::OnceClosure task = base::BindOnce(
        &JatterFirebaseClient::ExecuteNetworkRequest,
        base::Unretained(this), profile, std::move(json_body));

    if (is_auth_ready_) std::move(task).Run();
    else pending_network_tasks_.push_back(std::move(task));
  }
}

void JatterFirebaseClient::SetAuthorizationnHeader(
    Profile* profile,
    network::ResourceRequest* resource_request) {
  JatterTokenStorage* storage = JatterTokenStorage::GetOrCreate(profile);
  std::string id_token = storage->GetIdToken();
  resource_request->headers.SetHeader("Authorization", "Bearer " + id_token);
}

void JatterFirebaseClient::Invoke(
    Profile* profile,
    JatterFirebaseClient::CreateUrlLoaderCallback url_loader_creator,
    JatterFirebaseClient::ResponseCallback callback) {
  
  // Guard: Abort immediately if the profile has no auth token
  JatterTokenStorage* storage = JatterTokenStorage::GetOrCreate(profile);
  if (storage->GetIdToken().empty()) {
    // If we lack an ID token but have a refresh token, try to reinit the session
    if (!storage->GetRefreshToken().empty()) {
      RefreshAuthToken(profile, url_loader_creator, std::move(callback));
      return;
    }

    // If BOTH are empty, this is an unauthenticated profile. Abort safely.
    std::move(callback).Run(std::nullopt);
    return;
  }

  std::unique_ptr<network::SimpleURLLoader> url_loader(url_loader_creator());
  network::SimpleURLLoader* loader_ptr = url_loader.get();

  auto url_loader_factory = profile->GetDefaultStoragePartition()
                                ->GetURLLoaderFactoryForBrowserProcess();

  loaders_.insert(std::move(url_loader));

  loader_ptr->DownloadToString(
      url_loader_factory.get(),
      base::BindOnce(&JatterFirebaseClient::OnResponseReceivedWithRetry,
                     base::Unretained(this), base::Unretained(loader_ptr),
                     profile->GetWeakPtr(), url_loader_creator,
                     std::move(callback)),
      network::SimpleURLLoader::kMaxBoundedStringDownloadSize);
}

void JatterFirebaseClient::RefreshAuthToken(
    Profile* profile,
    JatterFirebaseClient::CreateUrlLoaderCallback url_loader_creator,
    JatterFirebaseClient::ResponseCallback callback) {
  JatterTokenStorage* storage = JatterTokenStorage::GetOrCreate(profile);
  std::string refresh_token = storage->GetRefreshToken();

  // Guard: Abort immediately if there is no refresh token
  if (refresh_token.empty()) {
    LOG(ERROR) << "No refresh token available to refresh auth.";
    std::move(callback).Run(std::nullopt);
    return;
  }

  GURL url(std::string("https://securetoken.googleapis.com/v1/token?key=") +
           jatter::kFirebaseApiKey);

  auto resource_request = std::make_unique<network::ResourceRequest>();
  resource_request->url = url;
  resource_request->method = "POST";
  resource_request->headers.SetHeader("Content-Type",
                                      "application/x-www-form-urlencoded");

  std::string body = "grant_type=refresh_token&refresh_token=" + refresh_token;

  auto url_loader = network::SimpleURLLoader::Create(
      std::move(resource_request), kFirebaseFunctionAnnotation);
  network::SimpleURLLoader* loader_ptr = url_loader.get();

  loaders_.insert(std::move(url_loader));
  loader_ptr->AttachStringForUpload(body, "application/x-www-form-urlencoded");

  auto url_loader_factory = profile->GetDefaultStoragePartition()
                                ->GetURLLoaderFactoryForBrowserProcess();

  loader_ptr->DownloadToString(
      url_loader_factory.get(),
      base::BindOnce(&JatterFirebaseClient::OnAuthenticationResponseReceived,
                     base::Unretained(this), base::Unretained(loader_ptr),
                     profile->GetWeakPtr(), url_loader_creator,
                     std::move(callback)),
      1024 * 1024);
}

void JatterFirebaseClient::OnResponseReceivedWithRetry(
    network::SimpleURLLoader* loader,
    base::WeakPtr<Profile> weak_profile,
    JatterFirebaseClient::CreateUrlLoaderCallback url_loader_creator,
    JatterFirebaseClient::ResponseCallback callback,
    std::optional<std::string> response_body) {
  // Guard: Safely exit if the profile was destroyed
  if (!weak_profile) {
    RemoveUrlLoader(loader);
    return;
  }
  Profile* profile = weak_profile.get();

  bool did_attempt_refresh = false;

  // 1. Grab the actual HTTP Status Code from the network layer
  int response_code = 0;
  if (loader && loader->ResponseInfo() && loader->ResponseInfo()->headers) {
    response_code = loader->ResponseInfo()->headers->response_code();
  }

  // 2. Determine if we need to refresh the token
  bool needs_refresh = false;

  // Trigger on known auth errors, OR 500 errors (since the backend crashes on expired tokens)
  if (response_code == 401 || response_code == 403 || response_code == 500) {
      needs_refresh = true;
  }

  // 3. Fallback: Keep the JSON check just in case the server ever returns a 200 OK 
  // with a JSON payload of {"status": "UNAUTHENTICATED"}
  if (!needs_refresh && response_body) {
    std::optional<base::Value> value = 
        base::JSONReader::Read(*response_body, base::JSON_PARSE_CHROMIUM_EXTENSIONS);

    if (value && value->is_dict()) {
      const std::string* status = value->GetDict().FindString("status");
      // Safely check if pointer is valid before dereferencing
      if (status && *status == "UNAUTHENTICATED") {
        needs_refresh = true;
      }
    }
  }

  // 4. Execute the Refresh
  if (needs_refresh) {
    LOG(INFO) << "[RAG] Auth failure detected (HTTP " << response_code 
              << "). Attempting token refresh...";
    
    did_attempt_refresh = true;
    RefreshAuthToken(profile, url_loader_creator, std::move(callback));
  }

  // 5. If no refresh was needed, pass the data back to the caller normally
  if (!did_attempt_refresh) {
    std::optional<std::string> result;
    if (response_body) {
      result = std::move(*response_body);
    }
    std::move(callback).Run(result);
  }

  RemoveUrlLoader(loader);
}

void JatterFirebaseClient::OnResponseReceived(
    network::SimpleURLLoader* loader,
    JatterFirebaseClient::ResponseCallback callback,
    std::optional<std::string> response_body) {
  std::optional<std::string> result;
  if (response_body) {
    result = std::move(*response_body);
  }
  std::move(callback).Run(result);
  RemoveUrlLoader(loader);
}

void JatterFirebaseClient::OnAuthenticationResponseReceived(
    network::SimpleURLLoader* loader,
    base::WeakPtr<Profile> weak_profile,
    JatterFirebaseClient::CreateUrlLoaderCallback url_loader_creator,
    JatterFirebaseClient::ResponseCallback callback,
    std::optional<std::string> response_body) {
  if (!weak_profile) {
    RemoveUrlLoader(loader);
    return;
  }
  Profile* profile = weak_profile.get();

  bool success = false;

  if (response_body) {
    std::optional<base::Value> value = 
        base::JSONReader::Read(*response_body, base::JSON_PARSE_CHROMIUM_EXTENSIONS);

    if (value && value->is_dict()) {
      const base::DictValue& dict = value->GetDict();
      const std::string* id_token = dict.FindString("id_token");
      const std::string* refresh_token = dict.FindString("refresh_token");
      if (id_token && refresh_token) {
        success = true;

        JatterTokenStorage* storage = JatterTokenStorage::GetOrCreate(profile);
        storage->SetTokens(*id_token, *refresh_token);

        auto url_loader = url_loader_creator();
        network::SimpleURLLoader* loader_ptr = url_loader.get();

        loaders_.insert(std::move(url_loader));
        auto url_loader_factory = profile->GetDefaultStoragePartition()
                                      ->GetURLLoaderFactoryForBrowserProcess();

        loader_ptr->DownloadToString(
            url_loader_factory.get(),
            base::BindOnce(&JatterFirebaseClient::OnResponseReceived,
                           base::Unretained(this), base::Unretained(loader_ptr),
                           std::move(callback)),
            1024 * 1024);
      }
    }
  }

  if (!success) {
    std::move(callback).Run(std::make_optional<std::string>("Error authenticating"));
  }

  RemoveUrlLoader(loader);
}

void JatterFirebaseClient::RemoveUrlLoader(network::SimpleURLLoader* loader) {
  auto it = std::find_if(
      loaders_.begin(), loaders_.end(),
      [loader](const std::unique_ptr<network::SimpleURLLoader>& ptr) {
        return ptr.get() == loader;
      });
  if (it != loaders_.end()) {
    loaders_.erase(it);
  }
}
