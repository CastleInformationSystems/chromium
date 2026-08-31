#ifndef IOS_CHROME_BROWSER_JATTER_ANALYTICS_JATTER_ANALYTICS_TAB_HELPER_H_
#define IOS_CHROME_BROWSER_JATTER_ANALYTICS_JATTER_ANALYTICS_TAB_HELPER_H_

#include "base/memory/raw_ptr.h"
#include "ios/web/public/web_state_observer.h"
#include "ios/web/public/web_state_user_data.h"

class JatterAnalyticsTabHelper : public web::WebStateObserver,
                                 public web::WebStateUserData<JatterAnalyticsTabHelper> {
 public:
  ~JatterAnalyticsTabHelper() override;

 private:
  friend class web::WebStateUserData<JatterAnalyticsTabHelper>;
  explicit JatterAnalyticsTabHelper(web::WebState* web_state);

  // web::WebStateObserver:
  void DidFinishNavigation(web::WebState* web_state,
                           web::NavigationContext* navigation_context) override;
  void WebStateDestroyed(web::WebState* web_state) override;

  raw_ptr<web::WebState> web_state_;
};

#endif  // IOS_CHROME_BROWSER_JATTER_ANALYTICS_JATTER_ANALYTICS_TAB_HELPER_H_