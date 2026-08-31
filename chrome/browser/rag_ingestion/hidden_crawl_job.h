// Copyright 2024 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_RAG_INGESTION_HIDDEN_CRAWL_JOB_H_
#define CHROME_BROWSER_RAG_INGESTION_HIDDEN_CRAWL_JOB_H_

#include <string>
#include <vector>
#include <memory>
#include <optional>

#include "base/functional/callback.h"
#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "base/timer/timer.h"
#include "content/public/browser/storage_partition_config.h"
#include "content/public/browser/web_contents.h"
#include "content/public/browser/web_contents_delegate.h"
#include "content/public/browser/web_contents_observer.h"
#include "net/cookies/cookie_access_result.h"
#include "services/network/public/cpp/simple_url_loader.h"
#include "services/network/public/mojom/cookie_manager.mojom.h"
#include "ui/accessibility/ax_enums.mojom.h"
#include "ui/accessibility/ax_tree_update.h"
#include "url/gurl.h"

class Profile;

class HiddenCrawlJob : public content::WebContentsObserver,
                       public content::WebContentsDelegate {
 public:
  // [UPDATED] Signature expects: html, inner_text, is_auth_wall
  using DoneCallback =
      base::OnceCallback<void(const std::string&, const std::string&, bool)>;

  HiddenCrawlJob(Profile* profile,
                 const GURL& url,
                 DoneCallback done_cb);
  ~HiddenCrawlJob() override;

  void Start();

  // content::WebContentsObserver:
  void DidFinishLoad(content::RenderFrameHost* render_frame_host,
                     const GURL& validated_url) override;
  void DidFailLoad(content::RenderFrameHost* render_frame_host,
                   const GURL& validated_url,
                   int error_code) override;

 private:
  // --- Step 1: Target URL Validation ---
  void StartAnonFetch();
  void OnAnonFetchComplete(std::optional<std::string> response_body);

  // --- Step 2: Auth Replication ---
  void CopyCookiesAndNavigate();
  void OnCookiesRetrieved(const std::vector<net::CookieWithAccessResult>& cookies,
                          const std::vector<net::CookieWithAccessResult>& excluded_cookies);
  void OnCookiesSet(net::CookieAccessResult result);
  void NavigateToTarget();

  // --- Step 3: Extraction ---
  void OnScriptExecuted(base::Value result);

  // --- Coordination ---
  void CheckFinish();
  void OnTimeout();

  void PollDOM();

  // --- Fallback Extraction ---
  void TryAXTreeFallback();
  void OnAXSnapshotReceived(ui::AXTreeUpdate& snapshot);

  raw_ptr<Profile> profile_;
  GURL target_url_;
  DoneCallback done_cb_;
  
  std::unique_ptr<content::WebContents> web_contents_;
  size_t pending_cookie_writes_ = 0;
  base::OneShotTimer watchdog_;

  // State Trackers
  std::unique_ptr<network::SimpleURLLoader> anon_loader_;
  bool is_anon_complete_ = false;
  bool is_auth_complete_ = false;
  bool is_auth_wall_ = false;

  // Extracted Data
  std::string final_html_;
  std::string final_text_;

  base::RepeatingTimer poll_timer_;
  int poll_attempts_ = 0;

  // For SPA polling
  int stable_ticks_ = 0;
  size_t last_text_length_ = 0;
  std::string best_text_so_far_;

  base::WeakPtrFactory<HiddenCrawlJob> weak_factory_{this};
};

#endif  // CHROME_BROWSER_RAG_INGESTION_HIDDEN_CRAWL_JOB_H_