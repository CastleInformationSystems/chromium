#include "ios/chrome/browser/jatter/jatter_firebase_client.h"

#include "base/i18n/time_formatting.h"
#include "base/json/json_reader.h"
#include "base/json/json_writer.h"
#include "base/logging.h"
#include "base/time/time.h"
#include "base/values.h"
#include "ios/chrome/browser/jatter/jatter_environment.h"
#include "ios/chrome/browser/jatter/jatter_token_storage.h"
#include "ios/chrome/browser/shared/model/profile/profile_ios.h"
#include "net/http/http_response_headers.h"
#include "net/traffic_annotation/network_traffic_annotation.h"
#include "services/network/public/cpp/resource_request.h"
#include "services/network/public/cpp/shared_url_loader_factory.h"
#include "services/network/public/mojom/url_response_head.mojom.h"

static const net::NetworkTrafficAnnotationTag kFirebaseFunctionAnnotation =
    net::DefineNetworkTrafficAnnotation("firebase_function_call", R"(
      semantics {
        sender: "Jatter Firebase function caller"
        description: "Invokes Firebase functions through https endpoints"
        trigger: "User navigates to a web page"
        data: "URL the user navigated to"
        destination: GOOGLE_OWNED_SERVICE
      }
      policy {
        cookies_allowed: NO
        setting: "Not user-configurable"
        policy_exception_justification: "Required for browser history RAG."
      }
    )");

JatterFirebaseClient::JatterFirebaseClient() {}
JatterFirebaseClient::~JatterFirebaseClient() {}

JatterFirebaseClient* JatterFirebaseClient::GetInstance() {
  return base::Singleton<JatterFirebaseClient>::get();
}

void JatterFirebaseClient::ObservePageVisit(ProfileIOS* profile,
                                            std::string url,
                                            std::string title) {
  JatterFirebaseClient::Invoke(
      profile,
      [profile, url, title]() {
        GURL endpoint(jatter::kLogWebPageVisitUrl);

        auto resource_request = std::make_unique<network::ResourceRequest>();
        resource_request->url = endpoint;
        resource_request->method = "POST";
        resource_request->headers.SetHeader("Content-Type", "application/json");
        JatterFirebaseClient::SetAuthorizationHeader(profile,
                                                     resource_request.get());

        base::DictValue metadata;
        metadata.Set("title", title);

        base::Time now = base::Time::Now();

        base::DictValue body;
        body.Set("date", base::TimeFormatAsIso8601(now));
        body.Set("url", url);
        body.Set("metadata", std::move(metadata));

        std::string json_body;
        base::JSONWriter::Write(body, &json_body);

        auto loader = network::SimpleURLLoader::Create(
            std::move(resource_request), kFirebaseFunctionAnnotation);
        loader->SetAllowHttpErrorResults(true);
        loader->AttachStringForUpload(json_body, "application/json");
        return loader;
      },
      base::BindOnce([](std::optional<std::string> response_body) {
        // Response handler callback
      }));
}

void JatterFirebaseClient::SetAuthorizationHeader(
    ProfileIOS* profile,
    network::ResourceRequest* resource_request) {
  JatterTokenStorage* storage = JatterTokenStorage::GetOrCreate(profile);
  std::string id_token = storage->GetIdToken();
#ifndef JATTER_PRODUCTION_MODE
  LOG(INFO) << "JatterFirebaseClient::SetAuthorizationHeader id_token: "
            << id_token;
#endif
  resource_request->headers.SetHeader("Authorization", "Bearer " + id_token);
}

void JatterFirebaseClient::Invoke(
    ProfileIOS* profile,
    JatterFirebaseClient::CreateUrlLoaderCallback url_loader_creator,
    JatterFirebaseClient::ResponseCallback callback) {
  std::unique_ptr<network::SimpleURLLoader> url_loader(url_loader_creator());
  network::SimpleURLLoader* loader_ptr = url_loader.get();

  auto url_loader_factory = profile->GetSharedURLLoaderFactory();

  loaders_.insert(std::move(url_loader));

  loader_ptr->DownloadToString(
      url_loader_factory.get(),
      base::BindOnce(&JatterFirebaseClient::OnResponseReceivedWithRetry,
                     base::Unretained(this), base::Unretained(loader_ptr),
                     base::Unretained(profile), url_loader_creator,
                     std::move(callback)),
      network::SimpleURLLoader::kMaxBoundedStringDownloadSize);
}

void JatterFirebaseClient::RefreshAuthToken(
    ProfileIOS* profile,
    JatterFirebaseClient::CreateUrlLoaderCallback url_loader_creator,
    JatterFirebaseClient::ResponseCallback callback) {
  JatterTokenStorage* storage = JatterTokenStorage::GetOrCreate(profile);
  std::string refresh_token = storage->GetRefreshToken();
#ifndef JATTER_PRODUCTION_MODE
  LOG(INFO) << "refresh_token RECALL: " << refresh_token;
#endif
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

  auto url_loader_factory = profile->GetSharedURLLoaderFactory();

  loader_ptr->DownloadToString(
      url_loader_factory.get(),
      base::BindOnce(&JatterFirebaseClient::OnAuthenticationResponseReceived,
                     base::Unretained(this), base::Unretained(loader_ptr),
                     base::Unretained(profile), url_loader_creator,
                     std::move(callback)),
      1024 * 1024);
}

void JatterFirebaseClient::OnResponseReceivedWithRetry(
    network::SimpleURLLoader* loader,
    ProfileIOS* profile,
    JatterFirebaseClient::CreateUrlLoaderCallback url_loader_creator,
    JatterFirebaseClient::ResponseCallback callback,
    std::optional<std::string> response_body) {
  bool did_attempt_refresh = false;

  int response_code = 0;
  if (loader && loader->ResponseInfo() && loader->ResponseInfo()->headers) {
    response_code = loader->ResponseInfo()->headers->response_code();
  }

  bool needs_refresh = false;
  if (response_code == 401 || response_code == 403 || response_code == 500) {
    needs_refresh = true;
  }

  if (!needs_refresh && response_body) {
    std::optional<base::Value> value = 
        base::JSONReader::Read(*response_body, base::JSON_PARSE_CHROMIUM_EXTENSIONS);

    if (value && value->is_dict()) {
      const std::string* status = value->GetDict().FindString("status");
      if (status && *status == "UNAUTHENTICATED") {
        needs_refresh = true;
      }
    }
  }

  if (needs_refresh) {
#ifndef JATTER_PRODUCTION_MODE
    LOG(INFO) << "[RAG] Auth failure detected (HTTP " << response_code 
              << "). Attempting token refresh...";
#endif
    did_attempt_refresh = true;
    RefreshAuthToken(profile, url_loader_creator, std::move(callback));
  }

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
    ProfileIOS* profile,
    JatterFirebaseClient::CreateUrlLoaderCallback url_loader_creator,
    JatterFirebaseClient::ResponseCallback callback,
    std::optional<std::string> response_body) {
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

        auto url_loader_factory = profile->GetSharedURLLoaderFactory();

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
#ifndef JATTER_PRODUCTION_MODE
    LOG(ERROR) << "Failed to get Firebase auth token refresh response";
#endif
    auto error_message = std::make_optional<std::string>("Error authenticating");
    std::move(callback).Run(error_message);
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
