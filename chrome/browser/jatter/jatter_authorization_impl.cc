#include "chrome/browser/jatter/jatter_authorization_impl.h"

#include "base/logging.h"

JatterAuthorizationImpl::JatterAuthorizationImpl(
    mojo::PendingReceiver<jatter::mojom::JatterAuthorization> receiver)
    : receiver_(this, std::move(receiver)) {}

JatterAuthorizationImpl::~JatterAuthorizationImpl() = default;

void JatterAuthorizationImpl::SendAuthToken(const std::string& token) {
  LOG(INFO) << "Received Firebase token from renderer: " << token;
  // Store or use the token here.
}
