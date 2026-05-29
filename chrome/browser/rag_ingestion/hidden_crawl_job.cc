// Copyright 2024 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/rag_ingestion/hidden_crawl_job.h"

#include "base/json/json_writer.h"
#include "base/strings/string_util.h"
#include "base/strings/utf_string_conversions.h"
#include "base/task/single_thread_task_runner.h"
#include "base/time/time.h"
#include "base/uuid.h"
#include "chrome/browser/profiles/profile.h"
#include "content/public/browser/navigation_controller.h"
#include "content/public/browser/render_frame_host.h"
#include "content/public/browser/storage_partition.h"
#include "content/public/browser/web_contents.h"
#include "content/public/common/content_constants.h"
#include "net/base/registry_controlled_domains/registry_controlled_domain.h"
#include "net/traffic_annotation/network_traffic_annotation.h"
#include "services/network/public/cpp/resource_request.h"
#include "services/network/public/cpp/shared_url_loader_factory.h"
#include "services/network/public/mojom/url_response_head.mojom.h"
#include "third_party/blink/public/common/renderer_preferences/renderer_preferences.h"
#include "third_party/blink/public/common/web_preferences/web_preferences.h"
#include "ui/accessibility/ax_mode.h"


namespace {

constexpr base::TimeDelta kWatchdogTimeout = base::Seconds(45); // Increased for "DOM Quiet" wait

// The "Smart Scraper" script from the spec.
// Waits for mutations to settle, then grabs text and links.
const char16_t kScraperScript[] = uR"(
    (function() {
        // 1. Ensure the DOM is fully constructed
        if (document.readyState !== 'complete' || !document.body) {
            return { 'ready': false, 'text': '', 'html': '' };
        }

        // 2. Grab the natively computed, human-visible text AND the raw HTML
        return { 
            'ready': true, 
            'text': document.body.innerText || '',
            'html': document.documentElement.outerHTML || ''
        };
    })();
)";

} // namespace

HiddenCrawlJob::HiddenCrawlJob(Profile* profile,
                               const GURL& url,
                               DoneCallback done_cb)
    : profile_(profile),
      target_url_(url),
      done_cb_(std::move(done_cb)) {
}

HiddenCrawlJob::~HiddenCrawlJob() {
  watchdog_.Stop();
  // web_contents_ will be destroyed here, verifying the 
  // "Destroy WebContents (memory partition dies)" requirement.
}

void HiddenCrawlJob::Start() {
  watchdog_.Start(FROM_HERE, kWatchdogTimeout,
                  base::BindOnce(&HiddenCrawlJob::OnTimeout, base::Unretained(this)));

  // 1. Correctly define the partition config
  auto partition_config = content::StoragePartitionConfig::Create(
      profile_, 
      "rag_job_" + base::Uuid::GenerateRandomV4().AsLowercaseString(), 
      "",   // partition_name
      true  // in_memory
  );

  // [FIX] Create a SiteInstance with that partition config
  scoped_refptr<content::SiteInstance> site_instance =
      content::SiteInstance::CreateForFixedStoragePartition(
          profile_, target_url_, partition_config);

  // [FIX] Assign the SiteInstance to params
  content::WebContents::CreateParams params(profile_, std::move(site_instance));
  params.initially_hidden = true; // Better for background jobs
  params.is_never_composited = true; 

  web_contents_ = content::WebContents::Create(params);
  web_contents_->SetDelegate(this);
  Observe(web_contents_.get());

  web_contents_->WasShown();

  StartAnonFetch();
  CopyCookiesAndNavigate();
}

void HiddenCrawlJob::StartAnonFetch() {
  auto resource_request = std::make_unique<network::ResourceRequest>();
  resource_request->url = target_url_;
  resource_request->method = "GET";
  resource_request->credentials_mode = network::mojom::CredentialsMode::kOmit;

  net::NetworkTrafficAnnotationTag traffic_annotation =
      net::DefineNetworkTrafficAnnotation("rag_page_validator", R"(
        semantics {
          sender: "RAG Page Validator"
          description: "Checks if the requested crawl target is public or private."
          trigger: "Background crawl message received."
          data: "None."
          destination: WEBSITE
        }
        policy { cookies_allowed: NO }
      )");

  auto factory = profile_->GetDefaultStoragePartition()->GetURLLoaderFactoryForBrowserProcess();
  anon_loader_ = network::SimpleURLLoader::Create(std::move(resource_request), traffic_annotation);
  anon_loader_->SetAllowHttpErrorResults(true);

  anon_loader_->DownloadToString(
      factory.get(),
      base::BindOnce(&HiddenCrawlJob::OnAnonFetchComplete, weak_factory_.GetWeakPtr()),
      1024);
}

void HiddenCrawlJob::OnAnonFetchComplete(std::optional<std::string> response_body) {
  is_anon_complete_ = true;

  if (anon_loader_->ResponseInfo() && anon_loader_->ResponseInfo()->headers) {
    int code = anon_loader_->ResponseInfo()->headers->response_code();
    GURL final_url = anon_loader_->GetFinalURL();

    if (code == 401 || code == 403) {
      is_auth_wall_ = true;
    } else if (final_url != target_url_) {
      std::string spec = final_url.spec();
      if (spec.find("login") != std::string::npos || spec.find("auth") != std::string::npos) {
        is_auth_wall_ = true;
      }
    }
  }
  CheckFinish();
}

void HiddenCrawlJob::CopyCookiesAndNavigate() {
  // 1. The SOURCE (User's main profile)
  content::StoragePartition* source_partition = profile_->GetDefaultStoragePartition();
  if (!source_partition) return;

  network::mojom::CookieManager* source_cookie_manager = 
      source_partition->GetCookieManagerForBrowserProcess();

  net::CookieOptions options = net::CookieOptions::MakeAllInclusive();
  options.set_return_excluded_cookies();

  // [FIX] Using source_cookie_manager here
  source_cookie_manager->GetCookieList(
      target_url_, options, net::CookiePartitionKeyCollection::Todo(),
      base::BindOnce(&HiddenCrawlJob::OnCookiesRetrieved, 
                     weak_factory_.GetWeakPtr()));
}

void HiddenCrawlJob::OnCookiesRetrieved(
    const std::vector<net::CookieWithAccessResult>& cookies,
    const std::vector<net::CookieWithAccessResult>& excluded_cookies) {
  
  if (cookies.empty()) {
    NavigateToTarget();
    return;
  }

  // 2. The DESTINATION (The isolated partition inside our job)
  content::StoragePartition* dest_partition = 
      web_contents_->GetBrowserContext()->GetStoragePartition(web_contents_->GetSiteInstance());
      
  network::mojom::CookieManager* dest_cookie_manager = 
      dest_partition->GetCookieManagerForBrowserProcess();

  pending_cookie_writes_ = cookies.size();

  for (const auto& entry : cookies) {
    const net::CanonicalCookie& cookie = entry.cookie;

    // [FIX] Using dest_cookie_manager here
    dest_cookie_manager->SetCanonicalCookie(
        cookie,
        target_url_, 
        net::CookieOptions::MakeAllInclusive(), 
        base::BindOnce(&HiddenCrawlJob::OnCookiesSet, weak_factory_.GetWeakPtr()));
  }
}
void HiddenCrawlJob::OnCookiesSet(net::CookieAccessResult result) {
  if (pending_cookie_writes_ > 0) {
    pending_cookie_writes_--;
  }

  // Once all cookies are copied, we navigate
  if (pending_cookie_writes_ == 0) {
    NavigateToTarget();
  }
}

void HiddenCrawlJob::NavigateToTarget() {
  if (!web_contents_) return;
  content::NavigationController::LoadURLParams load_params(target_url_);
  web_contents_->GetController().LoadURLWithParams(load_params);
}

void HiddenCrawlJob::DidFinishLoad(content::RenderFrameHost* render_frame_host,
                                   const GURL& validated_url) {
  if (!render_frame_host->IsInPrimaryMainFrame()) return;
  if (!render_frame_host->IsRenderFrameLive()) return;
  if (render_frame_host->GetLifecycleState() != content::RenderFrameHost::LifecycleState::kActive) return;

  // Start polling every 500ms
  poll_attempts_ = 0;
  poll_timer_.Start(FROM_HERE, base::Milliseconds(500), 
                    base::BindRepeating(&HiddenCrawlJob::PollDOM, weak_factory_.GetWeakPtr()));
}

void HiddenCrawlJob::PollDOM() {
  if (!web_contents_) {
      poll_timer_.Stop();
      return;
  }
  
  content::RenderFrameHost* rfh = web_contents_->GetPrimaryMainFrame();
  if (!rfh || !rfh->IsRenderFrameLive()) return;

  poll_attempts_++;

  // Note: Using the rfh directly here!
  rfh->ExecuteJavaScriptInIsolatedWorld(
      kScraperScript,
      base::BindOnce(&HiddenCrawlJob::OnScriptExecuted, weak_factory_.GetWeakPtr()), 1);
}

void HiddenCrawlJob::OnScriptExecuted(base::Value result) {
  // 1. Basic Validation
  if (!result.is_dict()) {
    LOG(WARNING) << "[RAG] JS returned a non-dictionary value!";
    return;
  }

  const auto& dict = result.GetDict();
  const std::string* current_text = dict.FindString("text");
  const std::string* current_html = dict.FindString("html");
  bool is_ready = dict.FindBool("ready").value_or(false);

  if (!current_text || !current_html) return;

  // 2. Growth Tracking
  size_t current_length = current_text->length();
  long length_diff = std::abs(static_cast<long>(current_length) - static_cast<long>(last_text_length_));
  
  // If the page content grew since the last poll, Jira is likely still rendering tickets.
  if (length_diff > 50) {
    last_text_length_ = current_length;
    final_text_ = *current_text; // Keep the biggest version
    final_html_ = *current_html;
    stable_ticks_ = 0;           // Reset the "silence" counter
    VLOG(1) << "[RAG] Content growing... Current length: " << current_length;
  } else {
    // Text size is identical to last time
    stable_ticks_++;
  }

  // 3. Termination Logic
  bool has_meaningful_text = current_length > 200; // Increased to ensure we got past just the sidebar
  
  // Success: Page says it's ready, we have text, and that text hasn't changed for 3 polls (1.5s)
  if (is_ready && has_meaningful_text && stable_ticks_ >= 3) {
    poll_timer_.Stop();
    LOG(INFO) << "[RAG] Content stabilized. Final text size: " << final_text_.length();
    
    is_auth_complete_ = true;
    CheckFinish();
  } 
  // Timeout: We've polled 30 times (15s) and still haven't stabilized or found text
  else if (poll_attempts_ >= 30) {
    poll_timer_.Stop();
    
    // If we have some text but just never "stabilized," let's use what we have instead of failing
    if (has_meaningful_text) {
        LOG(WARNING) << "[RAG] Polling timed out but text found. Proceeding with partial data.";
        is_auth_complete_ = true;
        CheckFinish();
    } else {
        LOG(WARNING) << "[RAG] JS Scrape failed on " << target_url_ << ". Trying AXTree fallback...";
        TryAXTreeFallback();
    }
  }
}

void HiddenCrawlJob::TryAXTreeFallback() {
  if (!web_contents_) {
      is_auth_complete_ = true;
      CheckFinish();
      return;
  }

  // Request a snapshot of the Accessibility Tree
  web_contents_->RequestAXTreeSnapshot(
      base::BindOnce(&HiddenCrawlJob::OnAXSnapshotReceived, weak_factory_.GetWeakPtr()),
      ui::AXMode::kWebContents, 
      5000,             // max_nodes to prevent freezing on massive pages
      base::Seconds(2), // Timeout for the snapshot generation
      content::WebContents::AXTreeSnapshotPolicy::kSameOriginDirectDescendants);
}

void HiddenCrawlJob::OnAXSnapshotReceived(ui::AXTreeUpdate& snapshot) {
  std::string fallback_text;
  
  // Iterate through every node in the tree and grab the human-readable text
  for (const auto& node : snapshot.nodes) {
    if (node.HasStringAttribute(ax::mojom::StringAttribute::kName)) {
      fallback_text += node.GetStringAttribute(ax::mojom::StringAttribute::kName) + " ";
    }
    if (node.HasStringAttribute(ax::mojom::StringAttribute::kValue)) {
      fallback_text += node.GetStringAttribute(ax::mojom::StringAttribute::kValue) + " ";
    }
  }

  // Clean up the text (removes massive gaps of whitespace)
  final_text_ = base::CollapseWhitespaceASCII(fallback_text, false);

  // The absolute last resort: The page Title
  if (final_text_.empty() && web_contents_) {
      final_text_ = base::UTF16ToUTF8(web_contents_->GetTitle());
  }

  is_auth_complete_ = true;
  CheckFinish();
}

void HiddenCrawlJob::DidFailLoad(content::RenderFrameHost* render_frame_host,
                                 const GURL& validated_url,
                                 int error_code) {
  if (render_frame_host != web_contents_->GetPrimaryMainFrame()) return;
  
  LOG(WARNING) << "HiddenCrawlJob Failed Load: " << error_code;

  poll_timer_.Stop();
  
  // Set empty data since the page failed to load
  final_html_ = "";
  final_text_ = "";
  
  // Mark the authenticated step as finished so it doesn't block
  is_auth_complete_ = true;
  
  // Hand control back to the coordinator
  CheckFinish();
}

void HiddenCrawlJob::CheckFinish() {
  if (!is_anon_complete_ || !is_auth_complete_) return;

  // =================================================================
  // EXTRA GUARD: The C++ Last-Resort Fallback
  // If the JS scraper timed out after 15 seconds and found no text,
  // we fallback to C++ WebContents data so the backend doesn't crash.
  // =================================================================
  if (final_text_.empty()) {
      LOG(WARNING) << "[RAG] HiddenCrawlJob JS extraction failed. Using C++ fallback on: " 
                   << target_url_;
                   
      if (web_contents_) {
          // Fallback 1: Grab the page Title (e.g. "Gmail - Inbox")
          final_text_ = base::UTF16ToUTF8(web_contents_->GetTitle());
      }
      
      // Fallback 2: If even the title is empty, provide a safe dummy string
      if (final_text_.empty()) {
          final_text_ = "[Page contained no extractable text]";
      }
  }

  // Now we can safely check our text heuristics
  if (!is_auth_wall_) {
      std::string text_lower = base::ToLowerASCII(final_text_);
      if (text_lower.find("log in") != std::string::npos || 
          text_lower.find("sign in") != std::string::npos ||
          text_lower.find("forgot password") != std::string::npos) {
          is_auth_wall_ = true;
      }
  }

  watchdog_.Stop();
  if (done_cb_) {
    std::move(done_cb_).Run(final_html_, final_text_, is_auth_wall_);
  }
}

void HiddenCrawlJob::OnTimeout() {
  LOG(WARNING) << "HiddenCrawlJob Timed Out on " << target_url_;
  if (done_cb_) {
      std::move(done_cb_).Run("", "", false);
  }
}
