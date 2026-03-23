// Copyright 2024 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/rag_ingestion/rag_ingestion_page_action_controller.h"

#include "base/hash/md5.h"
#include "chrome/browser/jatter/jatter_environment.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/rag_ingestion/rag_ingestion_service_factory.h"
#include "chrome/browser/rag_ingestion/rag_ingestion_tab_helper.h"
#include "chrome/browser/ui/browser_finder.h"
#include "chrome/browser/ui/browser_list.h"
#include "chrome/browser/ui/browser_navigator.h"
#include "chrome/browser/ui/browser_navigator_params.h"
#include "chrome/browser/ui/browser_window.h"
#include "chrome/browser/ui/chrome_pages.h"
#include "chrome/browser/ui/location_bar/location_bar.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/browser/ui/views/rag_ingestion/rag_ingestion_bubble_view.h"
#include "chrome/common/webui_url_constants.h"
#include "content/public/browser/web_contents.h"
#include "ui/base/window_open_disposition.h"
#include "url/origin.h"

RagIngestionPageActionController::RagIngestionPageActionController(
    content::WebContents* web_contents)
    : content::WebContentsObserver(web_contents),
      content::WebContentsUserData<RagIngestionPageActionController>(*web_contents) {}

RagIngestionPageActionController::~RagIngestionPageActionController() = default;

Browser* RagIngestionPageActionController::GetBrowser() const {
  content::WebContents* wc = web_contents();
  if (!wc) return nullptr;

  for (Browser* browser : *BrowserList::GetInstance()) {
    if (browser->tab_strip_model()->GetIndexOfWebContents(wc) !=
        TabStripModel::kNoTab) {
      return browser;
    }
  }
  return nullptr;
}

// [NEW] "Ask" State
void RagIngestionPageActionController::ShowOfferState() {
  current_state_ = IconState::kOffer;
  UpdateIcon();
  
 // Check if the tab is currently visible before drawing the bubble.
  if (web_contents() && web_contents()->GetVisibility() == content::Visibility::VISIBLE) {
    ShowBubble();
    is_bubble_pending_ = false;
  } else {
    // Tab is in the background. Flag it to show later.
    is_bubble_pending_ = true;
  }
}

// [NEW] "Active" State
void RagIngestionPageActionController::ShowActiveState() {
  current_state_ = IconState::kActive;
  UpdateIcon();
}

void RagIngestionPageActionController::ShowDisabledState() {
  current_state_ = IconState::kDisabled;
  UpdateIcon();
}

void RagIngestionPageActionController::Hide() {
  current_state_ = IconState::kHidden;
  UpdateIcon();
}

bool RagIngestionPageActionController::ShouldShowIcon() const {
  return current_state_ != IconState::kHidden;
}

void RagIngestionPageActionController::UpdateIcon() {
  Browser* browser = chrome::FindBrowserWithTab(web_contents());
  if (browser && browser->window() && browser->window()->GetLocationBar()) {
    browser->window()->GetLocationBar()->UpdateWithoutTabRestore();
  }
}

void RagIngestionPageActionController::ShowBubble() {
  Browser* browser = GetBrowser();
  if (!browser) return;

  bool show_management_ui = (current_state_ == IconState::kActive || 
                             current_state_ == IconState::kDisabled);
  
  // Double check actual permission status just in case
  content::WebContents* wc = web_contents();
  if (wc) {
    Profile* profile = Profile::FromBrowserContext(wc->GetBrowserContext());
    if (auto* service = RagIngestionServiceFactory::GetForProfile(profile)) {
      auto status = service->GetUserPermission(wc->GetLastCommittedURL());
      if (status == RagIngestionService::UserPermission::kGranted) {
          show_management_ui = true;
      }
    }
  }

  RagIngestionBubbleView::Show(browser, this, show_management_ui);
}

void RagIngestionPageActionController::OnIconClicked() {
  ShowBubble();
}

void RagIngestionPageActionController::OnPermissionUserDecision(bool allowed) {
  content::WebContents* wc = web_contents();
  if (!wc) return;

  GURL url = wc->GetLastCommittedURL();
  Profile* profile = Profile::FromBrowserContext(wc->GetBrowserContext());
  RagIngestionService* service = RagIngestionServiceFactory::GetForProfile(profile);

  if (!service) return;

  // 1. Persist Decision
  RagIngestionService::UserPermission status = 
      allowed ? RagIngestionService::UserPermission::kGranted
              : RagIngestionService::UserPermission::kDenied;

  service->SetUserPermission(url, status);

  // 2. Sync if Allowed
  if (allowed) {
    backend_info_.status = RagIngestionService::BackendStatus::kKnownAllowed;
    backend_info_.is_learning = true;
    backend_info_.can_prompt = false; 
    
    // [FIX] Handle the empty strings from the backend's "undecided" response
    if (backend_info_.canonical_host.empty()) {
        backend_info_.canonical_host = std::string(url.host());
    }
    if (backend_info_.site_name.empty()) {
        // Fall back to the current tab's title since the backend hasn't scraped it yet
        std::u16string title = wc->GetTitle();
        backend_info_.site_name = title.empty() ? std::string(url.host()) : base::UTF16ToUTF8(title);
    }

    // service->AddIngestionUrl(url);
    ShowActiveState();

    if (auto* helper = RagIngestionTabHelper::FromWebContents(wc)) {
        helper->IngestCurrentPage();
    }
  } else {
    // Stay visible, but mark as Disabled
    ShowDisabledState();
  }
}

void RagIngestionPageActionController::OnUpgradeRequired() {
  Browser* browser = GetBrowser();
  if (!browser) return;

  GURL upgrade_url(jatter::kAppUpgradeUrl); 
  WindowOpenDisposition disposition = WindowOpenDisposition::NEW_FOREGROUND_TAB; // Safe default

  // 1. Check what the user is currently looking at
  content::WebContents* current_web_contents = 
      browser->tab_strip_model()->GetActiveWebContents();
      
  if (current_web_contents) {
    GURL current_url = current_web_contents->GetVisibleURL();
    
    // 2. If it's an empty tab, the New Tab Page, or already your web app, 
    // it's safe to overwrite the current tab.
    if (current_url.is_empty() || 
        current_url.spec() == chrome::kChromeUINewTabURL ||
        current_url.DomainIs(jatter::kAppDomain)) {
      disposition = WindowOpenDisposition::CURRENT_TAB;
    }
  }

  // 3. Execute the smart navigation
  NavigateParams params(browser, upgrade_url, ui::PAGE_TRANSITION_LINK);
  params.disposition = disposition;
  Navigate(&params);
}

void RagIngestionPageActionController::OpenSettings() {
  Browser* browser = GetBrowser();
  content::WebContents* wc = web_contents();
  
  if (browser && wc) {
    chrome::ShowSiteSettings(browser, wc->GetLastCommittedURL());
  }
}

void RagIngestionPageActionController::OnVisibilityChanged(content::Visibility visibility) {
  if (visibility == content::Visibility::VISIBLE) {
    // The user just switched TO this tab.
    if (is_bubble_pending_ && current_state_ == IconState::kOffer) {
      ShowBubble();
      is_bubble_pending_ = false; // Reset the flag
    }
  } else {
    // The user just switched AWAY from this tab.
    // Chromium's LocationBarBubbleDelegateView automatically closes standard 
    // bubbles when a tab loses focus, so we don't need to manually destroy it.
    // We just need to flag it to re-open when they return, assuming they 
    // haven't made a decision yet.
    if (current_state_ == IconState::kOffer) {
      is_bubble_pending_ = true;
    }
  }
}

WEB_CONTENTS_USER_DATA_KEY_IMPL(RagIngestionPageActionController);