#include "chrome/browser/jatter/jatter_ntp_navigation_observer.h"

// Required macro implementation
WEB_CONTENTS_USER_DATA_KEY_IMPL(JatterNtpNavigationObserver);

// 1. Fix the Base Class Initialization error here:
JatterNtpNavigationObserver::JatterNtpNavigationObserver(content::WebContents* web_contents)
    : content::WebContentsObserver(web_contents),
      content::WebContentsUserData<JatterNtpNavigationObserver>(*web_contents) {
}

JatterNtpNavigationObserver::~JatterNtpNavigationObserver() = default;

void JatterNtpNavigationObserver::DidFinishNavigation(content::NavigationHandle* navigation_handle) {
    if (!navigation_handle->IsInPrimaryMainFrame() || 
        !navigation_handle->HasCommitted() || 
        navigation_handle->IsErrorPage()) {
      return;
    }

    const GURL& url = navigation_handle->GetURL();
    Profile* profile = Profile::FromBrowserContext(web_contents()->GetBrowserContext());

    // Check if the URL matches the Jatter web app URL. 
    // Using base::StartsWith handles cases where the app URL appends query parameters or fragments.
    bool is_ntp = base::StartsWith(url.spec(), jatter::kAppUrl, base::CompareCase::INSENSITIVE_ASCII);

    if (is_ntp) {
      // Factory returns nullptr in Incognito mode
      if (JatterFirebaseClient* client = JatterFirebaseClientFactory::GetForProfile(profile)) {
        client->RefreshPersonalizedAssistance(profile);
      }
    }
}
