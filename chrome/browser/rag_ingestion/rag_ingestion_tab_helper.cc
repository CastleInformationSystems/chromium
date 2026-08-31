// Copyright 2024 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/rag_ingestion/rag_ingestion_tab_helper.h"

#include <algorithm>
#include <iterator>
#include <vector>

#include "base/command_line.h" // [INJECTED] For CLI switch
#include "base/containers/span.h"
#include "base/files/file.h"
#include "base/files/file_path.h"
#include "base/files/file_util.h"
#include "base/functional/bind.h"
#include "base/json/json_writer.h"
#include "base/logging.h"
#include "base/no_destructor.h"
#include "base/path_service.h"
#include "base/strings/string_util.h"
#include "base/strings/utf_string_conversions.h"
#include "base/task/thread_pool.h"
#include "base/threading/scoped_blocking_call.h"
#include "base/time/time.h"
#include "base/timer/elapsed_timer.h"
#include "chrome/browser/jatter/jatter_environment.h"
#include "chrome/browser/password_manager/chrome_password_manager_client.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/jatter/jatter_firebase_client.h"
#include "chrome/browser/rag_ingestion/rag_ingestion_service.h"
#include "chrome/browser/rag_ingestion/rag_ingestion_service_factory.h"
#if BUILDFLAG(IS_ANDROID)
#include "chrome/browser/ui/android/rag_ingestion/rag_ingestion_controller_android.h"
#else
#include "chrome/browser/ui/rag_ingestion/rag_ingestion_page_action_controller.h"
#endif
#include "components/embedder_support/user_agent_utils.h"
#include "components/password_manager/core/browser/password_manager.h"
#include "content/public/browser/browser_context.h"
#include "content/public/common/mhtml_generation_params.h"
#include "content/public/browser/navigation_handle.h"
#include "content/public/browser/storage_partition.h"
#include "content/public/browser/web_contents.h"
#include "net/base/load_flags.h"
#include "net/cookies/site_for_cookies.h"
#include "net/http/http_request_headers.h"
#include "net/http/http_response_headers.h"
#include "net/traffic_annotation/network_traffic_annotation.h"
#include "services/network/public/cpp/resource_request.h"
#include "services/network/public/mojom/url_response_head.mojom.h"
#include "ui/accessibility/ax_enums.mojom.h"
#include "url/origin.h"

WEB_CONTENTS_USER_DATA_KEY_IMPL(RagIngestionTabHelper);

static base::TimeTicks g_rag_debug_start;

namespace {

const int kRagIngestionWorldId = 1; 

// [INJECTED] The command line switch to force testing
const char kRagForceAuthWallSwitch[] = "rag-force-auth-wall";

// Helper function to read the file in the background
std::string ReadMhtmlFileInBackground(const base::FilePath& file_path) {
  std::string file_content;
  base::ReadFileToString(file_path, &file_content);
  
  // Optional: Delete the temp file now that we have it in memory
  base::DeleteFile(file_path); 
  
  return file_content;
}

}  // namespace

RagIngestionTabHelper::RagIngestionTabHelper(content::WebContents* web_contents)
    : content::WebContentsObserver(web_contents),
      content::WebContentsUserData<RagIngestionTabHelper>(*web_contents) {}

RagIngestionTabHelper::~RagIngestionTabHelper() = default;

void RagIngestionTabHelper::DidFinishNavigation(
    content::NavigationHandle* navigation_handle) {
  if (!navigation_handle->IsInPrimaryMainFrame() ||
      !navigation_handle->HasCommitted()) {
    return;
  }

  ingestion_timer_.Stop();
  GURL url = navigation_handle->GetURL();

  // 2. Handle Single Page Applications (SPAs) like Notion or Twitter
  if (navigation_handle->IsSameDocument()) {
      // In an SPA, the URL changes via JS (pushState), so DOMContentLoaded 
      // will NOT fire again. We must trigger the extraction here.
      // Give React/Vue 1.5 seconds to swap out the DOM components.
      ingestion_timer_.Start(FROM_HERE, base::Milliseconds(500),
                             base::BindOnce(&RagIngestionTabHelper::DocumentReadyForExtraction,
                                            base::Unretained(this), url));
      return; 
  }

  // Just reset state. Don't run the heavy logic yet.
  live_text_content_.clear();
  anon_text_content_.clear();
  anon_loader_.reset();
  captured_links_.clear();
  has_live_text_ = false;
  has_anon_text_ = false;

  if (navigation_handle->GetResponseHeaders()) {
      std::string mime_type;
      if (navigation_handle->GetResponseHeaders()->GetMimeType(&mime_type) && 
          mime_type == "application/pdf") {
          
          Profile* profile = Profile::FromBrowserContext(web_contents()->GetBrowserContext());
          RagIngestionService* service = RagIngestionServiceFactory::GetForProfile(profile);

          // Only proceed if this site is already "learned" (Auth Wall + Granted)
          if (service && service->GetUserPermission(url) == RagIngestionService::UserPermission::kGranted) {
              LOG(INFO) << "[RAG] Intercepted in-browser PDF for learned site. Fetching bytes...";
              FetchPdfBytesForIngestion(url);
          }
          return; // Stop standard HTML processing for PDFs
      }
  }
}

void RagIngestionTabHelper::DocumentReadyForExtraction(const GURL& url) {
  if (base::CommandLine::ForCurrentProcess()->HasSwitch(kRagForceAuthWallSwitch)) {
    RagIngestionService::BackendPermissionInfo info{};
    OnPermissionCheckComplete(info);
    return;
  }

  if (ShouldIgnoreUrl(url)) return;
  
  Profile* profile = Profile::FromBrowserContext(web_contents()->GetBrowserContext());
  std::u16string title = web_contents()->GetTitle();
  
  JatterFirebaseClient::GetInstance()->ObservePageVisit(
      profile, 
      url.spec(), 
      base::UTF16ToUTF8(title)
  );

  CheckAuthWall(url);
}

void RagIngestionTabHelper::DocumentOnLoadCompletedInPrimaryMainFrame() {
  GURL url = web_contents()->GetLastCommittedURL();

  // if (base::CommandLine::ForCurrentProcess()->HasSwitch(kRagForceAuthWallSwitch)) {
  //   RagIngestionService::BackendPermissionInfo info{};
  //   OnPermissionCheckComplete(info);
  //   return;
  // }

  // Fire a short 500ms timer to let the render thread settle
  ingestion_timer_.Start(FROM_HERE, base::Milliseconds(500),
                         base::BindOnce(&RagIngestionTabHelper::DocumentReadyForExtraction,
                                        base::Unretained(this), url));
}

bool RagIngestionTabHelper::ShouldIgnoreUrl(const GURL& url) {
  // 1. Ignore internal browser pages
  if (url.SchemeIs("chrome") || url.SchemeIs("about") ||
      url.SchemeIs("devtools")) {
    return true;
  }
  
  // 2. Ignore local development and Jatter backend/frontend domains
  if (url.host() == jatter::kAppHost ||
      url.host() == "www.jatter.ai" ||
      url.host() == "localhost" || 
      url.host() == "127.0.0.1") {
    return true; // "Passes the check" to be ignored!
  }
  
  return false;
}

bool RagIngestionTabHelper::ShouldSkipCheckDueToCache(const GURL& url) {
  std::string spec = std::string(url.spec());
  auto it = last_check_time_.find(spec);
  if (it != last_check_time_.end()) {
    if (base::Time::Now() - it->second < kCacheDuration) {
      return true;
    }
  }
  return false;
}

bool RagIngestionTabHelper::PageHasLinkContaining(const std::string& partial_text) {
  // partial_text should be lowercase, but let's be safe
  std::string search = base::ToLowerASCII(partial_text);
  
  for (const std::string& link : captured_links_) {
    // We check the full URL string (e.g. "swedbank.ee/auth/logout")
    if (link.find(search) != std::string::npos) {
      return true;
    }
  }
  return false;
}

void RagIngestionTabHelper::CheckAuthWall(const GURL& url) {
  g_rag_debug_start = base::TimeTicks::Now();
#ifndef JATTER_PRODUCTION_MODE
  LOG(INFO) << "[RAG-DEBUG] [ 0 ms ] Pipeline Started - Forking Live JS and Anon Fetch concurrently...";
#endif  
  // Reset State
  live_text_content_.clear();
  anon_text_content_.clear();
  captured_links_.clear();
  has_live_text_ = false;
  has_anon_text_ = false;
  current_check_url_ = url;

  // 1. Start Branch A: Live DOM polling
  AttemptLiveTextExtraction(1);

  // 2. Start Branch B: Anonymous Network Fetch concurrently!
  StartAnonPageCheck();
}

void RagIngestionTabHelper::AttemptLiveTextExtraction(int attempt) {
  content::RenderFrameHost* rfh = web_contents()->GetPrimaryMainFrame();
  if (!rfh || !rfh->IsRenderFrameLive() || rfh->IsErrorDocument()) {
    return;
  }

  // STRICTLY SYNCHRONOUS JAVASCRIPT
  static const char16_t kScript[] = uR"(
    ({
      'text': (document.body?.innerText || ""),
      'links': Array.from(document.links || []).map(a => a.href)
    })
  )";

  rfh->ExecuteJavaScriptInIsolatedWorld(
      kScript,
      // Pass 'attempt' to the callback so it knows when to give up
      base::BindOnce(&RagIngestionTabHelper::OnLiveTextCaptured,
                     weak_factory_.GetWeakPtr(), attempt),
      kRagIngestionWorldId
  );
}

void RagIngestionTabHelper::OnLiveTextCaptured(int attempt, base::Value result) {
#ifndef JATTER_PRODUCTION_MODE
  LOG(INFO) << "[RAG-DEBUG] [ " << (base::TimeTicks::Now() - g_rag_debug_start).InMilliseconds() 
            << " ms ] Live JS Captured (Attempt " << attempt << ").";
#endif            
  if (result.is_dict()) {
    const auto& dict = result.GetDict();
    if (const std::string* text = dict.FindString("text")) {
      live_text_content_ = base::CollapseWhitespaceASCII(*text, false);
    }
    if (const base::ListValue* links = dict.FindList("links")) {
      for (const auto& item : *links) {
        if (item.is_string()) {
          captured_links_.push_back(base::ToLowerASCII(item.GetString()));
        }
      }
    }
  }

  if (live_text_content_.length() < 1000 && attempt < 10) {
      ingestion_timer_.Start(
          FROM_HERE, base::Milliseconds(300),
          base::BindOnce(&RagIngestionTabHelper::AttemptLiveTextExtraction,
                         base::Unretained(this), attempt + 1));
      return; 
  }

  if (live_text_content_.length() < 50) {
      LOG(WARNING) << "[RAG] Live text empty after retries. Aborting pipeline.";
#if !BUILDFLAG(IS_ANDROID)
      if (auto* controller = RagIngestionPageActionController::FromWebContents(web_contents())) {
          controller->Hide(); 
      }
#endif
      anon_loader_.reset(); // Cancel background network fetch if running
      return;
  }

  double heuristic_score = CalculateHeuristicScore();
  bool looks_private = heuristic_score > 0.0; 
  bool is_cached_safe = ShouldSkipCheckDueToCache(current_check_url_);

  if (is_cached_safe && !looks_private) {
#ifndef JATTER_PRODUCTION_MODE
    VLOG(1) << "Skipping check (Cached & Safe): " << current_check_url_;
#endif
    anon_loader_.reset(); // Cancel background network fetch immediately to save cellular data
    return; 
  }

  // Mark Branch A as complete and attempt to pass through the Join Gate
  has_live_text_ = true;
  CompareContentState();
}

void RagIngestionTabHelper::StartAnonPageCheck() {
  Profile* profile = Profile::FromBrowserContext(web_contents()->GetBrowserContext());
  anon_loader_ = std::make_unique<AnonPageLoader>(
      profile, 
      current_check_url_,
      base::BindOnce(&RagIngestionTabHelper::OnAnonTextCaptured,
                     weak_factory_.GetWeakPtr())
  );
  anon_loader_->Start();
}

void RagIngestionTabHelper::OnAnonTextCaptured(const AnonPageResult& result) {
#ifndef JATTER_PRODUCTION_MODE
  LOG(INFO) << "[RAG-DEBUG] [ " << (base::TimeTicks::Now() - g_rag_debug_start).InMilliseconds() 
            << " ms ] Anon Fetch complete.";
#endif            
  anon_text_content_ = base::CollapseWhitespaceASCII(result.inner_text, false);
  anon_response_code_ = result.http_status_code;
  anon_final_url_ = result.final_url;

  if (anon_text_content_.length() < 50) {
#ifndef JATTER_PRODUCTION_MODE
      LOG(INFO) << "[RAG] Anon text is empty/tiny on a 200 OK. Likely an SPA.";
#endif
      anon_text_content_.clear();
  }

  // Mark Branch B as complete and attempt to pass through the Join Gate
  has_anon_text_ = true;
  CompareContentState();
}

void RagIngestionTabHelper::MarkPageAsPublic(double score, const std::string& reason) {
#ifndef JATTER_PRODUCTION_MODE
    VLOG(1) << "[RAG] Page is Public (Score " << score << "). Reason: " << reason;
#endif
#if !BUILDFLAG(IS_ANDROID)
    if (auto* controller = RagIngestionPageActionController::FromWebContents(web_contents())) {
        controller->Hide(); 
    }
#endif
    
    last_check_time_[current_check_url_.spec()] = base::Time::Now();
}

void RagIngestionTabHelper::CompareContentState() {
  // --- THE JOIN GATE ---
  if (!has_live_text_ || !has_anon_text_) {
#ifndef JATTER_PRODUCTION_MODE
    LOG(INFO) << "[RAG-DEBUG] [ " << (base::TimeTicks::Now() - g_rag_debug_start).InMilliseconds() 
              << " ms ] Join Gate holding... (Live Ready: " << (has_live_text_ ? "YES" : "NO") 
              << ", Anon Ready: " << (has_anon_text_ ? "YES" : "NO") << ")";
#endif
    return;
  }

#ifndef JATTER_PRODUCTION_MODE
  LOG(INFO) << "[RAG-DEBUG] [ " << (base::TimeTicks::Now() - g_rag_debug_start).InMilliseconds() 
            << " ms ] Both parallel branches complete! Running Jaccard comparison.";
#endif            
  base::ElapsedTimer jaccard_timer;
  // --- A. Network Score (Did Anon get 403 or Redirected?) ---
  double network_score = CalculateNetworkScore(); 

  // --- B. Heuristic Score (Does the Live page look like a login form?) ---
  // Note: We run this on the LIVE text/content usually.
  double score_heuristics = CalculateHeuristicScore();

  // --- C. Content Score (The expensive Jaccard check) ---
  std::set<std::string> live_tokens = TokenizeText(live_text_content_);
  std::set<std::string> anon_tokens = TokenizeText(anon_text_content_);
  
  double content_similarity = CalculateJaccardSimilarity(live_tokens, anon_tokens);
  
  // Logic: 
  // High Similarity (0.95) -> Public Page
  // Low Similarity (<0.85) -> Auth Wall (User sees content, Anon sees "Please Login")
  
  double content_auth_probability = 1.0 - content_similarity;

  // --- THE SPA SAFETY NET ---
  // If the live page has text, but the anon page is completely empty despite a 200 OK status,
  // this is almost certainly a Client-Side Rendered (React/Next.js) app, NOT an Auth Wall.
  if (live_tokens.size() > 50 && anon_tokens.size() < 5 && anon_response_code_ == 200) {
#ifndef JATTER_PRODUCTION_MODE
      LOG(INFO) << "[RAG] Detected SPA (Client-Side Rendering) structure. Bypassing Jaccard check.";
#endif
      content_auth_probability = 0.0; // Trust the heuristics instead
  }

  // Weighted Sum Tuning
  double total_score = (content_auth_probability) + (score_heuristics * 0.4);

  if (network_score > 0.0) {
    total_score = std::max(total_score, 0.8 + (network_score * 0.2));
  }

  if (total_score > 1.0) {
    total_score = 1.0;
  }

  std::string json_string;

  // --- Final Weighted Decision ---
  // We can weight them, or simple cascading (as done above) is often more robust.
#ifndef JATTER_PRODUCTION_MODE
  LOG(INFO) << "[RAG-DEBUG] [ " << (base::TimeTicks::Now() - g_rag_debug_start).InMilliseconds() 
            << " ms ] Jaccard calculation took: " << jaccard_timer.Elapsed().InMilliseconds() << " ms.";
#endif
  if (total_score > 0.8) {
#ifndef JATTER_PRODUCTION_MODE
    LOG(INFO) << "[RAG-DEBUG] [ " << (base::TimeTicks::Now() - g_rag_debug_start).InMilliseconds() 
               << " ms ] Pinging Firebase Backend...";
    // AUTH WALL DETECTED
    VLOG(1) << "RagIngestion: Detected Auth Wall on " << current_check_url_;
#endif
    ReportAuthWall(); 
  } else {
     // PUBLIC / SAFE DETECTED
     // We ran the heavy check and confirmed it's public. 
     // Update the cache so we don't do this again for 10 minutes.
#ifndef JATTER_PRODUCTION_MODE     
    VLOG(1) << "RagIngestion: Page is Public (Score " << total_score << "). Caching result.";
#endif
#if !BUILDFLAG(IS_ANDROID)
     // We must tell the controller "This page is no longer interesting"
     if (auto* controller = RagIngestionPageActionController::FromWebContents(web_contents())) {
         controller->Hide(); // Or controller->ResetState(); depending on your API
     }
#endif
     
     // [NEW] SET THE CACHE HERE
     last_check_time_[current_check_url_.spec()] = base::Time::Now();
  }
}

double RagIngestionTabHelper::CalculateNetworkScore() {
  // 1. HTTP Code Check
  if (anon_response_code_ == 401 || anon_response_code_ == 403) {
    return 1.0; // Definite Auth Wall
  }

  // 2. Redirect Check
  // Did we ask for "site.com/doc" but end up at "site.com/login"?
  if (anon_final_url_ != current_check_url_) {
    std::string url_str = anon_final_url_.spec();
    if (url_str.find("login") != std::string::npos || 
        url_str.find("signin") != std::string::npos ||
        url_str.find("auth") != std::string::npos) {
      return 0.5; // Strong signal
    }
    // Generic redirect (e.g. trailing slash) is ignored
  }

  return 0.0;
}

double RagIngestionTabHelper::CalculateHeuristicScore() {
  double score = 0.0;

  // 1. URL-based signals (Strongest)
  std::string url_lower = base::ToLowerASCII(current_check_url_.spec());
  if (url_lower.find("logout") != std::string::npos || 
      url_lower.find("signout") != std::string::npos) {
    score += 0.2; // Immediate trigger
  }

  // 2. Link Destination Signals (Language Agnostic)
  // Checks captured_links_ for English technical terms (auth, logout, account)
  // which often exist in URLs even on non-English sites.
  if (PageHasLinkContaining("logout") || 
      PageHasLinkContaining("signout") || 
      PageHasLinkContaining("logoff") ||
      PageHasLinkContaining("/account/") || // e.g. /my-account/
      PageHasLinkContaining("/profile/")) {
    score += 0.2;
  }

  // 3. Text Signals (Weakest, Language Dependent)
  std::string text_lower = base::ToLowerASCII(live_text_content_);
  
  // Use your existing kAuthIndicators array here if you like
  // Or simple manual checks:
  if (text_lower.find("sign out") != std::string::npos || 
      text_lower.find("log out") != std::string::npos) {
    score += 0.2;
  }

  return score;
}

std::set<std::string> RagIngestionTabHelper::TokenizeText(
    const std::string& text) {
  std::set<std::string> tokens;
  std::string current_word;
  for (char c : text) {
    if (base::IsAsciiAlpha(c) || base::IsAsciiDigit(c)) {
      current_word.push_back(base::ToLowerASCII(c));
    } else if (!current_word.empty()) {
      if (current_word.length() > 2) {
        tokens.insert(current_word);
      }
      current_word.clear();
    }
  }
  if (current_word.length() > 2) {
    tokens.insert(current_word);
  }
  return tokens;
}

double RagIngestionTabHelper::CalculateJaccardSimilarity(
    const std::set<std::string>& set_a,
    const std::set<std::string>& set_b) {
  if (set_a.empty() && set_b.empty()) {
    return 1.0;
  }
  if (set_a.empty() || set_b.empty()) {
    return 0.0;
  }

  std::vector<std::string> intersection_result;
  std::set_intersection(set_a.begin(), set_a.end(), set_b.begin(), set_b.end(),
                        std::back_inserter(intersection_result));
  std::vector<std::string> union_result;
  std::set_union(set_a.begin(), set_a.end(), set_b.begin(), set_b.end(),
                 std::back_inserter(union_result));

  if (union_result.empty()) {
    return 0.0;
  }
  return static_cast<double>(intersection_result.size()) /
         static_cast<double>(union_result.size());
}

void RagIngestionTabHelper::IngestCurrentPage(int retry_count) {
  Profile* profile = Profile::FromBrowserContext(web_contents()->GetBrowserContext());
  RagIngestionService* service = RagIngestionServiceFactory::GetForProfile(profile);
  
  if (!service) return;

  // 1. Define a temporary file path for Chromium to write to
  base::FilePath temp_dir;
  base::GetTempDir(&temp_dir);
  base::FilePath mhtml_path = temp_dir.AppendASCII("temp_ingestion.mhtml");

  content::MHTMLGenerationParams params(mhtml_path);
  
  // 2. Start the generation
  web_contents()->GenerateMHTML(
      params,
      base::BindOnce(
          [](base::WeakPtr<RagIngestionService> service, 
             base::WeakPtr<RagIngestionTabHelper> helper, // Need this to call retry
             GURL url, 
             base::FilePath path, 
             int current_retry, // Track attempts
             int64_t size) {
            
            if (size <= 0) {
#ifndef JATTER_PRODUCTION_MODE
              LOG(ERROR) << "[RAG] MHTML generation failed completely.";
#endif              
              return;
            }

            // [NEW RETRY LOGIC] 
            // If the file is suspiciously small AND we haven't retried twice yet...
            if (size <= 25000 && current_retry < 2) {
#ifndef JATTER_PRODUCTION_MODE
              LOG(WARNING) << "[RAG] MHTML size (" << size 
                           << " bytes) is small. Retrying in 2 seconds... (Attempt " 
                           << current_retry + 1 << "/2)";
#endif              
              // 1. Delete the tiny file in the background
              base::ThreadPool::PostTask(
                  FROM_HERE, {base::MayBlock()}, 
                  base::BindOnce(base::IgnoreResult(&base::DeleteFile), path));
              
              // 2. Schedule a retry on the Main UI Thread in 2 seconds
              if (helper) {
                base::SequencedTaskRunner::GetCurrentDefault()->PostDelayedTask(
                    FROM_HERE,
                    base::BindOnce(&RagIngestionTabHelper::IngestCurrentPage, helper, current_retry + 1),
                    base::Seconds(2));
              }
              return; // Stop current pipeline
            }

            // If we get here, the file is either a healthy size, OR we ran out of retries
            // and we are going to send whatever we managed to capture.
            if (size <= 25000) {
#ifndef JATTER_PRODUCTION_MODE
              LOG(INFO) << "[RAG] MHTML is still small after retries. Proceeding with ingestion anyway.";
#endif
            }

            // Proceed with background read as normal...
            base::ThreadPool::PostTaskAndReplyWithResult(
                FROM_HERE, {base::MayBlock(), base::TaskPriority::BEST_EFFORT},
                base::BindOnce(&ReadMhtmlFileInBackground, path),
                base::BindOnce(
                    [](base::WeakPtr<RagIngestionService> inner_service, 
                       GURL inner_url,
                       std::string inner_title, 
                       std::string mhtml_data) {
                      if (inner_service && !mhtml_data.empty()) {
                        inner_service->StartDocumentIngestion(
                            inner_url, inner_title, mhtml_data, "multipart/related", "page.mhtml");
                      }
                    }, 
                    service, 
                    url, 
                    helper ? base::UTF16ToUTF8(helper->web_contents()->GetTitle()) : "Untitled")
            );
            
          }, service->GetWeakPtr(), weak_factory_.GetWeakPtr(), current_check_url_, mhtml_path, retry_count));
}

void RagIngestionTabHelper::ReportAuthWall() {
  if (!web_contents()) return;

  // [NEW] The Micro-Cache Check
  std::string url_spec = current_check_url_.spec();
  auto it = last_auth_ping_time_.find(url_spec);
  if (it != last_auth_ping_time_.end() && 
     (base::Time::Now() - it->second < kAuthPingCooldown)) {
#ifndef JATTER_PRODUCTION_MODE      
      LOG(WARNING) << "[RAG] Auth Wall API throttled (Micro-cache). Try again in a few seconds.";
#endif      
      // We still need to ensure the UI is drawn! 
      // If the controller was wiped by PrimaryPageChanged, we can just 
      // let the UI sit empty for 3 seconds, or force a 'kUnknown' state here.
      // Usually, just returning is fine because a spam-reloader won't care about UI.
      return; 
  }

  // Record the ping time
  last_auth_ping_time_[url_spec] = base::Time::Now();

  Profile* profile = Profile::FromBrowserContext(web_contents()->GetBrowserContext());
  RagIngestionService* service = RagIngestionServiceFactory::GetForProfile(profile);
  if (!service) return;
#ifndef JATTER_PRODUCTION_MODE
  LOG(INFO) << "[JATTER] Auth Wall confirmed. Querying backend for source-of-truth status: " 
            << current_check_url_;
#endif
  // [FIX ARCHITECTURE]
  // Completely removed the fast-local cache bypass here. We MUST ask the 
  // backend every single time to ensure we catch Web App overrides or Account Logouts.
  service->CheckBackendStatus(
      current_check_url_,
      base::BindOnce(&RagIngestionTabHelper::OnPermissionCheckComplete,
                     weak_factory_.GetWeakPtr()));
}

void RagIngestionTabHelper::OnPermissionCheckComplete(
    RagIngestionService::BackendPermissionInfo info) {
#ifndef JATTER_PRODUCTION_MODE
  LOG(INFO) << "[RAG-DEBUG] [ " << (base::TimeTicks::Now() - g_rag_debug_start).InMilliseconds() 
      << " ms ] Backend check complete. Drawing Android UI NOW.";
#endif
  // =========================================================================
  // 1. CROSS-PLATFORM ERROR HANDLING
  // =========================================================================
  if (info.is_error) {
#if BUILDFLAG(IS_ANDROID)
    if (auto* controller = RagIngestionControllerAndroid::FromWebContents(web_contents())) {
      controller->Hide();
    }
#else
    if (auto* controller = RagIngestionPageActionController::FromWebContents(web_contents())) {
      controller->Hide();
    }
#endif
    return;
  }

  Profile* profile = Profile::FromBrowserContext(web_contents()->GetBrowserContext());
  RagIngestionService* service = RagIngestionServiceFactory::GetForProfile(profile);
  if (!service) return;

  // =========================================================================
  // 2. CROSS-PLATFORM BUSINESS LOGIC (No UI)
  // =========================================================================
  if (info.status == RagIngestionService::BackendStatus::kKnownAllowed) {
    service->SyncLocalSettingFromBackend(current_check_url_, 
                                         RagIngestionService::UserPermission::kGranted);
    IngestCurrentPage(); 
  } else if (info.status == RagIngestionService::BackendStatus::kKnownBlocked) {
    service->SyncLocalSettingFromBackend(current_check_url_, 
                                         RagIngestionService::UserPermission::kDenied);
  }

  // =========================================================================
  // 3. PLATFORM-SPECIFIC UI LOGIC
  // =========================================================================
#if BUILDFLAG(IS_ANDROID)
  // --- ANDROID UI ---
  // Ensure the Android controller is created to manage the adaptive toolbar button.
  RagIngestionControllerAndroid::CreateForWebContents(web_contents());
  auto* controller = RagIngestionControllerAndroid::FromWebContents(web_contents());
  if (!controller) return;

  if (info.status == RagIngestionService::BackendStatus::kKnownAllowed) {
    controller->ShowActiveState();
  } 
  else if (info.status == RagIngestionService::BackendStatus::kKnownBlocked) {
    controller->ShowDisabledState();
  } 
  else if (info.status == RagIngestionService::BackendStatus::kUnknown) {
    RagIngestionService::UserPermission local_status = service->GetUserPermission(current_check_url_);
    
    if (local_status == RagIngestionService::UserPermission::kGranted) {
      controller->ShowActiveState();
    } else if (local_status == RagIngestionService::UserPermission::kDenied) {
      controller->ShowDisabledState();
    } else {
      // For undecided pages, trigger the ephemeral bottom sheet offer prompt
      controller->ShowOfferPrompt(current_check_url_); 
    }
  }

#else
  // --- DESKTOP UI ---
  // Desktop has a persistent Omnibox icon that must be initialized and updated 
  // for every state.
  RagIngestionPageActionController::CreateForWebContents(web_contents());
  auto* controller = RagIngestionPageActionController::FromWebContents(web_contents());
  if (!controller) return;

  controller->SetBackendInfo(info);

  if (info.status == RagIngestionService::BackendStatus::kKnownAllowed) {
    controller->ShowActiveState();
  } 
  else if (info.status == RagIngestionService::BackendStatus::kKnownBlocked) {
    controller->ShowDisabledState();
  } 
  else if (info.status == RagIngestionService::BackendStatus::kUnknown) {
    RagIngestionService::UserPermission local_status = service->GetUserPermission(current_check_url_);
    
    if (local_status == RagIngestionService::UserPermission::kGranted) {
      controller->ShowActiveState();
    } else if (local_status == RagIngestionService::UserPermission::kDenied) {
      controller->ShowDisabledState();
    } else {
      controller->ShowOfferState(); 
    }
  }
#endif
}

void RagIngestionTabHelper::FetchPdfBytesForIngestion(const GURL& url) {
  auto request = std::make_unique<network::ResourceRequest>();
  request->url = url;
  request->site_for_cookies = net::SiteForCookies::FromUrl(url);
  request->load_flags = net::LOAD_DISABLE_CACHE;
  request->credentials_mode = network::mojom::CredentialsMode::kInclude; 

  Profile* profile = Profile::FromBrowserContext(web_contents()->GetBrowserContext());
  auto url_loader_factory = profile->GetDefaultStoragePartition()
                                ->GetURLLoaderFactoryForBrowserProcess();

  net::NetworkTrafficAnnotationTag traffic_annotation =
      net::DefineNetworkTrafficAnnotation("rag_ingestion_pdf_fetcher", R"(
        semantics {
          sender: "RAG Ingestion Service"
          description: "Downloads PDF bytes from learned sites for ingestion."
          trigger: "User opens a PDF from an allowed RAG ingestion domain."
          data: "PDF file content."
          destination: OTHER
        }
        policy {
          cookies_allowed: YES
          cookies_store: "user"
          setting: "Feature is strictly opt-in."
        })");

  pdf_loader_ = network::SimpleURLLoader::Create(std::move(request), traffic_annotation);

  // Download up to 50MB into memory
  pdf_loader_->DownloadToString(
      url_loader_factory.get(),
      base::BindOnce(&RagIngestionTabHelper::OnPdfBytesFetched,
                     weak_factory_.GetWeakPtr(), url),
      network::SimpleURLLoader::kMaxBoundedStringDownloadSize);
}

void RagIngestionTabHelper::OnPdfBytesFetched(const GURL& url, std::optional<std::string> response_body) {
  if (!response_body) {
      LOG(ERROR) << "[RAG] Failed to fetch in-browser PDF bytes.";
      return;
  }
  
  Profile* profile = Profile::FromBrowserContext(web_contents()->GetBrowserContext());
  RagIngestionService* service = RagIngestionServiceFactory::GetForProfile(profile);
  
  if (!service) return;

  std::string pdf_str(response_body->begin(), response_body->end());

  // 1. Extract the actual filename from the end of the URL path
  std::string filename = url.ExtractFileName();
  
  // 2. Smart Fallback: If the URL didn't have a clean filename, use the domain
  if (filename.empty()) {
    filename = std::string(url.host()) + "_document.pdf";
  } else if (!base::EndsWith(filename, ".pdf", base::CompareCase::INSENSITIVE_ASCII)) {
      // Ensure it actually has the .pdf extension if it was something weird
      filename += ".pdf"; 
  }

  std::string page_title = base::UTF16ToUTF8(web_contents()->GetTitle());

  LOG(INFO) << "[RAG] Sending PDF to ingestion pipeline as: " << filename;

  // 3. Pass the dynamic filename to the unified ingestion pipeline
  service->StartDocumentIngestion(url, page_title, pdf_str, "application/pdf", filename);
}
