#ifndef IOS_CHROME_BROWSER_JATTER_JATTER_JAVA_SCRIPT_FEATURE_H_
#define IOS_CHROME_BROWSER_JATTER_JATTER_JAVA_SCRIPT_FEATURE_H_

#include <optional>
#include <string>

#include "base/no_destructor.h"
#include "ios/web/public/js_messaging/java_script_feature.h"

class JatterJavaScriptFeature : public web::JavaScriptFeature {
 public:
  static JatterJavaScriptFeature* GetInstance();

 private:
  friend class base::NoDestructor<JatterJavaScriptFeature>;

  JatterJavaScriptFeature();
  ~JatterJavaScriptFeature() override;

  // web::JavaScriptFeature:
  std::optional<std::string> GetScriptMessageHandlerName() const override;
  void ScriptMessageReceived(web::WebState* web_state,
                             const web::ScriptMessage& message) override;
};

#endif  // IOS_CHROME_BROWSER_JATTER_JATTER_JAVA_SCRIPT_FEATURE_H_