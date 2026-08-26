#ifndef IOS_CHROME_BROWSER_RAG_INGESTION_RAG_INGESTION_JAVA_SCRIPT_FEATURE_H_
#define IOS_CHROME_BROWSER_RAG_INGESTION_RAG_INGESTION_JAVA_SCRIPT_FEATURE_H_

#include "base/functional/callback.h"
#include "base/no_destructor.h"
#include "base/values.h"
#include "ios/web/public/js_messaging/java_script_feature.h"

namespace web {
class WebState;
}  // namespace web

class RagIngestionJavaScriptFeature : public web::JavaScriptFeature {
 public:
  static RagIngestionJavaScriptFeature* GetInstance();

  // Invokes `ragIngestion.extractContent()` in the main frame and returns
  // the resulting dictionary asynchronously to `callback`.
  void ExtractTextAndLinks(web::WebState* web_state,
                           base::OnceCallback<void(const base::Value*)> callback);

 private:
  friend class base::NoDestructor<RagIngestionJavaScriptFeature>;
  RagIngestionJavaScriptFeature();
  ~RagIngestionJavaScriptFeature() override;
};

#endif  // IOS_CHROME_BROWSER_RAG_INGESTION_RAG_INGESTION_JAVA_SCRIPT_FEATURE_H_