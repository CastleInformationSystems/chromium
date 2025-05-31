#include "chrome/browser/jatter/jatter_authorization_impl.h"

#include "base/json/json_reader.h"
#include "base/json/json_writer.h"
#include "base/logging.h"
#include "base/values.h"
#include "chrome/browser/browser_process.h"
#include "chrome/browser/jatter/jatter_environment.h"
#include "chrome/browser/jatter/jatter_token_storage.h"
#include "chrome/browser/profiles/profile.h"
#include "content/public/browser/browser_context.h"
#include "content/public/browser/render_process_host.h"
#include "content/public/browser/storage_partition.h"
#include "net/traffic_annotation/network_traffic_annotation.h"
#include "services/network/public/cpp/resource_request.h"
#include "services/network/public/cpp/shared_url_loader_factory.h"
#include "services/network/public/cpp/simple_url_loader.h"
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
    auto helper = base::MakeRefCounted<DownloadHelper>(render_frame_host,
                                                       std::move(loader));
    helper->StartDownload(factory);
  }

  DownloadHelper(content::RenderFrameHost* render_frame,
                 std::unique_ptr<network::SimpleURLLoader> loader)
      : render_frame_host(render_frame),
        simple_url_loader_(std::move(loader)) {}

 private:
  friend class base::RefCounted<DownloadHelper>;

  ~DownloadHelper() = default;

  void StartDownload(network::mojom::URLLoaderFactory* factory) {
    simple_url_loader_->DownloadToString(
        factory, base::BindOnce(&DownloadHelper::OnDownloadComplete, this),
        1024 * 1024);
  }

  void OnDownloadComplete(std::unique_ptr<std::string> response_body) {
    if (response_body) {
      LOG(INFO) << "Firebase response: " << *response_body;

      std::optional<base::Value> value = base::JSONReader::Read(*response_body);

      if (value && value->is_dict()) {
        const base::Value::Dict& dict = value->GetDict();
        const std::string* id_token = dict.FindString("idToken");
        const std::string* refresh_token = dict.FindString("refreshToken");
        if (id_token && refresh_token) {
          LOG(INFO) << "id_token: " << *id_token;
          LOG(INFO) << "refresh_token: " << *refresh_token;

          Profile* profile = Profile::FromBrowserContext(
              render_frame_host->GetProcess()->GetBrowserContext());

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

  raw_ptr<content::RenderFrameHost> render_frame_host;
  std::unique_ptr<network::SimpleURLLoader> simple_url_loader_;
};

JatterAuthorizationImpl::JatterAuthorizationImpl(
    mojo::PendingReceiver<jatter::mojom::JatterAuthorization> receiver,
    content::RenderFrameHost* rfh)
    : render_frame_host(rfh), receiver_(this, std::move(receiver)) {}

JatterAuthorizationImpl::~JatterAuthorizationImpl() = default;

void JatterAuthorizationImpl::SendAuthToken(const std::string& token) {
  LOG(INFO) << "Received Firebase token from renderer: " << token;
  SendCustomTokenRequest(token, kFirebaseApiKey);
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

  base::Value::Dict body;
  body.Set("token", custom_token);
  body.Set("returnSecureToken", true);

  std::string json_body;
  base::JSONWriter::Write(body, &json_body);

  auto loader = network::SimpleURLLoader::Create(std::move(resource_request),
                                                 kFirebaseAuthAnnotation);
  loader->AttachStringForUpload(json_body, "application/json");

  auto url_loader_factory = render_frame_host->GetBrowserContext()
                                ->GetDefaultStoragePartition()
                                ->GetURLLoaderFactoryForBrowserProcess();

  DownloadHelper::Start(render_frame_host, std::move(loader),
                        url_loader_factory.get());
}
