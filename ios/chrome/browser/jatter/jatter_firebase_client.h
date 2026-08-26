#ifndef IOS_CHROME_BROWSER_JATTER_JATTER_FIREBASE_CLIENT_H_
#define IOS_CHROME_BROWSER_JATTER_JATTER_FIREBASE_CLIENT_H_

#include <memory>
#include <optional>
#include <set>
#include <string>

#include "base/containers/unique_ptr_adapters.h"
#include "base/functional/callback.h"
#include "base/memory/singleton.h"
#include "services/network/public/cpp/simple_url_loader.h"

class ProfileIOS;

class JatterFirebaseClient {
 public:
  using ResponseCallback = base::OnceCallback<void(std::optional<std::string>)>;
  using CreateUrlLoaderCallback =
      std::function<std::unique_ptr<network::SimpleURLLoader>()>;

  static JatterFirebaseClient* GetInstance();

  void ObservePageVisit(ProfileIOS* profile, std::string url, std::string title);

  static void SetAuthorizationHeader(
      ProfileIOS* profile,
      network::ResourceRequest* resource_request);

  void Invoke(ProfileIOS* profile,
              CreateUrlLoaderCallback url_loader_creator,
              ResponseCallback callback);

 private:
  friend struct base::DefaultSingletonTraits<JatterFirebaseClient>;

  JatterFirebaseClient();
  ~JatterFirebaseClient();

  JatterFirebaseClient(const JatterFirebaseClient&) = delete;
  JatterFirebaseClient& operator=(const JatterFirebaseClient&) = delete;

  void OnResponseReceivedWithRetry(network::SimpleURLLoader* loader,
                                   ProfileIOS* profile,
                                   CreateUrlLoaderCallback url_loader_creator,
                                   ResponseCallback callback,
                                   std::optional<std::string> response_body);

  void OnResponseReceived(network::SimpleURLLoader* loader,
                          ResponseCallback callback,
                          std::optional<std::string> response_body);

  void OnAuthenticationResponseReceived(
      network::SimpleURLLoader* loader,
      ProfileIOS* profile,
      CreateUrlLoaderCallback url_loader_creator,
      ResponseCallback callback,
      std::optional<std::string> response_body);

  void RefreshAuthToken(ProfileIOS* profile,
                        CreateUrlLoaderCallback url_loader_creator,
                        ResponseCallback callback);

  void RemoveUrlLoader(network::SimpleURLLoader* loader);

  std::set<std::unique_ptr<network::SimpleURLLoader>, base::UniquePtrComparator>
      loaders_;
};

#endif  // IOS_CHROME_BROWSER_JATTER_JATTER_FIREBASE_CLIENT_H_