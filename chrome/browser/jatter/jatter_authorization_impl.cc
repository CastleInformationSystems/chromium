#include "chrome/browser/jatter/jatter_authorization_impl.h"

#include <optional>

#include "base/json/json_reader.h"
#include "base/json/json_writer.h"
#include "base/logging.h"
#include "base/values.h"
#include "chrome/browser/browser_process.h"
#include "chrome/browser/jatter/jatter_environment.h"
#include "chrome/browser/jatter/jatter_token_storage.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/rag_ingestion/rag_ingestion_service_factory.h"
#include "chrome/browser/rag_ingestion/rag_ingestion_service.h"
#include "content/public/browser/browser_context.h"
#include "content/public/browser/global_routing_id.h"
#include "content/public/browser/render_process_host.h"
#include "content/public/browser/storage_partition.h"
#include "net/http/http_response_headers.h"
#include "net/traffic_annotation/network_traffic_annotation.h"
#include "services/network/public/cpp/resource_request.h"
#include "services/network/public/cpp/shared_url_loader_factory.h"
#include "services/network/public/cpp/simple_url_loader.h"
#include "services/network/public/mojom/url_response_head.mojom.h"

#include "url/gurl.h"

static const net::NetworkTrafficAnnotationTag kFirebaseAuthAnnotation =
    net::DefineNetworkTrafficAnnotation("firebase_custom_token",
                                        R"(
      semantics {
        sender: "Custom Firebase Auth"
        description: "Signs in using a Firebase custom authentication token."
        trigger: "User signs in through custom token workflow."
        data: "Authentication token and request for secure token."
        destination: GOOGLE_OWNED_SERVICE
      }
      policy {
        cookies_allowed: NO
        setting: "Not user-configurable"
        policy_exception_justification: "Required for custom token login."
      }
    )");

class DownloadHelper : public base::RefCounted<DownloadHelper> {
 public:
  static void Start(content::RenderFrameHost* render_frame_host,
                    std::unique_ptr<network::SimpleURLLoader> loader,
                    network::mojom::URLLoaderFactory* factory) {
    // 1. Grab the safe ID synchronously while the pointer is guaranteed valid.
    content::GlobalRenderFrameHostId safe_frame_id = render_frame_host->GetGlobalId();

    // 2. Pass the ID (NOT the pointer) into the constructor
    auto helper = base::MakeRefCounted<DownloadHelper>(safe_frame_id,
                                                       std::move(loader));
    helper->StartDownload(factory);
  }

  DownloadHelper(content::GlobalRenderFrameHostId frame_id,
                 std::unique_ptr<network::SimpleURLLoader> loader)
      : frame_id_(frame_id),
        simple_url_loader_(std::move(loader)) {}

 private:
  friend class base::RefCounted<DownloadHelper>;

  ~DownloadHelper() = default;

  void StartDownload(network::mojom::URLLoaderFactory* factory) {
    simple_url_loader_->DownloadToString(
        factory, base::BindOnce(&DownloadHelper::OnDownloadComplete, this),
        1024 * 1024);
  }

  void OnDownloadComplete(std::optional<std::string> response_body) {
    int net_error = simple_url_loader_->NetError();
  
    if (net_error != net::OK) {
      LOG(ERROR) << "!!! FIREBASE NETWORK ERROR: " << net_error 
                << " (" << net::ErrorToString(net_error) << ")";
    }

    // 2. Check HTTP Status Code (400, 401, 403, 500)
    int response_code = 0;
    if (simple_url_loader_->ResponseInfo() && simple_url_loader_->ResponseInfo()->headers) {
      response_code = simple_url_loader_->ResponseInfo()->headers->response_code();
      LOG(ERROR) << "!!! FIREBASE HTTP STATUS: " << response_code;
    }

    if (response_body) {
    #ifndef JATTER_PRODUCTION_MODE
      LOG(INFO) << "Firebase response: " << *response_body;
    #endif

      std::optional<base::Value> value = 
          base::JSONReader::Read(*response_body, base::JSON_PARSE_CHROMIUM_EXTENSIONS);

      if (value && value->is_dict()) {
        const base::DictValue& dict = value->GetDict();
        const std::string* id_token = dict.FindString("idToken");
        const std::string* refresh_token = dict.FindString("refreshToken");
        if (id_token && refresh_token) {
#ifndef JATTER_PRODUCTION_MODE
          LOG(INFO) << "id_token: " << *id_token;
          LOG(INFO) << "refresh_token: " << *refresh_token;
#endif  
          // 4. Safely resolve the ID back to a pointer inside the callback
          content::RenderFrameHost* rfh = content::RenderFrameHost::FromID(frame_id_);
          if (!rfh) {
            LOG(WARNING) << "Tab closed during Firebase auth. Aborting.";
            return; // Safe exit!
          }
          Profile* profile = Profile::FromBrowserContext(rfh->GetBrowserContext());

          JatterTokenStorage* storage =
              JatterTokenStorage::GetOrCreate(profile);

          // Store tokens
          storage->SetTokens(*id_token, *refresh_token);
        }
      }

    } else {
      LOG(ERROR) << "Failed to get Firebase response";
    }
  }

  content::GlobalRenderFrameHostId frame_id_;
  std::unique_ptr<network::SimpleURLLoader> simple_url_loader_;
};

JatterAuthorizationImpl::JatterAuthorizationImpl(
    content::RenderFrameHost& rfh,
    mojo::PendingReceiver<jatter::mojom::JatterAuthorization> receiver)
    : content::DocumentService<jatter::mojom::JatterAuthorization>(rfh, std::move(receiver)) {}

JatterAuthorizationImpl::~JatterAuthorizationImpl() = default;

void JatterAuthorizationImpl::SendAuthToken(const std::string& token) {
#ifndef JATTER_PRODUCTION_MODE
  LOG(INFO) << "Received Firebase token from renderer: " << token;
  LOG(INFO) << "kFirebaseApiKey: " << jatter::kFirebaseApiKey;
#endif
  SendCustomTokenRequest(token, jatter::kFirebaseApiKey);
}

void JatterAuthorizationImpl::SendPrivateKey(const std::string& private_key) {
#ifndef JATTER_PRODUCTION_MODE
  LOG(INFO) << "Received Firebase private_key from renderer: " << private_key;
#endif
  Profile* profile = Profile::FromBrowserContext(
              render_frame_host().GetBrowserContext());
  RagIngestionService* service = 
      RagIngestionServiceFactory::GetForProfile(profile);
  
  if (service) {
    service->SetPrivateKey(private_key);
  } else {
    // Optional: Log it so you know it was safely ignored
    VLOG(1) << "[Jatter] Ignored SendPrivateKey: RagIngestionService is null (Incognito).";
  }
}

void JatterAuthorizationImpl::SendCustomTokenRequest(
    const std::string& custom_token,
    const std::string& api_key) {
  GURL url(
      "https://identitytoolkit.googleapis.com/v1/"
      "accounts:signInWithCustomToken?key=" +
      api_key);

  auto resource_request = std::make_unique<network::ResourceRequest>();
  resource_request->url = url;
  resource_request->method = "POST";
  resource_request->headers.SetHeader("Content-Type", "application/json");

  base::DictValue body;
  body.Set("token", custom_token);
  body.Set("returnSecureToken", true);

  std::string json_body;
  base::JSONWriter::Write(body, &json_body);

  auto loader = network::SimpleURLLoader::Create(std::move(resource_request),
                                                 kFirebaseAuthAnnotation);
  loader->AttachStringForUpload(json_body, "application/json");

  auto url_loader_factory = render_frame_host().GetBrowserContext()
                                ->GetDefaultStoragePartition()
                                ->GetURLLoaderFactoryForBrowserProcess();

  DownloadHelper::Start(&render_frame_host(), std::move(loader),
                        url_loader_factory.get());
}
