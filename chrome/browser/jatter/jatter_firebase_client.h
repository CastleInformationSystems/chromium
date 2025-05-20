#ifndef JATTER_FIREBASE_CLIENT_H_
#define JATTER_FIREBASE_CLIENT_H_

#include "base/supports_user_data.h"
#include "chrome/browser/profiles/profile.h"
#include "content/public/browser/render_frame_host.h"
#include "services/network/public/cpp/simple_url_loader.h"

class JatterFirebaseClient {
 public:
  static void ObservePageVisit(Profile* profile,
                               std::string url,
                               std::string title);

 private:
  static void SetAuthorizationnHeader(
      Profile* profile,
      network::ResourceRequest* resource_request);

  static void RefreshAuthToken(Profile* profile,
                               base::OnceCallback<void(bool)> callback);

  static void Invoke(
      Profile* profile,
      std::function<std::unique_ptr<network::SimpleURLLoader>()> loader_factory,
      std::function<void(std::unique_ptr<std::string> response_body)> callback);
};

#endif  // JATTER_FIREBASE_CLIENT_H_
