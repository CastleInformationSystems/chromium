#ifndef JATTER_AUTHORIZATION_IMPL_H_
#define JATTER_AUTHORIZATION_IMPL_H_

#include "chrome/browser/jatter/jatter.mojom.h"
#include "content/public/browser/render_frame_host.h"
#include "mojo/public/cpp/bindings/receiver.h"

class JatterAuthorizationImpl : public jatter::mojom::JatterAuthorization {
 public:
  JatterAuthorizationImpl(
      mojo::PendingReceiver<jatter::mojom::JatterAuthorization> receiver,
      content::RenderFrameHost* render_frame_host);
  ~JatterAuthorizationImpl() override;

  void SendAuthToken(const std::string& token) override;

 private:
  void SendCustomTokenRequest(const std::string& custom_token,
                              const std::string& api_key);
  void OnResponse(std::unique_ptr<std::string> response_body);

  raw_ptr<content::RenderFrameHost> render_frame_host;
  mojo::Receiver<jatter::mojom::JatterAuthorization> receiver_;
};

#endif  // JATTER_AUTHORIZATION_IMPL_H_
