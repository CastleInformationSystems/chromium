// Copyright 2024 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_UI_RAG_INGESTION_RAG_INGESTION_PAGE_ACTION_CONTROLLER_H_
#define CHROME_BROWSER_UI_RAG_INGESTION_RAG_INGESTION_PAGE_ACTION_CONTROLLER_H_

#include "base/memory/raw_ptr.h"
#include "chrome/browser/rag_ingestion/rag_ingestion_service.h"
#include "chrome/browser/ui/browser.h"
#include "content/public/browser/web_contents_observer.h"
#include "content/public/browser/web_contents_user_data.h"

class RagIngestionPageActionController
    : public content::WebContentsObserver,
      public content::WebContentsUserData<RagIngestionPageActionController> {
 public:
  enum class IconState {
    kHidden,
    kOffer,   // "Ask" state (Grey/Outline)
    kActive,   // "Ingesting" state (Purple/Solid)
    kDisabled
  };

  ~RagIngestionPageActionController() override;

  // Store and retrieve the backend info
  void SetBackendInfo(const RagIngestionService::BackendPermissionInfo& info) {
      backend_info_ = info;
  }
  const RagIngestionService::BackendPermissionInfo& GetBackendInfo() const {
      return backend_info_;
  }

  // Distinct states called by TabHelper
  void ShowOfferState();
  void ShowActiveState();
  void ShowDisabledState();

  // Resets state to hidden (e.g. when page becomes public)
  void Hide();

  // Called by the UI when the user clicks the icon
  void OnIconClicked();

  // Returns true if the icon should be visible in the Omnibox
  bool ShouldShowIcon() const;
  
  // Returns the current state for the Icon View to decide color/icon
  IconState GetIconState() const { return current_state_; }

  // Called by the View when user clicks the icon
  void ShowBubble();
  
  // Called by the Bubble View
  void OnPermissionUserDecision(bool allowed);

  // Handle the case where the user clicks Allow, but needs an upgrade
  void OnUpgradeRequired();

  void OpenSettings();

 private:
  friend class content::WebContentsUserData<RagIngestionPageActionController>;
  explicit RagIngestionPageActionController(content::WebContents* web_contents);
  
  void UpdateIcon();

  // [NEW] Override the visibility hook from WebContentsObserver
  void OnVisibilityChanged(content::Visibility visibility) override;

  // Helper to find the browser instance
  Browser* GetBrowser() const;

  RagIngestionService::BackendPermissionInfo backend_info_;

  // State of the icon for the current page
  IconState current_state_ = IconState::kHidden;
   // [NEW] State tracker for background loading
  bool is_bubble_pending_ = false;

  WEB_CONTENTS_USER_DATA_KEY_DECL();
};

#endif  // CHROME_BROWSER_UI_RAG_INGESTION_RAG_INGESTION_PAGE_ACTION_CONTROLLER_H_