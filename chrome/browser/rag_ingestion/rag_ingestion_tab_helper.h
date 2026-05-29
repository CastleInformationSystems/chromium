// Copyright 2024 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_RAG_INGESTION_RAG_INGESTION_TAB_HELPER_H_
#define CHROME_BROWSER_RAG_INGESTION_RAG_INGESTION_TAB_HELPER_H_

#include <map>
#include <optional>
#include <set>
#include <string>
#include <vector>

#include "base/memory/weak_ptr.h"
#include "base/time/time.h"
#include "base/timer/timer.h"
#include "chrome/browser/rag_ingestion/rag_ingestion_anon_loader.h"
#include "chrome/browser/rag_ingestion/rag_ingestion_service.h"
#include "content/public/browser/web_contents_observer.h"
#include "content/public/browser/web_contents_user_data.h"
#include "services/network/public/cpp/simple_url_loader.h"
#include "ui/accessibility/ax_mode.h"
#include "ui/accessibility/ax_tree_update.h"
#include "url/gurl.h"

namespace content {
class WebContents;
class NavigationHandle;
}  // namespace content

class RagIngestionTabHelper
    : public content::WebContentsObserver,
      public content::WebContentsUserData<RagIngestionTabHelper> {
 public:
  ~RagIngestionTabHelper() override;
  RagIngestionTabHelper(const RagIngestionTabHelper&) = delete;
  RagIngestionTabHelper& operator=(const RagIngestionTabHelper&) = delete;

  // content::WebContentsObserver overrides:
  void DidFinishNavigation(
      content::NavigationHandle* navigation_handle) override;
  void DocumentOnLoadCompletedInPrimaryMainFrame() override;
  // void DOMContentLoaded(content::RenderFrameHost* render_frame_host) override;

  void IngestCurrentPage(int retry_count = 0);

 private:
  explicit RagIngestionTabHelper(content::WebContents* web_contents);
  friend class content::WebContentsUserData<RagIngestionTabHelper>;

  // --- Filters & Caching ---
  bool ShouldIgnoreUrl(const GURL& url);
  bool ShouldSkipCheckDueToCache(const GURL& url);

  // --- Execution Flow ---
  void CheckAuthWall(const GURL& url);
  void ReportAuthWall();

  // // Step 1: Extract "Live" text (What user sees) via Accessibility Tree
  // void ExtractLivePageText();
  // void OnAXSnapshotReceived(ui::AXTreeUpdate& snapshot);

  // // Step 2: Fetch "Anonymous" HTML (What bot sees) via Network
  // void FetchAnonymousPage(const GURL& url);
  // void OnAnonymousRequestComplete(std::optional<std::string> response_body);

  // // Step 3: Core Logic (ZAP Integrated)
  // void CalculateAuthConfidence();

  // --- Scoring Helpers ---
  double CalculateNetworkScore();  // 401, 403, Redirects
  // double CalculateContentScore();  // Jaccard Similarity

  // [ZAP Integration] - "Indicator Pattern" & "Verification Detection"
  double CalculateHeuristicScore();

  void AttemptLiveTextExtraction(int attempt);

  // Helper to trigger the network check after heuristics pass
  void StartAnonPageCheck();

  bool PageHasLinkContaining(const std::string& partial_text);

  void FetchPdfBytesForIngestion(const GURL& url);
  void OnPdfBytesFetched(const GURL& url, std::optional<std::string> response_body);

  // --- Text Utilities ---
  // std::string CleanText(const std::string& input);
  // std::string StripHtmlTags(const std::string& html);
  std::set<std::string> TokenizeText(const std::string& text);
  double CalculateJaccardSimilarity(const std::set<std::string>& set_a,
                                    const std::set<std::string>& set_b);

  void DocumentReadyForExtraction(const GURL& url);

  void OnPermissionCheckComplete(
      RagIngestionService::BackendPermissionInfo info);

  // [NEW] Parallel Extraction Callbacks
  void OnLiveTextCaptured(int attempt, base::Value result);
  void OnAnonTextCaptured(const AnonPageResult& result);
  void MarkPageAsPublic(double score, const std::string& reason);
  
  // [NEW] Convergence Point
  void CompareContentState();

  std::unique_ptr<AnonPageLoader> anon_loader_;

  // --- State ---
  std::unique_ptr<network::SimpleURLLoader> no_cookies_loader_;
  std::unique_ptr<network::SimpleURLLoader> pdf_loader_;
  GURL current_check_url_;
  GURL anon_final_url_;

  // Domain Caching (ZAP "Poll Frequency")
  std::map<std::string, base::Time> last_check_time_;

  // Data collected for comparison
  std::string live_text_content_; // Changed from optional for simpler logic
  std::string anon_text_content_;
  bool has_live_text_ = false;
  bool has_anon_text_ = false;

  std::vector<std::string> captured_links_;

  std::optional<int> anon_response_code_;

  // Thresholds
  const double kAuthConfidenceThreshold = 0.75;
  const base::TimeDelta kCacheDuration = base::Minutes(10);
  base::OneShotTimer ingestion_timer_;

  base::WeakPtrFactory<RagIngestionTabHelper> weak_factory_{this};

  WEB_CONTENTS_USER_DATA_KEY_DECL();
};

#endif  // CHROME_BROWSER_RAG_INGESTION_RAG_INGESTION_TAB_HELPER_H_