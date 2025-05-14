#include "chrome/browser/jatter/jatter_firebase_client.h"

#include "base/json/json_reader.h"
#include "base/json/json_writer.h"
#include "chrome/browser/browser_process.h"
#include "chrome/browser/jatter/jatter_token_storage.h"
#include "chrome/browser/profiles/profile.h"
#include "content/public/browser/browser_context.h"
#include "content/public/browser/render_process_host.h"
#include "content/public/browser/storage_partition.h"
#include "net/traffic_annotation/network_traffic_annotation.h"
#include "services/network/public/cpp/resource_request.h"

static const char kFirebaseApiKey[] = "AIzaSyDMEcoBQaYnTWMA_hGmkIK3hpr0NB9Zqf8";

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

void JatterFirebaseClient::ObservePageVisit(
    content::RenderFrameHost* render_frame_host,
    std::string url) {
  JatterFirebaseClient::Invoke(
      render_frame_host,
      [render_frame_host, url]() {
        GURL endpoint(
            std::string(
                "https://identitytoolkit.googleapis.com/v1/token?key=") +
            kFirebaseApiKey);

        auto resource_request = std::make_unique<network::ResourceRequest>();
        resource_request->url = endpoint;
        resource_request->method = "POST";
        resource_request->headers.SetHeader("Content-Type", "application/json");
        JatterFirebaseClient::SetAuthorizationnHeader(render_frame_host,
                                                      resource_request.get());

        base::Value::Dict data;
        data.Set("url", url);

        base::Value::Dict body;
        body.Set("data", std::move(data));

        std::string json_body;
        base::JSONWriter::Write(body, &json_body);

        auto loader = network::SimpleURLLoader::Create(
            std::move(resource_request), kFirebaseFunctionAnnotation);
        loader->AttachStringForUpload(json_body, "application/json");
        return loader;
      },
      [](std::unique_ptr<std::string> response_body) {

      });
}

void JatterFirebaseClient::SetAuthorizationnHeader(
    content::RenderFrameHost* render_frame_host,
    network::ResourceRequest* resource_request) {
  Profile* profile = Profile::FromBrowserContext(
      render_frame_host->GetProcess()->GetBrowserContext());

  JatterTokenStorage* storage = JatterTokenStorage::GetOrCreate(profile);
  std::string id_token = storage->GetIdToken();
  LOG(INFO) << "JatterFirebaseClient::SetAuthenticationHeader id_token: "
            << id_token;

  resource_request->headers.SetHeader("Authorization", "Bearer " + id_token);
}

void JatterFirebaseClient::Invoke(
    content::RenderFrameHost* render_frame_host,
    std::function<std::unique_ptr<network::SimpleURLLoader>()> loader_factory,
    std::function<void(std::unique_ptr<std::string> response_body)> callback) {
  std::unique_ptr<network::SimpleURLLoader> loader(loader_factory());

  auto url_loader_factory = render_frame_host->GetBrowserContext()
                                ->GetDefaultStoragePartition()
                                ->GetURLLoaderFactoryForBrowserProcess();

  loader->DownloadToString(
      url_loader_factory.get(),
      base::BindOnce(
          [](content::RenderFrameHost* render_frame_host,
             std::function<std::unique_ptr<network::SimpleURLLoader>()>
                 loader_factory,
             std::function<void(std::unique_ptr<std::string> response_body)>
                 callback,
             std::unique_ptr<std::string> response_body) {
            bool did_attempt_refresh = false;

            if (response_body) {
              LOG(INFO) << "JatterFirebaseClient::Invoke response: "
                        << *response_body;

              std::optional<base::Value> value =
                  base::JSONReader::Read(*response_body);

              if (value && value->is_dict()) {
                const base::Value::Dict& dict = value->GetDict();
                const std::string* status = dict.FindString("status");
                if (status) {
                  if (*status == "UNAUTHENTICATED") {
                    LOG(INFO) << "Refreshing token";

                    did_attempt_refresh = true;

                    base::OnceCallback<void(bool)> once_callback =
                        base::BindOnce(
                            [](content::RenderFrameHost* render_frame_host,
                               std::function<std::unique_ptr<
                                   network::SimpleURLLoader>()> loader_factory,
                               std::function<void(std::unique_ptr<std::string>)>
                                   callback,
                               bool success) {
                              if (success) {
                                auto loader = loader_factory();

                                auto url_loader_factory =
                                    render_frame_host->GetBrowserContext()
                                        ->GetDefaultStoragePartition()
                                        ->GetURLLoaderFactoryForBrowserProcess();

                                auto once_callback = base::BindOnce(
                                    [](std::function<void(
                                           std::unique_ptr<std::string>)>
                                           callback,
                                       std::optional<std::string>
                                           response_body) {
                                      if (response_body) {
                                        callback(std::make_unique<std::string>(
                                            *response_body));
                                      } else {
                                        callback(nullptr);
                                      }
                                    },
                                    std::move(callback));

                                loader->DownloadToString(
                                    url_loader_factory.get(),
                                    std::move(once_callback), 1024 * 1024);
                              } else {
                                auto foobar = std::make_unique<std::string>(
                                    "Error authenticating");
                                callback(std::move(foobar));
                              }
                            },
                            render_frame_host, loader_factory, callback);

                    JatterFirebaseClient::RefreshAuthToken(
                        render_frame_host, std::move(once_callback));
                  }
                }
              }
            } else {
              LOG(ERROR)
                  << "JatterFirebaseClient::Invoke Failed to get response";
            }

            if (!did_attempt_refresh) {
              callback(std::move(response_body));
            }
          },
          render_frame_host, loader_factory, callback),
      1024 * 1024);
}

void JatterFirebaseClient::RefreshAuthToken(
    content::RenderFrameHost* render_frame_host,
    base::OnceCallback<void(bool)> callback) {
  Profile* profile = Profile::FromBrowserContext(
      render_frame_host->GetProcess()->GetBrowserContext());

  JatterTokenStorage* storage = JatterTokenStorage::GetOrCreate(profile);
  std::string refresh_token = storage->GetRefreshToken();
  LOG(INFO) << "refresh_token RECALL: " << refresh_token;

  GURL url(std::string("https://securetoken.googleapis.com/v1/token?key=") +
           kFirebaseApiKey);

  auto resource_request = std::make_unique<network::ResourceRequest>();
  resource_request->url = url;
  resource_request->method = "POST";
  resource_request->headers.SetHeader("Content-Type",
                                      "application/x-www-form-urlencoded");

  std::string body = "grant_type=refresh_token&refresh_token=" + refresh_token;

  auto loader = network::SimpleURLLoader::Create(std::move(resource_request),
                                                 kFirebaseFunctionAnnotation);
  loader->AttachStringForUpload(body, "application/x-www-form-urlencoded");

  auto url_loader_factory = render_frame_host->GetBrowserContext()
                                ->GetDefaultStoragePartition()
                                ->GetURLLoaderFactoryForBrowserProcess();

  loader->DownloadToString(
      url_loader_factory.get(),
      base::BindOnce(
          [](content::RenderFrameHost* render_frame_host,
             base::OnceCallback<void(bool)> callback,
             std::unique_ptr<std::string> response_body) {
            bool success = false;

            if (response_body) {
              LOG(INFO) << "Firebase auth token refresh response: "
                        << *response_body;

              std::optional<base::Value> value =
                  base::JSONReader::Read(*response_body);

              if (value && value->is_dict()) {
                const base::Value::Dict& dict = value->GetDict();
                const std::string* id_token = dict.FindString("id_token");
                const std::string* refresh_token =
                    dict.FindString("refresh_token");
                if (id_token && refresh_token) {
                  LOG(INFO) << "id_token: " << *id_token;
                  LOG(INFO) << "refresh_token: " << *refresh_token;

                  Profile* profile = Profile::FromBrowserContext(
                      render_frame_host->GetProcess()->GetBrowserContext());

                  JatterTokenStorage* storage =
                      JatterTokenStorage::GetOrCreate(profile);

                  // Store tokens
                  storage->SetTokens(*id_token, *refresh_token);
                  success = true;
                }
              }
            } else {
              LOG(ERROR)
                  << "Failed to get Firebase auth token refresh response";
            }

            std::move(callback).Run(success);
          },
          render_frame_host, std::move(callback)),
      1024 * 1024);
}
