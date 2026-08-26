#ifndef IOS_CHROME_BROWSER_JATTER_JATTER_AUTHORIZATION_HANDLER_H_
#define IOS_CHROME_BROWSER_JATTER_JATTER_AUTHORIZATION_HANDLER_H_

#include <memory>
#include <string>

#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "base/values.h"
#include "ios/web/public/web_state_observer.h"
#include "ios/web/public/web_state_user_data.h"
#include "services/network/public/cpp/simple_url_loader.h"

class ProfileIOS;

class JatterAuthorizationHandler 
    : public web::WebStateObserver,
      public web::WebStateUserData<JatterAuthorizationHandler> {
 public:
  ~JatterAuthorizationHandler() override;

  // web::WebStateObserver:
  void WebStateDestroyed(web::WebState* web_state) override;

  // TODO: In modern iOS Chromium, you must create a `web::JavaScriptFeature` 
  // subclass to intercept JS messages, and have it call this method.
  void OnJsMessageReceived(const base::Value& message,
                           const GURL& page_url,
                           bool has_user_gesture,
                           bool is_main_frame);

 private:
  friend class web::WebStateUserData<JatterAuthorizationHandler>;

  JatterAuthorizationHandler(web::WebState* web_state, ProfileIOS* profile);

  void SendCustomTokenRequest(const std::string& custom_token,
                              const std::string& api_key);
                              
  void OnDownloadComplete(std::unique_ptr<network::SimpleURLLoader> loader,
                          std::optional<std::string> response_body);

  // M147 requires raw_ptr for all class-level pointers
  raw_ptr<web::WebState> web_state_ = nullptr;
  raw_ptr<ProfileIOS> profile_ = nullptr;
  base::WeakPtrFactory<JatterAuthorizationHandler> weak_ptr_factory_{this};
  
  // Manually implementing the UserData key to bypass macro expansion errors
  static const int kUserDataKey;
};

#endif  // IOS_CHROME_BROWSER_JATTER_JATTER_AUTHORIZATION_HANDLER_H_