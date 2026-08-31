#import "ios/chrome/browser/jatter/url_rewriter/jatter_url_rewriter.h"

#import "ios/chrome/browser/jatter/jatter_environment.h"
#import "ios/chrome/browser/shared/model/url/chrome_url_constants.h"
#import "ios/web/public/browser_state.h"
#import "url/gurl.h"

bool WillRewriteNewTabPageToJatter(GURL* url,
                                   web::BrowserState* browser_state) {
  // Do not rewrite in incognito mode (avoids accidental data leakage).
  if (browser_state->IsOffTheRecord()) {
    return false;
  }

  // Only intercept the standard Chromium NTP schema.
  if (url->host() == kChromeUINewTabHost && url->SchemeIs(kChromeUIScheme)) {
    
    // Inject the centralized Jatter URL (ensure namespace is correct if applicable).
    GURL jatter_url(jatter::kAppUrl); 
    
    if (jatter_url.is_valid() && jatter_url != *url) {
      *url = jatter_url;
      return true;
    }
  }
  return false;
}