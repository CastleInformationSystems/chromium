#ifndef JATTER_AUTHORIZATION_IMPL_H_
#define JATTER_AUTHORIZATION_IMPL_H_

#include "chrome/browser/jatter/jatter.mojom.h"
#include "content/public/browser/document_service.h"
#include "content/public/browser/render_frame_host.h"
#include "mojo/public/cpp/bindings/receiver.h"

class JatterAuthorizationImpl : public content::DocumentService<jatter::mojom::JatterAuthorization> {
 public:
  JatterAuthorizationImpl(
      content::RenderFrameHost& render_frame_host,
      mojo::PendingReceiver<jatter::mojom::JatterAuthorization> receiver);
  ~JatterAuthorizationImpl() override;

  void SendAuthToken(const std::string& token) override;
  void SendPrivateKey(const std::string& private_key) override;

 private:
  void SendCustomTokenRequest(const std::string& custom_token,
                              const std::string& api_key);
  void OnResponse(std::unique_ptr<std::string> response_body);

};

#endif  // JATTER_AUTHORIZATION_IMPL_H_
