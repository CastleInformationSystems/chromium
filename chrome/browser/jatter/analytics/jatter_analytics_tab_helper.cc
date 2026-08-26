// Copyright 2024 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/jatter/analytics/jatter_analytics_tab_helper.h"

#include "chrome/browser/jatter/analytics/jatter_analytics_service.h"
#include "chrome/browser/jatter/analytics/jatter_analytics_service_factory.h"
#include "chrome/browser/profiles/profile.h"
#include "content/public/browser/navigation_handle.h"
#include "content/public/browser/web_contents.h"

JatterAnalyticsTabHelper::JatterAnalyticsTabHelper(content::WebContents* contents)
    : content::WebContentsObserver(contents),
      content::WebContentsUserData<JatterAnalyticsTabHelper>(*contents) {}

JatterAnalyticsTabHelper::~JatterAnalyticsTabHelper() = default;

void JatterAnalyticsTabHelper::DidFinishNavigation(
    content::NavigationHandle* navigation_handle) {
  // Only track committed, primary main-frame navigations (ignore subframes and anchor/fragment navigations)
  if (!navigation_handle->HasCommitted() ||
      !navigation_handle->IsInPrimaryMainFrame() ||
      navigation_handle->IsSameDocument()) {
    return;
  }

  Profile* profile =
      Profile::FromBrowserContext(web_contents()->GetBrowserContext());
  if (!profile) {
    return;
  }

  if (auto* analytics =
          JatterAnalyticsServiceFactory::GetForProfile(profile)) {
    analytics->RecordNavigationEvent();
  }
}

WEB_CONTENTS_USER_DATA_KEY_IMPL(JatterAnalyticsTabHelper);