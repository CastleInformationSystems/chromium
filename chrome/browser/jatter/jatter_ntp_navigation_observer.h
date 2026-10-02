#ifndef CHROME_BROWSER_JATTER_JATTER_NTP_NAVIGATION_OBSERVER_H_
#define CHROME_BROWSER_JATTER_JATTER_NTP_NAVIGATION_OBSERVER_H_

#include "base/strings/string_util.h"
#include "chrome/browser/jatter/jatter_environment.h"
#include "chrome/browser/jatter/jatter_firebase_client.h"
#include "chrome/browser/jatter/jatter_firebase_client_factory.h"
#include "chrome/common/pref_names.h"
#include "chrome/common/webui_url_constants.h"
#include "components/prefs/pref_service.h"
#include "content/public/browser/navigation_handle.h"
#include "content/public/browser/web_contents_observer.h"
#include "content/public/browser/web_contents_user_data.h"
#include "url/gurl.h"

class JatterNtpNavigationObserver 
    : public content::WebContentsObserver,
      public content::WebContentsUserData<JatterNtpNavigationObserver> {
 public:
  ~JatterNtpNavigationObserver() override;

  void DidFinishNavigation(content::NavigationHandle* navigation_handle) override;

 private:
  friend class content::WebContentsUserData<JatterNtpNavigationObserver>;
  
  // Constructor declaration
  explicit JatterNtpNavigationObserver(content::WebContents* web_contents);

  WEB_CONTENTS_USER_DATA_KEY_DECL();
};

#endif
