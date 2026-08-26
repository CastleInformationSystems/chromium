#import "ios/chrome/browser/jatter/rag_ingestion_tab_helper.h"

#include <set>

#import "base/functional/bind.h"
#import "base/functional/callback.h"
#import "base/strings/string_split.h"
#import "base/strings/string_util.h"
#import "base/strings/sys_string_conversions.h"
#import "base/strings/utf_string_conversions.h"
#import "base/time/time.h"
#import "components/content_settings/core/browser/host_content_settings_map.h"
#import "components/content_settings/core/common/content_settings.h"
#import "components/content_settings/core/common/content_settings_types.h"
#import "ios/chrome/browser/content_settings/model/host_content_settings_map_factory.h"
#import "ios/chrome/browser/jatter/jatter_environment.h"
#import "ios/chrome/browser/jatter/rag_commands.h"
#import "ios/chrome/browser/jatter/rag_ingestion_java_script_feature.h"
#import "ios/chrome/browser/jatter/rag_ingestion_service_factory.h"
#import "ios/chrome/browser/shared/model/profile/profile_ios.h"
#import "ios/chrome/browser/shared/model/browser/browser.h"
#import "ios/chrome/browser/shared/model/browser/browser_list.h"
#import "ios/chrome/browser/shared/model/browser/browser_list_factory.h"
#import "ios/chrome/browser/shared/model/web_state_list/web_state_list.h"
#import "ios/chrome/browser/shared/public/commands/command_dispatcher.h"
#import "ios/web/public/navigation/navigation_context.h"
#import "ios/web/public/web_state.h"


RagIngestionTabHelper::RagIngestionTabHelper(web::WebState* web_state)
    : web_state_(web_state) {
  web_state_->AddObserver(this);
}

RagIngestionTabHelper::~RagIngestionTabHelper() {
  if (web_state_) {
    web_state_->RemoveObserver(this);
  }
}

void RagIngestionTabHelper::WebStateDestroyed(web::WebState* web_state) {
  ingestion_timer_.Stop();
  web_state_->RemoveObserver(this);
  web_state_ = nullptr;
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

void RagIngestionTabHelper::DidFinishNavigation(
    web::WebState* web_state,
    web::NavigationContext* navigation_context) {
  
  if (!navigation_context->HasCommitted() || !navigation_context->IsSameDocument()) {
    return;
  }

  // Handle SPAs (same-document navigations like Notion or Twitter)
  ingestion_timer_.Stop();
  GURL url = web_state->GetVisibleURL();
  
  ingestion_timer_.Start(
      FROM_HERE, base::Milliseconds(500),
      base::BindOnce(&RagIngestionTabHelper::DocumentReadyForExtraction,
                     weak_factory_.GetWeakPtr(), url));
}

void RagIngestionTabHelper::PageLoaded(
    web::WebState* web_state,
    web::PageLoadCompletionStatus load_success) {
  
  if (load_success != web::PageLoadCompletionStatus::SUCCESS) {
    return;
  }

  ingestion_timer_.Stop();
  GURL url = web_state->GetVisibleURL();

  // Reset state on clean page load
  live_text_content_.clear();
  captured_links_.clear();
  has_live_text_ = false;

  // 500ms delay to let render thread settle (matches desktop DocumentOnLoadCompleted)
  ingestion_timer_.Start(
      FROM_HERE, base::Milliseconds(500),
      base::BindOnce(&RagIngestionTabHelper::DocumentReadyForExtraction,
                     weak_factory_.GetWeakPtr(), url));
}

void RagIngestionTabHelper::DocumentReadyForExtraction(const GURL& url) {
  if (ShouldIgnoreUrl(url)) return;

  current_check_url_ = url;
  
  NSLog(@"[RAG-iOS] Document ready for: %s. Starting extraction...", url.spec().c_str());
  AttemptLiveTextExtraction(1);
}

void RagIngestionTabHelper::AttemptLiveTextExtraction(int attempt) {
  if (!web_state_) return;

  NSLog(@"[RAG-iOS] Attempting live text extraction (Attempt %d/6)...", attempt);

  RagIngestionJavaScriptFeature::GetInstance()->ExtractTextAndLinks(
      web_state_,
      base::BindOnce(&RagIngestionTabHelper::OnLiveTextCaptured,
                     weak_factory_.GetWeakPtr(), attempt));
}

void RagIngestionTabHelper::OnLiveTextCaptured(int attempt, const base::Value* result) {
  if (!result) {
    NSLog(@"[RAG-iOS] ERROR (Attempt %d/6): JS execution returned NULL! __gCrWeb.ragIngestion was not found.", attempt);
  } else if (!result->is_dict()) {
    NSLog(@"[RAG-iOS] ERROR (Attempt %d/6): JS returned non-dictionary type: %d", attempt, (int)result->type());
  } else {
    const auto& dict = result->GetDict();
    
    // 1. Parse Text
    if (const std::string* text = dict.FindString("text")) {
      live_text_content_ = *text;
      live_text_content_ = base::CollapseWhitespaceASCII(live_text_content_, false);
    }

    // 2. Parse Links
    if (const base::ListValue* links = dict.FindList("links")) {
      captured_links_.clear();
      for (const auto& item : *links) {
        if (item.is_string()) {
          captured_links_.push_back(base::ToLowerASCII(item.GetString()));
        }
      }
    }
  }

  NSLog(@"[RAG-iOS] (Attempt %d/6) Extracted %zu chars and %zu links from: %s", 
        attempt, live_text_content_.length(), captured_links_.size(), current_check_url_.spec().c_str());

  // RETRY LOGIC: If text is suspiciously small, retry up to 6 times
  if (live_text_content_.length() < 500 && attempt < 6) {
    ingestion_timer_.Start(
        FROM_HERE, base::Milliseconds(500),
        base::BindOnce(&RagIngestionTabHelper::AttemptLiveTextExtraction,
                       weak_factory_.GetWeakPtr(), attempt + 1));
    return; 
  }

  if (live_text_content_.length() < 50) {
    NSLog(@"[RAG-iOS] Live text empty after retries. Aborting pipeline.");
    return;
  }

  has_live_text_ = true;
  NSLog(@"[RAG-iOS] SUCCESS: Locked in %zu characters! Ready for Phase 2 (Anon Loader).", live_text_content_.length());

  // [NEW] Trigger the Anon Loader
  ProfileIOS* profile = ProfileIOS::FromBrowserState(web_state_->GetBrowserState());
  anon_loader_ = std::make_unique<AnonPageLoader>(
      profile, 
      current_check_url_,
      base::BindOnce(&RagIngestionTabHelper::OnAnonTextCaptured,
                     weak_factory_.GetWeakPtr())
  );
  anon_loader_->Start();
}

void RagIngestionTabHelper::OnAnonTextCaptured(const AnonPageResult& result) {
  anon_text_content_ = base::CollapseWhitespaceASCII(result.inner_text, false);
  
  // Store these for CalculateNetworkScore() later
  anon_response_code_ = result.http_status_code;
  anon_final_url_ = result.final_url;

  if (anon_text_content_.length() < 50) {
    NSLog(@"[RAG-iOS] Anon text is empty/tiny on a %d status. Likely an SPA or blocked.", anon_response_code_);
    anon_text_content_.clear(); // Force exactly empty for CompareContentState's SPA check
  }

  // Tell the state machine we finished the fetch phase
  has_anon_text_ = true;
  NSLog(@"[RAG-iOS] Anon fetch complete. Stripped to %zu characters.", anon_text_content_.length());
  
  if (has_live_text_) {
    CompareContentState();
  }
}

void RagIngestionTabHelper::CompareContentState() {
  double similarity = CalculateJaccardSimilarity(live_text_content_, anon_text_content_);
  
  NSLog(@"[RAG-iOS] === JACCARD SIMILARITY: %f ===", similarity);

  BOOL is_paywalled = similarity < 0.3;
  if (is_paywalled) {
    NSLog(@"[RAG-iOS] OVERLAY DETECTED: Page is likely behind an auth wall or paywall.");

    ReportAuthWall();

    // // --- Bridge to the UI Layer ---
    // ProfileIOS* profile = ProfileIOS::FromBrowserState(web_state_->GetBrowserState());
    // BrowserList* browser_list = BrowserListFactory::GetForProfile(profile);

    // Browser* active_browser = nullptr;
    
    // // Find the Browser window that currently owns our specific WebState
    // for (Browser* browser : browser_list->BrowsersOfType(BrowserList::BrowserType::kRegular)) {
    //   if (browser->GetWebStateList()->GetIndexOfWebState(web_state_) != WebStateList::kInvalidIndex) {
    //     active_browser = browser;
    //     break;
    //   }
    // }

    // if (active_browser) {
    //   id<RagCommands> rag_handler = 
    //       HandlerForProtocol(active_browser->GetCommandDispatcher(), RagCommands);
      
    //   NSString* host_payload = base::SysUTF8ToNSString(current_check_url_.host());

    //   // Create a repeating callback (Objective-C blocks require copyable captures)
    //   auto callback = base::BindRepeating(&RagIngestionTabHelper::OnPermissionDecision, 
    //                                       weak_factory_.GetWeakPtr(), 
    //                                       current_check_url_);
      
    //   // Wrap it in an Objective-C block
    //   RagPermissionDecisionBlock decision_block = ^(BOOL allowed) {
    //     callback.Run(allowed);
    //   };

    //   // Dispatch the command!
    //   [rag_handler showRagPermissionUIForHost:host_payload decisionHandler:decision_block];
    // }
  } else {
    NSLog(@"[RAG-iOS] OPEN ACCESS: Page is freely accessible natively.");
  }
}

double RagIngestionTabHelper::CalculateJaccardSimilarity(const std::string& text1, const std::string& text2) {
  // Split both strings into lowercased word tokens
  std::vector<std::string> words1 = base::SplitString(
      base::ToLowerASCII(text1), base::kWhitespaceASCII, 
      base::TRIM_WHITESPACE, base::SPLIT_WANT_NONEMPTY);
      
  std::vector<std::string> words2 = base::SplitString(
      base::ToLowerASCII(text2), base::kWhitespaceASCII, 
      base::TRIM_WHITESPACE, base::SPLIT_WANT_NONEMPTY);

  if (words1.empty() && words2.empty()) return 1.0;
  if (words1.empty() || words2.empty()) return 0.0;

  // Convert to unique sets
  std::set<std::string> set1(words1.begin(), words1.end());
  std::set<std::string> set2(words2.begin(), words2.end());

  // Calculate intersection
  size_t intersection_size = 0;
  for (const auto& word : set1) {
    if (set2.find(word) != set2.end()) {
      intersection_size++;
    }
  }

  // Calculate union: size(A) + size(B) - size(A ∩ B)
  size_t union_size = set1.size() + set2.size() - intersection_size;

  return static_cast<double>(intersection_size) / static_cast<double>(union_size);
}

void RagIngestionTabHelper::OnPermissionDecision(const GURL& url, bool allowed) {
  ProfileIOS* profile = ProfileIOS::FromBrowserState(web_state_->GetBrowserState());
  RagIngestionService* service = RagIngestionServiceFactory::GetForProfile(profile);

  if (allowed) {
    LOG(INFO) << "[RAG-iOS] User manually GRANTED permission for " << url.host();
    NSLog(@"[RAG-iOS] User Granted Permission. Triggering pipeline.");
    
    icon_state_ = IconState::kActive;
    
    if (service) {
      service->SetUserPermission(url, RagIngestionService::UserPermission::kGranted);
      if (has_live_text_) {
        std::string title = base::UTF16ToUTF8(web_state_->GetTitle());
        service->StartPassiveLearningPipeline(url, std::string(url.host()), title, live_text_content_);
      }
    }
  } else {
    LOG(INFO) << "[RAG-iOS] User manually DENIED permission for " << url.host();
    NSLog(@"[RAG-iOS] User Denied Permission.");
    
    icon_state_ = IconState::kDisabled;
    
    if (service) {
      service->SetUserPermission(url, RagIngestionService::UserPermission::kDenied);
    }
  }
  
  // Push the final user decision to the Omnibox immediately
  UpdateLocationBarUI();
}

void RagIngestionTabHelper::ReportAuthWall() {
  if (!web_state_) return;

  std::string url_spec = current_check_url_.spec();
  auto it = last_auth_ping_time_.find(url_spec);
  
  // 3-second cooldown to prevent API spam on aggressive reloads
  if (it != last_auth_ping_time_.end() && 
     (base::Time::Now() - it->second < base::Seconds(3))) {
      NSLog(@"[RAG-iOS] Auth Wall API throttled (Micro-cache).");
      return; 
  }
  last_auth_ping_time_[url_spec] = base::Time::Now();

  ProfileIOS* profile = ProfileIOS::FromBrowserState(web_state_->GetBrowserState());
  RagIngestionService* service = RagIngestionServiceFactory::GetForProfile(profile);
  if (!service) return;

  NSLog(@"[RAG-iOS] Auth Wall confirmed. Querying backend for source-of-truth status: %s", url_spec.c_str());

  service->CheckBackendStatus(
      current_check_url_,
      base::BindOnce(&RagIngestionTabHelper::OnPermissionCheckComplete,
                     weak_factory_.GetWeakPtr()));
}

void RagIngestionTabHelper::OnPermissionCheckComplete(
    RagIngestionService::BackendPermissionInfo info) {
  if (!web_state_) return;

  ProfileIOS* profile = ProfileIOS::FromBrowserState(web_state_->GetBrowserState());
  RagIngestionService* service = RagIngestionServiceFactory::GetForProfile(profile);
  if (!service) return;
  
  std::string host = info.canonical_host.empty() ? std::string(current_check_url_.host()) : info.canonical_host;
  std::string title = base::UTF16ToUTF8(web_state_->GetTitle());

  if (info.status == RagIngestionService::BackendStatus::kKnownAllowed) {
    LOG(INFO) << "[RAG-iOS] Backend SSOT: Explicit ALLOW for " << host;
    NSLog(@"[RAG-iOS] Backend Allowed: Sending text to passive learning pipeline.");
    
    service->SyncLocalSettingFromBackend(current_check_url_, RagIngestionService::UserPermission::kGranted);
    
    icon_state_ = IconState::kActive;
    UpdateLocationBarUI();
    
    service->StartPassiveLearningPipeline(current_check_url_, host, title, live_text_content_);
  } 
  else if (info.status == RagIngestionService::BackendStatus::kKnownBlocked) {
    LOG(INFO) << "[RAG-iOS] Backend SSOT: Explicit DENY for " << host;
    NSLog(@"[RAG-iOS] Backend Blocked: Aborting ingestion.");
    
    service->SyncLocalSettingFromBackend(current_check_url_, RagIngestionService::UserPermission::kDenied);
    
    icon_state_ = IconState::kDisabled;
    UpdateLocationBarUI();
  }
  else if (info.status == RagIngestionService::BackendStatus::kUnknown) {
    LOG(INFO) << "[RAG-iOS] Backend SSOT: UNKNOWN for " << host << ". Checking local fallback.";
    RagIngestionService::UserPermission local_status = service->GetUserPermission(current_check_url_);
    
    if (local_status == RagIngestionService::UserPermission::kGranted) {
      LOG(INFO) << "[RAG-iOS] Local fallback: ALLOWED.";
      NSLog(@"[RAG-iOS] Local Allowed: Sending text to passive learning pipeline.");
      
      icon_state_ = IconState::kActive;
      UpdateLocationBarUI();
      
      service->StartPassiveLearningPipeline(current_check_url_, host, title, live_text_content_);
    } 
    else if (local_status == RagIngestionService::UserPermission::kDenied) {
      LOG(INFO) << "[RAG-iOS] Local fallback: BLOCKED.";
      NSLog(@"[RAG-iOS] Local Blocked: Aborting ingestion.");
      
      icon_state_ = IconState::kDisabled;
      UpdateLocationBarUI();
    } 
    else {
      LOG(INFO) << "[RAG-iOS] Local fallback: UNKNOWN. Prompting user.";
      NSLog(@"[RAG-iOS] Unknown State: Triggering UI Prompt.");
      
      icon_state_ = IconState::kOffer;
      UpdateLocationBarUI();
      
      ShowPermissionUI(); 
    }
  }
}

void RagIngestionTabHelper::ShowPermissionUI() {
  ProfileIOS* profile = ProfileIOS::FromBrowserState(web_state_->GetBrowserState());
  BrowserList* browser_list = BrowserListFactory::GetForProfile(profile);

  Browser* active_browser = nullptr;
  for (Browser* browser : browser_list->BrowsersOfType(BrowserList::BrowserType::kRegular)) {
    if (browser->GetWebStateList()->GetIndexOfWebState(web_state_) != WebStateList::kInvalidIndex) {
      active_browser = browser;
      break;
    }
  }

  if (active_browser) {
    id<RagCommands> rag_handler = 
        HandlerForProtocol(active_browser->GetCommandDispatcher(), RagCommands);
    
    NSString* host_payload = base::SysUTF8ToNSString(current_check_url_.host());
    NSString* site_name = base::SysUTF16ToNSString(web_state_->GetTitle());

    if (icon_state_ == IconState::kOffer) {
      // STATE: Ask for permission (Drop-Down Banner)
      auto callback = base::BindRepeating(&RagIngestionTabHelper::OnPermissionDecision, 
                                          weak_factory_.GetWeakPtr(), 
                                          current_check_url_);
      
      RagPermissionDecisionBlock decision_block = ^(BOOL allowed) {
        callback.Run(allowed);
      };

      [rag_handler showRagPermissionUIForHost:host_payload decisionHandler:decision_block];
      
    } else if (icon_state_ == IconState::kActive || icon_state_ == IconState::kDisabled) {
      // STATE: Already decided (Management Bottom Sheet)
      BOOL is_enabled = (icon_state_ == IconState::kActive);
      
      [rag_handler showRagManagementUIForHost:host_payload 
                                     siteName:site_name 
                                    isEnabled:is_enabled];
    }
  }
}

void RagIngestionTabHelper::DidStartNavigation(
    web::WebState* web_state,
    web::NavigationContext* navigation_context) {
  // Ignore same-document navigations (like single-page app anchor scrolls)
  if (navigation_context->IsSameDocument()) {
    return;
  }

  // Reset state for the new page load
  icon_state_ = IconState::kHidden;
  
  // Tell the Location Bar to hide the icon immediately
  UpdateLocationBarUI(); 
}

void RagIngestionTabHelper::UpdateLocationBarUI() {
  if (!web_state_) return;
  
  ProfileIOS* profile = ProfileIOS::FromBrowserState(web_state_->GetBrowserState());
  BrowserList* browser_list = BrowserListFactory::GetForProfile(profile);

  for (Browser* browser : browser_list->BrowsersOfType(BrowserList::BrowserType::kRegular)) {
    if (browser->GetWebStateList()->GetIndexOfWebState(web_state_) != WebStateList::kInvalidIndex) {
      // 1. Fetch the specific handler for the Location Bar
      id<RagLocationBarCommands> location_bar_handler = 
          HandlerForProtocol(browser->GetCommandDispatcher(), RagLocationBarCommands);
      
      [location_bar_handler updateRagLocationBarIcon];
      break;
    }
  }
}

void RagIngestionTabHelper::WasShown(web::WebState* web_state) {
  // Whenever this tab is brought to the foreground, push its current 
  // icon_state_ (whether it's kHidden, kActive, etc.) to the Location Bar.
  UpdateLocationBarUI();
}
