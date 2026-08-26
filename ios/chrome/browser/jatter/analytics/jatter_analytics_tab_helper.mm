#include "ios/chrome/browser/jatter/analytics/jatter_analytics_tab_helper.h"

#include "ios/chrome/browser/jatter/analytics/jatter_analytics_service.h"
#include "ios/chrome/browser/jatter/analytics/jatter_analytics_service_factory.h"
#include "ios/chrome/browser/shared/model/profile/profile_ios.h"
#include "ios/web/public/navigation/navigation_context.h"

JatterAnalyticsTabHelper::JatterAnalyticsTabHelper(web::WebState* web_state)
    : web_state_(web_state) {
  web_state_->AddObserver(this);
}

JatterAnalyticsTabHelper::~JatterAnalyticsTabHelper() {
  if (web_state_) {
    web_state_->RemoveObserver(this);
    web_state_ = nullptr;
  }
}

void JatterAnalyticsTabHelper::DidFinishNavigation(
    web::WebState* web_state,
    web::NavigationContext* navigation_context) {
  // Only track successful, main-frame navigations (ignore anchor link clicks)
  if (navigation_context->HasCommitted() && !navigation_context->IsSameDocument()) {
    ProfileIOS* profile = ProfileIOS::FromBrowserState(web_state->GetBrowserState());
    if (JatterAnalyticsService* service = JatterAnalyticsServiceFactory::GetForProfile(profile)) {
      service->RecordNavigationEvent();
    }
  }
}

void JatterAnalyticsTabHelper::WebStateDestroyed(web::WebState* web_state) {
  web_state_->RemoveObserver(this);
  web_state_ = nullptr;
}