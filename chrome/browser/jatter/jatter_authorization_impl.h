#ifndef JATTER_AUTHORIZATION_IMPL_H_
#define JATTER_AUTHORIZATION_IMPL_H_

#include "chrome/browser/jatter/jatter.mojom.h"
#include "mojo/public/cpp/bindings/receiver.h"

class JatterAuthorizationImpl : public jatter::mojom::JatterAuthorization {
 public:
  JatterAuthorizationImpl(
      mojo::PendingReceiver<jatter::mojom::JatterAuthorization> receiver);
  ~JatterAuthorizationImpl() override;

  void SendAuthToken(const std::string& token) override;

 private:
  mojo::Receiver<jatter::mojom::JatterAuthorization> receiver_;
};

#endif  // JATTER_AUTHORIZATION_IMPL_H_
