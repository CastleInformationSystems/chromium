#include "chrome/browser/jatter/jatter_tab_helper.h"

#include "base/functional/bind.h"
#include "chrome/browser/jatter/jatter_firebase_client.h"
#include "chrome/browser/jatter/jatter_firebase_client_factory.h"
#include "chrome/browser/profiles/profile.h"
#include "content/public/browser/navigation_controller.h"
#include "content/public/browser/navigation_entry.h"
#include "content/public/browser/navigation_handle.h"
#include "content/public/browser/web_contents.h"

JatterTabHelper::JatterTabHelper(content::WebContents* web_contents)
    : content::WebContentsObserver(web_contents),
      content::WebContentsUserData<JatterTabHelper>(*web_contents) {
  if (web_contents->GetVisibility() == content::Visibility::VISIBLE) {
    last_active_time_ = base::TimeTicks::Now();
  }
}

JatterTabHelper::~JatterTabHelper() = default;

void JatterTabHelper::DidFinishNavigation(content::NavigationHandle* navigation_handle) {
  // 1. Strictly filter for actual cross-document main-frame navigations
  if (!navigation_handle->IsInPrimaryMainFrame() || 
      !navigation_handle->HasCommitted() || 
      navigation_handle->IsSameDocument() ||
      navigation_handle->IsErrorPage() ||
      !navigation_handle->GetURL().SchemeIsHTTPOrHTTPS()) {
    return;
  }

  // 2. Final flush for the document the user is permanently leaving
  FlushToNetwork(/*is_final_flush=*/true);

  // 3. Reset the stopwatch for the new page
  accumulated_duration_ = base::TimeDelta();
  
  // 4. Capture the exact ID that HistoryService will use
  current_nav_entry_id_ = navigation_handle->GetNavigationId();

  // 5. Resume timing if the tab is actively on screen
  if (web_contents()->GetVisibility() == content::Visibility::VISIBLE) {
    last_active_time_ = base::TimeTicks::Now();
  } else {
    last_active_time_ = base::TimeTicks();
  }
}

void JatterTabHelper::OnVisibilityChanged(content::Visibility visibility) {
  if (visibility == content::Visibility::HIDDEN) {
    // Pause the stopwatch and accumulate the active time
    if (!last_active_time_.is_null()) {
      accumulated_duration_ += base::TimeTicks::Now() - last_active_time_;
      last_active_time_ = base::TimeTicks(); 
    }
    
    // Start the 2-minute debounce timer to prevent network spam during rapid Alt-Tabbing
    debounce_timer_.Start(
        FROM_HERE, base::Minutes(2), 
        base::BindOnce(&JatterTabHelper::FlushToNetwork, base::Unretained(this), /*is_final_flush=*/false));

  } else if (visibility == content::Visibility::VISIBLE) {
    // Resume the stopwatch and cancel any pending background flushes
    last_active_time_ = base::TimeTicks::Now();
    debounce_timer_.Stop();
  }
}

void JatterTabHelper::WebContentsDestroyed() {
  // Catch any final active time before the tab window ceases to exist
  if (!last_active_time_.is_null()) {
    accumulated_duration_ += base::TimeTicks::Now() - last_active_time_;
    last_active_time_ = base::TimeTicks();
  }
  
  debounce_timer_.Stop();
  FlushToNetwork(/*is_final_flush=*/true);
}

void JatterTabHelper::FlushToNetwork(bool is_final_flush) {
  if (current_nav_entry_id_ == 0 || accumulated_duration_.is_zero()) {
    return;
  }

  Profile* profile = Profile::FromBrowserContext(web_contents()->GetBrowserContext());
  if (!profile || profile->IsOffTheRecord()) {
    return; // Audit Check C13: Never upload from Incognito/Guest profiles
  }

  JatterFirebaseClient* client = JatterFirebaseClientFactory::GetForProfile(profile);
  if (client) {
    client->UpdateActiveDuration(current_nav_entry_id_, 
                                 accumulated_duration_.InMicroseconds(), 
                                 is_final_flush);
  }
}

WEB_CONTENTS_USER_DATA_KEY_IMPL(JatterTabHelper);
