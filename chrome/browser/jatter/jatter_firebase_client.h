#ifndef JATTER_FIREBASE_CLIENT_H_
#define JATTER_FIREBASE_CLIENT_H_

#include "base/containers/unique_ptr_adapters.h"
#include "base/memory/singleton.h"
#include "base/supports_user_data.h"
#include "chrome/browser/profiles/profile.h"
#include "content/public/browser/render_frame_host.h"
#include "services/network/public/cpp/simple_url_loader.h"

class JatterFirebaseClient {
 public:
  using ResponseCallback = base::OnceCallback<void(std::optional<std::string>)>;
  using CreateUrlLoaderCallback =
      std::function<std::unique_ptr<network::SimpleURLLoader>()>;

  static JatterFirebaseClient* GetInstance();

  void ObservePageVisit(Profile* profile, std::string url, std::string title);

 private:
  friend struct base::DefaultSingletonTraits<JatterFirebaseClient>;

  JatterFirebaseClient();
  ~JatterFirebaseClient();

  JatterFirebaseClient(const JatterFirebaseClient&) = delete;
  JatterFirebaseClient& operator=(const JatterFirebaseClient&) = delete;

  void OnResponseReceivedWithRetry(network::SimpleURLLoader* loader,
                                   Profile* profile,
                                   CreateUrlLoaderCallback url_loader_creator,
                                   ResponseCallback callback,
                                   std::unique_ptr<std::string> response_body);

  void OnResponseReceived(network::SimpleURLLoader* loader,
                          ResponseCallback callback,
                          std::unique_ptr<std::string> response_body);

  void OnAuthenticationResponseReceived(
      network::SimpleURLLoader* loader,
      Profile* profile,
      CreateUrlLoaderCallback url_loader_creator,
      ResponseCallback callback,
      std::unique_ptr<std::string> response_body);

  static void SetAuthorizationnHeader(
      Profile* profile,
      network::ResourceRequest* resource_request);

  void RefreshAuthToken(Profile* profile,
                        CreateUrlLoaderCallback url_loader_creator,
                        ResponseCallback callback);

  void Invoke(Profile* profile,
              CreateUrlLoaderCallback url_loader_creator,
              ResponseCallback callback);

  void RemoveUrlLoader(network::SimpleURLLoader* loader);

  std::set<std::unique_ptr<network::SimpleURLLoader>, base::UniquePtrComparator>
      loaders_;
};

#endif  // JATTER_FIREBASE_CLIENT_H_
