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

  // ==========================================
  // Ignore Same-Document Navigations
  // ==========================================
  // If JavaScript just changed the #hash or pushed a history state, 
  // there are no new HTTP headers. Keep the old URL and status code!
  if (handle->IsSameDocument()) {
      last_committed_url_ = handle->GetURL(); // Update URL just in case
      return; // Do NOT touch last_http_status_code_!
  }

  last_committed_url_ = handle->GetURL();

  if (handle->GetResponseHeaders()) {
    last_http_status_code_ = handle->GetResponseHeaders()->response_code();
  } else {
    // Fallback for some error pages or file://
    last_http_status_code_ = 0; 
  }
}

void AnonPageLoader::DOMContentLoaded(content::RenderFrameHost* render_frame_host) {
  // We only care about the main frame, not iframes within the anon page
  if (!render_frame_host->IsInPrimaryMainFrame()) return;
  if (callback_run_) return;

  // The hidden tab has built the DOM. Give it 1.5 seconds to execute any 
  // client-side rendering (React/Vue) before we scrape it.
  extraction_timer_.Start(
      FROM_HERE, base::Milliseconds(1500),
      base::BindOnce(&AnonPageLoader::ExecuteExtraction, base::Unretained(this), 1));
}

void AnonPageLoader::ExecuteExtraction(int attempt) {
  if (callback_run_) return;

  content::RenderFrameHost* rfh = headless_contents_->GetPrimaryMainFrame();

  if (!rfh || !rfh->IsRenderFrameLive() || rfh->IsErrorDocument()) {
    if (!callback_run_ && callback_) {
      callback_run_ = true;
      std::move(callback_).Run({"", last_http_status_code_, last_committed_url_});
    }
    return;
  }

  static const char16_t kScript[] = uR"(
    (() => {
      if (!document.body) return "";
      
      // 1. Clone the body in memory so we don't destroy the live page
      let clone = document.body.cloneNode(true);
      
      // 2. Destroy all non-text tags from the clone
      let badTags = clone.querySelectorAll(
          'script, style, noscript, svg, img, video, ' + 
          'select, template, dialog, ' +
          '[aria-hidden="true"], [hidden], ' + 
          '[style*="display: none"], [style*="display:none"]'
      );
      badTags.forEach(tag => tag.remove());
      
      // 3. Extract textContent (which ignores CSS visibility completely)
      // and collapse the massive whitespaces into single spaces.
      return clone.textContent.replace(/\s+/g, ' ').trim();
    })();
  )";
  
  rfh->ExecuteJavaScriptInIsolatedWorld(
      kScript,
      base::BindOnce(
          [](base::WeakPtr<AnonPageLoader> loader, int attempt, base::Value result) {
            if (!loader) return;
            
            std::string text = result.is_string() ? result.GetString() : "";

            // C++ POLLING LOGIC
            // If the text is empty and we haven't maxed out our 6 attempts, wait 500ms and try again
            if (text.length() < 500 && attempt < 10) {
                loader->extraction_timer_.Start(
                    FROM_HERE, base::Milliseconds(300),
                    base::BindOnce(&AnonPageLoader::ExecuteExtraction, loader, attempt + 1));
                return;
            }

            // We either got the text, or we hit our attempt limit. Fire the callback.
            loader->callback_run_ = true;
            AnonPageResult final_result{text, loader->last_http_status_code_, loader->last_committed_url_};
            
            if (loader->callback_) {
              std::move(loader->callback_).Run(final_result);
            }
          },
          weak_factory_.GetWeakPtr(), attempt),
      kAnonLoaderWorldId 
  );
}
