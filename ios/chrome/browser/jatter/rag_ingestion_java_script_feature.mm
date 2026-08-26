#import "ios/chrome/browser/jatter/rag_ingestion_java_script_feature.h"

#import "base/values.h"
#import "ios/web/public/js_messaging/web_frame.h"
#import "ios/web/public/js_messaging/web_frames_manager.h"
#import "ios/web/public/web_state.h"

// static
RagIngestionJavaScriptFeature* RagIngestionJavaScriptFeature::GetInstance() {
  static base::NoDestructor<RagIngestionJavaScriptFeature> instance;
  return instance.get();
}

RagIngestionJavaScriptFeature::RagIngestionJavaScriptFeature()
    : web::JavaScriptFeature(
          // Use kIsolatedWorld to protect our DOM scraping from website interference
          web::ContentWorld::kIsolatedWorld,
          {web::JavaScriptFeature::FeatureScript::CreateWithFilename(
              "rag_ingestion",
              web::JavaScriptFeature::FeatureScript::InjectionTime::kDocumentStart,
              // Only scrape the main frame for Phase 1 (matches IsInPrimaryMainFrame)
              web::JavaScriptFeature::FeatureScript::TargetFrames::kMainFrame)}) {}

RagIngestionJavaScriptFeature::~RagIngestionJavaScriptFeature() = default;

void RagIngestionJavaScriptFeature::ExtractTextAndLinks(
    web::WebState* web_state,
    base::OnceCallback<void(const base::Value*)> callback) {
  
  if (!web_state) {
    std::move(callback).Run(nullptr);
    return;
  }

  web::WebFramesManager* frames_manager = GetWebFramesManager(web_state);
  web::WebFrame* main_frame = frames_manager->GetMainWebFrame();
  
  if (!main_frame) {
    std::move(callback).Run(nullptr);
    return;
  }

  // Safely invoke ragIngestion.extractContent() inside WebKit
  CallJavaScriptFunction(
      main_frame,
      "ragIngestion.extractContent",
      /*parameters=*/{},
      std::move(callback),
      /*timeout=*/base::Seconds(3));
}