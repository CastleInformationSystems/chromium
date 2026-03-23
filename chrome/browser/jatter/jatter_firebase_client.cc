#include "chrome/browser/jatter/jatter_firebase_client.h"

#include "base/i18n/time_formatting.h"
#include "base/json/json_reader.h"
#include "base/json/json_writer.h"
#include "base/memory/singleton.h"
#include "base/time/time.h"
#include "chrome/browser/browser_process.h"
#include "chrome/browser/jatter/jatter_environment.h"
#include "chrome/browser/jatter/jatter_token_storage.h"
#include "chrome/browser/profiles/profile.h"
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

JatterFirebaseClient::JatterFirebaseClient() {}
JatterFirebaseClient::~JatterFirebaseClient() {}

JatterFirebaseClient* JatterFirebaseClient::GetInstance() {
  return base::Singleton<JatterFirebaseClient>::get();
}

void JatterFirebaseClient::ObservePageVisit(Profile* profile,
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
        JatterFirebaseClient::SetAuthorizationnHeader(profile,
                                                      resource_request.get());

        base::Value::Dict metadata;
        metadata.Set("title", title);

        base::Time now = base::Time::Now();

        base::Value::Dict body;
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

      }));
}

void JatterFirebaseClient::SetAuthorizationnHeader(
    Profile* profile,
    network::ResourceRequest* resource_request) {
  JatterTokenStorage* storage = JatterTokenStorage::GetOrCreate(profile);
  std::string id_token = storage->GetIdToken();
  LOG(INFO) << "JatterFirebaseClient::SetAuthenticationHeader id_token: "
            << id_token;

  resource_request->headers.SetHeader("Authorization", "Bearer " + id_token);
}

void JatterFirebaseClient::Invoke(
    Profile* profile,
    JatterFirebaseClient::CreateUrlLoaderCallback url_loader_creator,
    JatterFirebaseClient::ResponseCallback callback) {
  std::unique_ptr<network::SimpleURLLoader> url_loader(url_loader_creator());
  network::SimpleURLLoader* loader_ptr = url_loader.get();

  auto url_loader_factory = profile->GetDefaultStoragePartition()
                                ->GetURLLoaderFactoryForBrowserProcess();
  LOG(INFO) << "JatterFirebaseClient::Invoke";

  // Store the loader to keep it alive
  loaders_.insert(std::move(url_loader));

  loader_ptr->DownloadToString(
      url_loader_factory.get(),
      base::BindOnce(&JatterFirebaseClient::OnResponseReceivedWithRetry,
                     base::Unretained(this), base::Unretained(loader_ptr),
                     base::Unretained(profile), url_loader_creator,
                     std::move(callback)),
      network::SimpleURLLoader::kMaxBoundedStringDownloadSize);
}

// loader->DownloadToString(
//       url_loader_factory.get(),
//       base::BindOnce(
//           [](Profile* profile,
//              std::function<std::unique_ptr<network::SimpleURLLoader>()>
//                  loader_factory,
//              std::function<void(std::unique_ptr<std::string> response_body)>
//                  callback,
//              std::unique_ptr<std::string> response_body) {
//             bool did_attempt_refresh = false;

//             if (response_body) {
//               LOG(INFO) << "JatterFirebaseClient::Invoke response: "
//                         << *response_body;

//               std::optional<base::Value> value =
//                   base::JSONReader::Read(*response_body, 
//                                          base::JSON_PARSE_CHROMIUM_EXTENSIONS);

//               if (value && value->is_dict()) {
//                 const base::Value::Dict& dict = value->GetDict();
//                 const std::string* status = dict.FindString("status");
//                 if (status) {
//                   if (*status == "UNAUTHENTICATED") {
//                     LOG(INFO) << "Refreshing token";

//                     did_attempt_refresh = true;

//                     base::OnceCallback<void(bool)> once_callback =
//                         base::BindOnce(
//                             [](Profile* profile,
//                                std::function<std::unique_ptr<
//                                    network::SimpleURLLoader>()>
//                                    loader_factory,
//                                std::function<void(std::unique_ptr<std::string>)>
//                                    callback,
//                                bool success) {
//                               if (success) {
//                                 auto loader = loader_factory();

//                                 auto url_loader_factory =
//                                     profile->GetDefaultStoragePartition()
//                                         ->GetURLLoaderFactoryForBrowserProcess();

//                                 auto once_callback = base::BindOnce(
//                                     [](std::function<void(
//                                            std::unique_ptr<std::string>)>
//                                            callback,
//                                        std::optional<std::string>
//                                            response_body) {
//                                       if (response_body) {
//                                         callback(std::make_unique<std::string>(
//                                             *response_body));
//                                       } else {
//                                         callback(nullptr);
//                                       }
//                                     },
//                                     std::move(callback));

//                                 loader->DownloadToString(
//                                     url_loader_factory.get(),
//                                     std::move(once_callback), 1024 * 1024);
//                               } else {
//                                 auto foobar = std::make_unique<std::string>(
//                                     "Error authenticating");
//                                 callback(std::move(foobar));
//                               }
//                             },
//                             profile, loader_factory, callback);

//                     JatterFirebaseClient::RefreshAuthToken(
//                         profile, std::move(once_callback));
//                   }
//                 }
//               }
//             } else {
//               LOG(ERROR)
//                   << "JatterFirebaseClient::Invoke Failed to get response";
//             }

//             if (!did_attempt_refresh) {
//               callback(std::move(response_body));
//             }
//           },
//           profile, loader_factory, callback),
//       1024 * 1024);

void JatterFirebaseClient::RefreshAuthToken(
    Profile* profile,
    JatterFirebaseClient::CreateUrlLoaderCallback url_loader_creator,
    JatterFirebaseClient::ResponseCallback callback) {
  JatterTokenStorage* storage = JatterTokenStorage::GetOrCreate(profile);
  std::string refresh_token = storage->GetRefreshToken();
  LOG(INFO) << "refresh_token RECALL: " << refresh_token;

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

  // Store the loader to keep it alive
  loaders_.insert(std::move(url_loader));

  loader_ptr->AttachStringForUpload(body, "application/x-www-form-urlencoded");

  auto url_loader_factory = profile->GetDefaultStoragePartition()
                                ->GetURLLoaderFactoryForBrowserProcess();

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
    Profile* profile,
    JatterFirebaseClient::CreateUrlLoaderCallback url_loader_creator,
    JatterFirebaseClient::ResponseCallback callback,
    std::unique_ptr<std::string> response_body) {
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
    std::unique_ptr<std::string> response_body) {
  std::optional<std::string> result;
  if (response_body) {
    result = std::move(*response_body);
  }

  std::move(callback).Run(result);

  RemoveUrlLoader(loader);
}

// void OnAuthenticationResponseReceived(
//     network::SimpleURLLoader* loader,
//     Profile* profile,
//     CreateUrlLoaderCallback url_loader_creator,
//     ResponseCallback callback,
//     bool success) {
//   if (success) {
//     auto url_loader = url_loader_creator();
//     network::SimpleURLLoader* loader_ptr = url_loader.get();

//     // Store the loader to keep it alive
//     loaders_.insert(std::move(url_loader));

//     auto url_loader_factory = profile->GetDefaultStoragePartition()
//                                   ->GetURLLoaderFactoryForBrowserProcess();

//     loader_ptr->DownloadToString(
//         url_loader_factory.get(),
//         base::BindOnce(&JatterFirebaseClient::OnResponseReceived, this,
//                        loader_ptr, std::move(callback)),
//         1024 * 1024);
//   } else {
//     auto error_message = std::make_unique<std::string>("Error
//     authenticating"); std::move(callback).Run(std::move(error_message));
//   }

//   RemoveUrlLoader(loader);
// }

void JatterFirebaseClient::OnAuthenticationResponseReceived(
    network::SimpleURLLoader* loader,
    Profile* profile,
    JatterFirebaseClient::CreateUrlLoaderCallback url_loader_creator,
    JatterFirebaseClient::ResponseCallback callback,
    std::unique_ptr<std::string> response_body) {
  bool success = false;

  if (response_body) {
    LOG(INFO) << "Firebase auth token refresh response: " << *response_body;

    std::optional<base::Value> value = 
        base::JSONReader::Read(*response_body, base::JSON_PARSE_CHROMIUM_EXTENSIONS);

    if (value && value->is_dict()) {
      const base::Value::Dict& dict = value->GetDict();
      const std::string* id_token = dict.FindString("id_token");
      const std::string* refresh_token = dict.FindString("refresh_token");
      if (id_token && refresh_token) {
        LOG(INFO) << "id_token: " << *id_token;
        LOG(INFO) << "refresh_token: " << *refresh_token;

        success = true;

        JatterTokenStorage* storage = JatterTokenStorage::GetOrCreate(profile);

        // Store tokens
        storage->SetTokens(*id_token, *refresh_token);

        auto url_loader = url_loader_creator();
        network::SimpleURLLoader* loader_ptr = url_loader.get();

        // Store the loader to keep it alive
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
    LOG(ERROR) << "Failed to get Firebase auth token refresh response";
    auto error_message =
        std::make_optional<std::string>("Error authenticating");
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
