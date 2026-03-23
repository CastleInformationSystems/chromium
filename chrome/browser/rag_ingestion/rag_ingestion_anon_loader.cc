// chrome/browser/rag_ingestion/rag_ingestion_anon_loader.cc
#include "chrome/browser/rag_ingestion/rag_ingestion_anon_loader.h"

#include "base/uuid.h"
#include "chrome/browser/profiles/profile.h"
#include "content/public/browser/navigation_controller.h"
#include "content/public/browser/navigation_handle.h"
#include "content/public/browser/render_frame_host.h"
#include "content/public/browser/web_contents.h"
#include "content/public/common/content_switches.h"
#include "third_party/blink/public/common/web_preferences/web_preferences.h"

namespace {
const int kAnonLoaderWorldId = 1; 
}

AnonPageLoader::AnonPageLoader(Profile* profile, const GURL& url, OnLoadedCallback callback)
    : profile_(profile), url_(url), callback_(std::move(callback)) {}

AnonPageLoader::~AnonPageLoader() = default;

void AnonPageLoader::Start() {
  // 1. Create a unique, isolated, in-memory storage partition on the MAIN profile.
  // This provides cookie-less anonymity without triggering ProfileDestroyer UAFs.
  auto partition_config = content::StoragePartitionConfig::Create(
      profile_, 
      "anon_loader_" + base::Uuid::GenerateRandomV4().AsLowercaseString(), 
      "",   // partition_name
      true  // in_memory
  );

  scoped_refptr<content::SiteInstance> site_instance =
      content::SiteInstance::CreateForFixedStoragePartition(
          profile_, url_, partition_config);

  // 2. Attach the WebContents to the MAIN profile, but securely isolated by the SiteInstance
  content::WebContents::CreateParams create_params(profile_, std::move(site_instance));
  create_params.initially_hidden = true; 
  create_params.is_never_composited = true;
  
  headless_contents_ = content::WebContents::Create(create_params);
  headless_contents_->SetAudioMuted(true);
  blink::web_pref::WebPreferences prefs = 
      headless_contents_->GetOrCreateWebPreferences();
  prefs.hide_scrollbars = true;
  headless_contents_->SetWebPreferences(prefs);
  
  // Observe the new WebContents
  Observe(headless_contents_.get());

  content::NavigationController::LoadURLParams load_params(url_);
  headless_contents_->GetController().LoadURLWithParams(load_params);
}

void AnonPageLoader::DidFinishNavigation(content::NavigationHandle* handle) {
  // We only care about the primary main frame (ignore iframes/ads)
  if (!handle->IsInPrimaryMainFrame() || !handle->HasCommitted()) return;

  last_committed_url_ = handle->GetURL();

  if (handle->GetResponseHeaders()) {
    last_http_status_code_ = handle->GetResponseHeaders()->response_code();
  } else {
    // Fallback for some error pages or file://
    last_http_status_code_ = 0; 
  }
}

// void AnonPageLoader::DocumentOnLoadCompletedInPrimaryMainFrame() {
//   if (callback_run_) return;

//   content::RenderFrameHost* rfh = headless_contents_->GetPrimaryMainFrame();

//   // Safety Checks
//   if (!rfh || !rfh->IsRenderFrameLive() || rfh->IsErrorDocument()) {
//     if (callback_run_ == false && callback_) {
//       callback_run_ = true;
//       AnonPageResult fail_result{"", last_http_status_code_, last_committed_url_};
//       std::move(callback_).Run(fail_result);
//     }
//     return;
//   }

//   // Mark run to prevent double-firing
//   callback_run_ = true;

//   // [FIX] Use Isolated World (ID 1) to bypass security blocks
//   rfh->ExecuteJavaScriptInIsolatedWorld(
//       u"document.body.innerText",
//       base::BindOnce(
//           [](base::WeakPtr<AnonPageLoader> loader, base::Value result) {
//             if (!loader) return;
            
//             AnonPageResult final_result;
//             final_result.inner_text = result.is_string() ? result.GetString() : "";
//             final_result.http_status_code = loader->last_http_status_code_;
//             final_result.final_url = loader->last_committed_url_;

//             if (loader->callback_) {
//               std::move(loader->callback_).Run(final_result);
//             }
//           },
//           weak_factory_.GetWeakPtr()),
//       kAnonLoaderWorldId 
//   );
// }

void AnonPageLoader::DOMContentLoaded(content::RenderFrameHost* render_frame_host) {
  // We only care about the main frame, not iframes within the anon page
  if (!render_frame_host->IsInPrimaryMainFrame()) return;
  if (callback_run_) return;

  // The hidden tab has built the DOM. Give it 1.5 seconds to execute any 
  // client-side rendering (React/Vue) before we scrape it.
  extraction_timer_.Start(
      FROM_HERE, base::Milliseconds(1500),
      base::BindOnce(&AnonPageLoader::ExecuteExtraction, base::Unretained(this)));
}

// [NEW] This holds the logic that used to be inside DocumentOnLoadCompleted...
void AnonPageLoader::ExecuteExtraction() {
  if (callback_run_) return;

  content::RenderFrameHost* rfh = headless_contents_->GetPrimaryMainFrame();

  // Safety Checks
  if (!rfh || !rfh->IsRenderFrameLive() || rfh->IsErrorDocument()) {
    if (!callback_run_ && callback_) {
      callback_run_ = true;
      AnonPageResult fail_result{"", last_http_status_code_, last_committed_url_};
      std::move(callback_).Run(fail_result);
    }
    return;
  }

  // Mark run to prevent double-firing
  callback_run_ = true;

  // Execute JS to get the text
  rfh->ExecuteJavaScriptInIsolatedWorld(
      u"document.body.innerText",
      base::BindOnce(
          [](base::WeakPtr<AnonPageLoader> loader, base::Value result) {
            if (!loader) return;
            
            AnonPageResult final_result;
            final_result.inner_text = result.is_string() ? result.GetString() : "";
            final_result.http_status_code = loader->last_http_status_code_;
            final_result.final_url = loader->last_committed_url_;

            if (loader->callback_) {
              std::move(loader->callback_).Run(final_result);
            }
          },
          weak_factory_.GetWeakPtr()),
      kAnonLoaderWorldId 
  );
}