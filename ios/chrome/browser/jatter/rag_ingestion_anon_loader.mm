// ios/chrome/browser/jatter/rag_ingestion_anon_loader.mm
#import "ios/chrome/browser/jatter/rag_ingestion_anon_loader.h"

#import "base/strings/string_util.h"
#import "base/time/time.h"
#import "ios/chrome/browser/shared/model/profile/profile_ios.h"
#import "ios/chrome/browser/jatter/rag_ingestion_java_script_feature.h"
#import "ios/chrome/browser/shared/model/profile/profile_ios.h"
#import "ios/web/public/js_messaging/web_frame.h"
#import "ios/web/public/js_messaging/web_frames_manager.h"
#import "ios/web/public/navigation/navigation_context.h"
#import "ios/web/public/navigation/navigation_manager.h"
#import "ios/web/public/web_state.h"
#import "net/http/http_response_headers.h"

AnonPageLoader::AnonPageLoader(ProfileIOS* profile, const GURL& url, OnLoadedCallback callback)
    : profile_(profile), url_(url), callback_(std::move(callback)) {}

AnonPageLoader::~AnonPageLoader() {
  if (headless_web_state_) {
    headless_web_state_->RemoveObserver(this);
  }
}

void AnonPageLoader::Start() {
  // 1. Get or create the Off-The-Record (Incognito) profile for cookie-less anonymity
  ProfileIOS* otr_profile = profile_->GetOffTheRecordProfile();

  // 2. Create the headless WebState
  web::WebState::CreateParams create_params(otr_profile);
  headless_web_state_ = web::WebState::Create(create_params);
  
  // Note: On iOS, WKWebView is lazy. Calling GetView() forces it to initialize
  // the underlying WKWebView so it can process JS and build the DOM off-screen.
  std::ignore = headless_web_state_->GetView();

  headless_web_state_->AddObserver(this);

  // 3. Trigger the navigation
  web::NavigationManager::WebLoadParams load_params(url_);
  headless_web_state_->GetNavigationManager()->LoadURLWithParams(load_params);
}

void AnonPageLoader::DidFinishNavigation(web::WebState* web_state,
                                         web::NavigationContext* navigation_context) {
  if (!navigation_context->HasCommitted() || navigation_context->IsSameDocument()) {
    return;
  }

  last_committed_url_ = navigation_context->GetUrl();

  if (navigation_context->GetResponseHeaders()) {
    last_http_status_code_ = navigation_context->GetResponseHeaders()->response_code();
  } else {
    last_http_status_code_ = 0;
  }
}

void AnonPageLoader::PageLoaded(web::WebState* web_state,
                                web::PageLoadCompletionStatus load_completion_status) {
  if (callback_run_) return;

  // The hidden tab has built the DOM. Give it 1.5 seconds to execute any 
  // client-side rendering (React/Vue) before we scrape it.
  extraction_timer_.Start(
      FROM_HERE, base::Milliseconds(1500),
      base::BindOnce(&AnonPageLoader::ExecuteExtraction, base::Unretained(this), 1));
}

void AnonPageLoader::WebStateDestroyed(web::WebState* web_state) {
  headless_web_state_->RemoveObserver(this);
  
  if (!callback_run_ && callback_) {
    callback_run_ = true;
    std::move(callback_).Run({"", last_http_status_code_, last_committed_url_});
  }
}

void AnonPageLoader::ExecuteExtraction(int attempt) {
  if (callback_run_ || !headless_web_state_) return;

  // Fetch the main frame strictly from the isolated world where our script lives
  web::WebFramesManager* frames_manager = RagIngestionJavaScriptFeature::GetInstance()
      ->GetWebFramesManager(headless_web_state_.get());
  
  web::WebFrame* main_frame = frames_manager->GetMainWebFrame();

  if (!main_frame) {
    if (!callback_run_ && callback_) {
      callback_run_ = true;
      std::move(callback_).Run({"", last_http_status_code_, last_committed_url_});
    }
    return;
  }

  base::ListValue parameters;
  
  // Call the exact same JS API we proved out in Phase 1!
  main_frame->CallJavaScriptFunction(
      "ragIngestion.extractContent",
      parameters,
      base::BindOnce(
          [](base::WeakPtr<AnonPageLoader> loader, int attempt, const base::Value* result) {
            if (!loader) return;
            
            std::string text = "";
            if (result && result->is_dict()) {
              if (const std::string* extracted_text = result->GetDict().FindString("text")) {
                text = *extracted_text;
                text = base::CollapseWhitespaceASCII(text, false);
              }
            }

            // Polling Logic: Retry up to 6 times if text is too small
            if (text.length() < 500 && attempt < 6) {
              loader->extraction_timer_.Start(
                  FROM_HERE, base::Milliseconds(500),
                  base::BindOnce(&AnonPageLoader::ExecuteExtraction, loader, attempt + 1));
              return;
            }

            // Finished or maxed out attempts. Fire callback!
            loader->callback_run_ = true;
            AnonPageResult final_result{text, loader->last_http_status_code_, loader->last_committed_url_};
            
            if (loader->callback_) {
              std::move(loader->callback_).Run(final_result);
            }
          },
          weak_factory_.GetWeakPtr(), attempt),
      base::Seconds(5));
}