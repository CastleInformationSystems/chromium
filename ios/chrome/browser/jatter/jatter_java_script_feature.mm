#include "ios/chrome/browser/jatter/jatter_java_script_feature.h"

#import "base/no_destructor.h"
#import "base/values.h"
#import "ios/chrome/browser/jatter/jatter_authorization_handler.h"
#import "ios/web/public/js_messaging/script_message.h"

// static
JatterJavaScriptFeature* JatterJavaScriptFeature::GetInstance() {
  static base::NoDestructor<JatterJavaScriptFeature> instance;
  return instance.get();
}

JatterJavaScriptFeature::JatterJavaScriptFeature()
    : web::JavaScriptFeature(
          web::ContentWorld::kPageContentWorld,
          {web::JavaScriptFeature::FeatureScript::CreateWithFilename(
              "jatter_bridge",
              web::JavaScriptFeature::FeatureScript::InjectionTime::kDocumentStart,
              web::JavaScriptFeature::FeatureScript::TargetFrames::kAllFrames)}) {}

JatterJavaScriptFeature::~JatterJavaScriptFeature() = default;

std::optional<std::string> JatterJavaScriptFeature::GetScriptMessageHandlerName() const {
  return "jatterAuth";
}

void JatterJavaScriptFeature::ScriptMessageReceived(
    web::WebState* web_state,
    const web::ScriptMessage& message) {
  
  JatterAuthorizationHandler* handler = 
      JatterAuthorizationHandler::FromWebState(web_state);
      
  if (handler && message.body() && message.body()->is_dict()) {
    GURL page_url = message.request_url().has_value() ? *message.request_url() : GURL();

    handler->OnJsMessageReceived(*message.body(), page_url,
                                 message.is_user_interacting(),
                                 message.is_main_frame());
  }
}