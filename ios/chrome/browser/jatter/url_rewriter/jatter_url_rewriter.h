#ifndef IOS_CHROME_BROWSER_JATTER_URL_REWRITER_JATTER_URL_REWRITER_H_
#define IOS_CHROME_BROWSER_JATTER_URL_REWRITER_JATTER_URL_REWRITER_H_

class GURL;

namespace web {
class BrowserState;
}

// URL rewriter that intercepts navigation to the standard Chromium New Tab Page
// and redirects it to the custom Jatter application endpoint.
bool WillRewriteNewTabPageToJatter(GURL* url,
                                   web::BrowserState* browser_state);

#endif  // IOS_CHROME_BROWSER_JATTER_URL_REWRITER_JATTER_URL_REWRITER_H_