#ifndef JATTER_FIREBASE_CLIENT_H_
#define JATTER_FIREBASE_CLIENT_H_

#include <vector>
#include <set>

#include "base/containers/unique_ptr_adapters.h"
#include "base/functional/callback.h"
#include "base/memory/singleton.h"
#include "base/memory/weak_ptr.h"
#include "base/scoped_observation.h"
#include "base/supports_user_data.h"
#include "base/task/cancelable_task_tracker.h"
#include "base/values.h"
#include "chrome/browser/profiles/profile.h"
#include "components/history/core/browser/history_service_observer.h"
#include "components/history/core/browser/history_types.h"
#include "components/keyed_service/core/keyed_service.h"
#include "content/public/browser/render_frame_host.h"
#include "services/network/public/cpp/simple_url_loader.h"

namespace user_prefs {
class PrefRegistrySyncable;
}

class JatterFirebaseClient : public history::HistoryServiceObserver,
                             public KeyedService {
 public:
  using ResponseCallback = base::OnceCallback<void(std::optional<std::string>)>;
  using CreateUrlLoaderCallback =
      std::function<std::unique_ptr<network::SimpleURLLoader>()>;

  explicit JatterFirebaseClient(Profile* profile);
  ~JatterFirebaseClient() override;

  static void RegisterProfilePrefs(user_prefs::PrefRegistrySyncable* registry);

  void OnURLVisited(history::HistoryService* history_service,
                    const history::VisitedURLInfo& visited_url_info) override;

  // Step 4 FRE history ingestion
  void LogBulkWebPageVisits(Profile* profile, base::ListValue visits_list);

  static void SetAuthorizationnHeader(
      Profile* profile,
      network::ResourceRequest* resource_request);

  void Invoke(Profile* profile,
              CreateUrlLoaderCallback url_loader_creator,
              ResponseCallback callback);
  void SetAuthReady(); 

 private:
  void OnResponseReceivedWithRetry(network::SimpleURLLoader* loader,
                                   base::WeakPtr<Profile> weak_profile,
                                   CreateUrlLoaderCallback url_loader_creator,
                                   ResponseCallback callback,
                                   std::optional<std::string> response_body);

  void OnResponseReceived(network::SimpleURLLoader* loader,
                          ResponseCallback callback,
                          std::optional<std::string> response_body);

  void OnAuthenticationResponseReceived(
      network::SimpleURLLoader* loader,
      base::WeakPtr<Profile> weak_profile,
      CreateUrlLoaderCallback url_loader_creator,
      ResponseCallback callback,
      std::optional<std::string> response_body);   

  void RefreshAuthToken(Profile* profile,
                        CreateUrlLoaderCallback url_loader_creator,
                        ResponseCallback callback);

  void RemoveUrlLoader(network::SimpleURLLoader* loader);

  void ExecuteNetworkRequest(Profile* profile, std::string json_body);

  bool is_auth_ready_ = false;
  std::vector<base::OnceClosure> pending_network_tasks_;
  base::ScopedObservation<history::HistoryService, history::HistoryServiceObserver>
      history_observation_{this};
  std::set<std::unique_ptr<network::SimpleURLLoader>, base::UniquePtrComparator>
      loaders_;
  base::CancelableTaskTracker task_tracker_;
  raw_ptr<Profile> profile_;

  base::WeakPtrFactory<JatterFirebaseClient> weak_ptr_factory_{this};
};

#endif  // JATTER_FIREBASE_CLIENT_H_
