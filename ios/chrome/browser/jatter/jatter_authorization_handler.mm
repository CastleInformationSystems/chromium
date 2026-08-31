#include "ios/chrome/browser/jatter/jatter_authorization_handler.h"

#import <Foundation/Foundation.h>

#include "base/functional/bind.h"
#include "base/json/json_reader.h"
#include "base/json/json_writer.h"
#include "base/logging.h"
#include "ios/chrome/browser/jatter/jatter_environment.h"
#include "ios/chrome/browser/jatter/jatter_token_storage.h"
#include "ios/chrome/browser/jatter/rag_ingestion_service.h"
#include "ios/chrome/browser/jatter/rag_ingestion_service_factory.h"
#include "ios/chrome/browser/shared/model/profile/profile_ios.h"
#include "ios/web/public/web_state.h"
#include "services/network/public/cpp/resource_request.h"
#include "services/network/public/cpp/shared_url_loader_factory.h"

// Manually initialize the WebStateUserData key
const int JatterAuthorizationHandler::kUserDataKey = 0;

JatterAuthorizationHandler::JatterAuthorizationHandler(web::WebState* web_state,
                                                       ProfileIOS* profile)
    : web_state_(web_state), profile_(profile) {
#ifndef JATTER_PRODUCTION_MODE
  NSLog(@"[JatterAuth] HANDLER CREATED! If you see this, the C++ is attached to the tab.");
#endif
  web_state_->AddObserver(this);
}

JatterAuthorizationHandler::~JatterAuthorizationHandler() {
  if (web_state_) {
    web_state_->RemoveObserver(this);
  }
}

void JatterAuthorizationHandler::WebStateDestroyed(web::WebState* web_state) {
  web_state_->RemoveObserver(this);
  web_state_ = nullptr;
}

void JatterAuthorizationHandler::OnJsMessageReceived(
    const base::Value& message,
    const GURL& page_url,
    bool has_user_gesture,
    bool is_main_frame) {
#ifndef JATTER_PRODUCTION_MODE  
  LOG(INFO) << "[JatterAuth] JS Message intercepted from URL: " << page_url.spec();
  NSLog(@"[JatterAuth] JS MESSAGE RECEIVED!");
#endif
  if (!message.is_dict()) {
#ifndef JATTER_PRODUCTION_MODE
    LOG(WARNING) << "[JatterAuth] Aborting: JS message is not a dictionary.";
#endif
    return;
  }

  const std::string* command = message.GetDict().FindString("command");
  if (!command) {
#ifndef JATTER_PRODUCTION_MODE
    LOG(WARNING) << "[JatterAuth] Aborting: JS message missing 'command' key.";
#endif
    return;
  }
#ifndef JATTER_PRODUCTION_MODE
  LOG(INFO) << "[JatterAuth] Processing command: " << *command;
#endif
  if (*command == "jatterAuth.sendToken") {
    const std::string* token = message.GetDict().FindString("token");
    if (token) {
#ifndef JATTER_PRODUCTION_MODE
      NSLog(@"[JatterAuth] token - %s", token->c_str());
      LOG(INFO) << "[JatterAuth] SUCCESS: Extracted token from JS payload: " << *token;
#endif
      SendCustomTokenRequest(*token, jatter::kFirebaseApiKey);
    } 
#ifndef JATTER_PRODUCTION_MODE
    else {
      LOG(ERROR) << "[JatterAuth] FAILED: 'sendToken' command received, but 'token' key is missing!";
    }
#endif
  }
  else if (*command == "jatterAuth.sendPrivateKey") {
    const std::string* pk = message.GetDict().FindString("privateKey");
    if (pk) {
#ifndef JATTER_PRODUCTION_MODE
      NSLog(@"[JatterAuth] Received private key.");
#endif
      if (web_state_) {
        ProfileIOS* profile = ProfileIOS::FromBrowserState(web_state_->GetBrowserState());
        RagIngestionService* service = RagIngestionServiceFactory::GetForProfile(profile);
        
        if (service) {
          service->SetPrivateKey(*pk);
#ifndef JATTER_PRODUCTION_MODE
          LOG(INFO) << "[JatterAuth] Successfully routed private key to RagIngestionService.";
#endif
        }
#ifndef JATTER_PRODUCTION_MODE 
        else {
          LOG(WARNING) << "[JatterAuth] Ignored SendPrivateKey: RagIngestionService is null (Incognito).";
        }
#endif
      }
    }
  }
#ifndef JATTER_PRODUCTION_MODE 
  else {
    LOG(WARNING) << "[JatterAuth] Unknown command received: " << *command;
  }
#endif
}

void JatterAuthorizationHandler::SendCustomTokenRequest(
    const std::string& custom_token,
    const std::string& api_key) {
#ifndef JATTER_PRODUCTION_MODE  
  LOG(INFO) << "[JatterAuth] Preparing outbound Firebase request...";
#endif
  GURL url("https://identitytoolkit.googleapis.com/v1/accounts:signInWithCustomToken?key=" + api_key);

  auto resource_request = std::make_unique<network::ResourceRequest>();
  resource_request->url = url;
  resource_request->method = "POST";
  resource_request->headers.SetHeader("Content-Type", "application/json");

  base::DictValue body;
  body.Set("token", custom_token);
  body.Set("returnSecureToken", true);

  std::string json_body;
  base::JSONWriter::Write(body, &json_body);

  auto loader = network::SimpleURLLoader::Create(std::move(resource_request),
                                                 NO_TRAFFIC_ANNOTATION_YET);
  loader->AttachStringForUpload(json_body, "application/json");

  auto url_loader_factory = profile_->GetSharedURLLoaderFactory();

  auto* loader_ptr = loader.get();
#ifndef JATTER_PRODUCTION_MODE  
  LOG(INFO) << "[JatterAuth] Dispatching network request to Firebase Identity Toolkit.";
#endif
  loader_ptr->DownloadToString(
      url_loader_factory.get(),
      base::BindOnce(&JatterAuthorizationHandler::OnDownloadComplete,
                     weak_ptr_factory_.GetWeakPtr(), std::move(loader)),
      1024 * 1024);
}

void JatterAuthorizationHandler::OnDownloadComplete(
    std::unique_ptr<network::SimpleURLLoader> loader,
    std::optional<std::string> response_body) {
  
  if (!response_body) {
#ifndef JATTER_PRODUCTION_MODE
    NSLog(@"[JatterAuth] NETWORK ERROR: Firebase request failed. Net error code: %d", 
          loader->NetError());
#endif
    return;
  }
#ifndef JATTER_PRODUCTION_MODE
  NSLog(@"[JatterAuth] SUCCESS: Firebase response received: %s", response_body->c_str());
#endif
  std::optional<base::Value> value = 
      base::JSONReader::Read(*response_body, base::JSON_PARSE_CHROMIUM_EXTENSIONS);

  if (value && value->is_dict()) {
    const std::string* id_token = value->GetDict().FindString("idToken");
    const std::string* refresh_token = value->GetDict().FindString("refreshToken");
    
    if (id_token && refresh_token) {
#ifndef JATTER_PRODUCTION_MODE
      NSLog(@"[JatterAuth] Parsed tokens successfully! Saving to JatterTokenStorage.");
      NSLog(@"[JatterAuth] Extracted idToken: %s", id_token->c_str());
#endif      
      JatterTokenStorage* storage = JatterTokenStorage::GetOrCreate(profile_);
      storage->SetTokens(*id_token, *refresh_token);
#ifndef JATTER_PRODUCTION_MODE      
      NSLog(@"[JatterAuth] Tokens successfully pushed to storage!");
#endif
    }
#ifndef JATTER_PRODUCTION_MODE    
    else {
      NSLog(@"[JatterAuth] FAILED: Firebase JSON response missing 'idToken' or 'refreshToken'.");
    }
#endif
  }
#ifndef JATTER_PRODUCTION_MODE 
  else {
    NSLog(@"[JatterAuth] FAILED: Could not parse Firebase response as JSON dictionary.");
  }
#endif
}