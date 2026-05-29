// chrome/browser/rag_ingestion/rag_ingestion_anon_loader.h
#ifndef CHROME_BROWSER_RAG_INGESTION_RAG_INGESTION_ANON_LOADER_H_
#define CHROME_BROWSER_RAG_INGESTION_RAG_INGESTION_ANON_LOADER_H_

#include <string>
#include "base/functional/callback.h"
#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "base/timer/timer.h"
#include "content/public/browser/web_contents_observer.h"
#include "net/http/http_response_headers.h"
#include "url/gurl.h"

class Profile;

namespace content {
class WebContents;
}

// [NEW] Struct to hold everything we learned from the Anon visit
struct AnonPageResult {
  std::string inner_text;
  int http_status_code = 0; // e.g., 200, 401, 403
  GURL final_url;           // To detect redirects (e.g., site.com -> site.com/login)
};

// Loads a URL in an Off-The-Record (Incognito) headless WebContents
// and extracts document.body.innerText.
class AnonPageLoader : public content::WebContentsObserver {
 public:
  using OnLoadedCallback = base::OnceCallback<void(const AnonPageResult&)>;

  AnonPageLoader(Profile* profile, const GURL& url, OnLoadedCallback callback);
  ~AnonPageLoader() override;

  void Start();

 private:
  // We need DidFinishNavigation to catch headers/redirects
  void DidFinishNavigation(content::NavigationHandle* navigation_handle) override;
  
  // void DocumentOnLoadCompletedInPrimaryMainFrame() override;

  void DOMContentLoaded(content::RenderFrameHost* render_frame_host) override;
  void ExecuteExtraction(int attempt = 1);

  raw_ptr<Profile> profile_;
  GURL url_;
  OnLoadedCallback callback_;
  std::unique_ptr<content::WebContents> headless_contents_;

  // State captured during navigation
  int last_http_status_code_ = 0;
  GURL last_committed_url_;
  
  // Guard to ensure we only run the callback once (e.g. avoid iframes triggering it)
  bool callback_run_ = false;

  base::OneShotTimer extraction_timer_;

  base::WeakPtrFactory<AnonPageLoader> weak_factory_{this};
};

#endif  // CHROME_BROWSER_RAG_INGESTION_RAG_INGESTION_ANON_LOADER_H_