// ios/chrome/browser/jatter/rag_ingestion_anon_loader.h
#ifndef IOS_CHROME_BROWSER_JATTER_RAG_INGESTION_ANON_LOADER_H_
#define IOS_CHROME_BROWSER_JATTER_RAG_INGESTION_ANON_LOADER_H_

#include <string>
#include "base/functional/callback.h"
#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "base/timer/timer.h"
#include "ios/web/public/web_state_observer.h"
#include "url/gurl.h"

class ProfileIOS;

namespace web {
class WebState;
class NavigationContext;
}

// Struct to hold everything we learned from the Anon visit
struct AnonPageResult {
  std::string inner_text;
  int http_status_code = 0; // e.g., 200, 401, 403
  GURL final_url;           // To detect redirects
};

// Loads a URL in an Off-The-Record (Incognito) headless WebState
// and extracts the text using our injected JS feature.
class AnonPageLoader : public web::WebStateObserver {
 public:
  using OnLoadedCallback = base::OnceCallback<void(const AnonPageResult&)>;

  AnonPageLoader(ProfileIOS* profile, const GURL& url, OnLoadedCallback callback);
  ~AnonPageLoader() override;

  void Start();

 private:
  // web::WebStateObserver implementation
  void DidFinishNavigation(web::WebState* web_state,
                           web::NavigationContext* navigation_context) override;
  void PageLoaded(web::WebState* web_state,
                  web::PageLoadCompletionStatus load_completion_status) override;
  void WebStateDestroyed(web::WebState* web_state) override;

  void ExecuteExtraction(int attempt = 1);

  raw_ptr<ProfileIOS> profile_;
  GURL url_;
  OnLoadedCallback callback_;
  
  // The iOS equivalent of content::WebContents
  std::unique_ptr<web::WebState> headless_web_state_;

  // State captured during navigation
  int last_http_status_code_ = 0;
  GURL last_committed_url_;
  
  bool callback_run_ = false;

  base::OneShotTimer extraction_timer_;
  base::WeakPtrFactory<AnonPageLoader> weak_factory_{this};
};

#endif  // IOS_CHROME_BROWSER_JATTER_RAG_INGESTION_ANON_LOADER_H_