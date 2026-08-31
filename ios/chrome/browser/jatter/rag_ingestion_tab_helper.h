// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef IOS_CHROME_BROWSER_RAG_INGESTION_RAG_INGESTION_TAB_HELPER_H_
#define IOS_CHROME_BROWSER_RAG_INGESTION_RAG_INGESTION_TAB_HELPER_H_

#include <string>
#include <vector>

#include "base/memory/weak_ptr.h"
#include "base/timer/timer.h"
#include "base/values.h"
#include "ios/chrome/browser/jatter/rag_ingestion_anon_loader.h"
#import "ios/chrome/browser/jatter/rag_ingestion_service.h"
#include "ios/web/public/web_state_observer.h"
#include "ios/web/public/web_state_user_data.h"
#include "url/gurl.h"

namespace web {
class WebState;
class NavigationContext;
}  // namespace web

class RagIngestionTabHelper : public web::WebStateObserver,
                              public web::WebStateUserData<RagIngestionTabHelper> {
 public:
  ~RagIngestionTabHelper() override;

  RagIngestionTabHelper(const RagIngestionTabHelper&) = delete;
  RagIngestionTabHelper& operator=(const RagIngestionTabHelper&) = delete;

  // web::WebStateObserver implementation:
  void DidFinishNavigation(web::WebState* web_state,
                           web::NavigationContext* navigation_context) override;
  void PageLoaded(web::WebState* web_state,
                  web::PageLoadCompletionStatus load_success) override;
  void WebStateDestroyed(web::WebState* web_state) override;

  const std::string& live_text_content() const { return live_text_content_; }
  bool has_live_text() const { return has_live_text_; }

  enum class IconState { 
    kHidden,   // Default: No icon
    kActive,   // Green: Allowed and ingesting
    kDisabled, // Gray: Explicitly blocked
    kOffer     // Prompt: Needs user permission
  };

  IconState GetIconState() const { return icon_state_; }

  void ShowPermissionUI();

 private:
  explicit RagIngestionTabHelper(web::WebState* web_state);
  friend class web::WebStateUserData<RagIngestionTabHelper>;

  // Internal extraction flow
  void DocumentReadyForExtraction(const GURL& url);
  void AttemptLiveTextExtraction(int attempt);

  void OnAnonTextCaptured(const AnonPageResult& result);
  void OnLiveTextCaptured(int attempt, const base::Value* result);

  void CompareContentState();
  double CalculateJaccardSimilarity(const std::string& text1, const std::string& text2);

  void OnPermissionDecision(const GURL& url, bool allowed);

  // [NEW] Backend Check methods
  void ReportAuthWall();
  void OnPermissionCheckComplete(RagIngestionService::BackendPermissionInfo info);

  bool ShouldIgnoreUrl(const GURL& url);

  // Override from web::WebStateObserver
  void DidStartNavigation(web::WebState* web_state,
                          web::NavigationContext* navigation_context) override;

  void UpdateLocationBarUI();
  void WasShown(web::WebState* web_state) override;

  raw_ptr<web::WebState> web_state_ = nullptr;
  
  // State management
  GURL current_check_url_;
  std::string live_text_content_;
  std::vector<std::string> captured_links_;
  bool has_live_text_ = false;

  std::unique_ptr<AnonPageLoader> anon_loader_;
  std::string anon_text_content_;
  int anon_response_code_ = 0;
  GURL anon_final_url_;
  bool has_anon_text_ = false;

  // [NEW] Micro-cache to prevent API spam on rapid reloads
  std::map<std::string, base::Time> last_auth_ping_time_;

  // Timers and weak pointer factory for asynchronous JS execution callbacks
  base::OneShotTimer ingestion_timer_;

  IconState icon_state_ = IconState::kHidden;

  base::WeakPtrFactory<RagIngestionTabHelper> weak_factory_{this};
};

#endif  // IOS_CHROME_BROWSER_RAG_INGESTION_RAG_INGESTION_TAB_HELPER_H_