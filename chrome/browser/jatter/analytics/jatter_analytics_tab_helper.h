// Copyright 2024 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_JATTER_ANALYTICS_JATTER_ANALYTICS_TAB_HELPER_H_
#define CHROME_BROWSER_JATTER_ANALYTICS_JATTER_ANALYTICS_TAB_HELPER_H_

#include "content/public/browser/web_contents_observer.h"
#include "content/public/browser/web_contents_user_data.h"

namespace content {
class NavigationHandle;
class WebContents;
}  // namespace content

class JatterAnalyticsTabHelper 
    : public content::WebContentsObserver,
      public content::WebContentsUserData<JatterAnalyticsTabHelper> {
 public:
  ~JatterAnalyticsTabHelper() override;

  // content::WebContentsObserver implementation:
  void DidFinishNavigation(content::NavigationHandle* navigation_handle) override;

 private:
  explicit JatterAnalyticsTabHelper(content::WebContents* contents);
  friend class content::WebContentsUserData<JatterAnalyticsTabHelper>;

  WEB_CONTENTS_USER_DATA_KEY_DECL();
};

#endif  // CHROME_BROWSER_JATTER_ANALYTICS_JATTER_ANALYTICS_TAB_HELPER_H_