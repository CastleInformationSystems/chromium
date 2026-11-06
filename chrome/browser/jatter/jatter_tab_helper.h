#ifndef CHROME_BROWSER_JATTER_JATTER_TAB_HELPER_H_
#define CHROME_BROWSER_JATTER_JATTER_TAB_HELPER_H_

#include "base/time/time.h"
#include "base/timer/timer.h"
#include "content/public/browser/web_contents_observer.h"
#include "content/public/browser/web_contents_user_data.h"

class JatterTabHelper : public content::WebContentsObserver,
                        public content::WebContentsUserData<JatterTabHelper> {
 public:
  ~JatterTabHelper() override;

  JatterTabHelper(const JatterTabHelper&) = delete;
  JatterTabHelper& operator=(const JatterTabHelper&) = delete;

 private:
  explicit JatterTabHelper(content::WebContents* web_contents);
  friend class content::WebContentsUserData<JatterTabHelper>;

  // content::WebContentsObserver overrides:
  void DidFinishNavigation(content::NavigationHandle* navigation_handle) override;
  void OnVisibilityChanged(content::Visibility visibility) override;
  void WebContentsDestroyed() override;

  void FlushToNetwork(bool is_final_flush);

  base::TimeTicks last_active_time_;
  base::TimeDelta accumulated_duration_;
  int64_t current_nav_entry_id_ = 0;
  
  // Debounces network flushes when a user rapidly switches tabs.
  base::OneShotTimer debounce_timer_;

  WEB_CONTENTS_USER_DATA_KEY_DECL();
};

#endif  // CHROME_BROWSER_JATTER_JATTER_TAB_HELPER_H_
