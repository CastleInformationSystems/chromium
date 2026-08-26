// Copyright 2026 Jatter
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/android/rag_ingestion/rag_ingestion_controller_android.h"

#include "base/android/jni_string.h"
#include "base/strings/escape.h"
#include "chrome/browser/jatter/jatter_environment.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/rag_ingestion/rag_ingestion_service.h"
#include "chrome/browser/rag_ingestion/rag_ingestion_service_factory.h"
#include "chrome/browser/rag_ingestion/rag_ingestion_tab_helper.h"
#include "content/public/browser/page.h"
#include "content/public/browser/page_navigator.h"
#include "content/public/common/referrer.h"
#include "content/public/browser/web_contents.h"

// Generated header
#include "chrome/browser/ui/android/rag_ingestion/jni_headers/RagIngestionBridge_jni.h"

RagIngestionControllerAndroid::RagIngestionControllerAndroid(
    content::WebContents* web_contents)
    : content::WebContentsObserver(web_contents),
      content::WebContentsUserData<RagIngestionControllerAndroid>(*web_contents),
      current_state_(0) {}

RagIngestionControllerAndroid::~RagIngestionControllerAndroid() {
  if (!java_bridge_.is_null()) {
    Java_RagIngestionBridge_destroy(base::android::AttachCurrentThread(),
                                    java_bridge_);
  }
}

void RagIngestionControllerAndroid::EnsureBridgeInitialized() {
  if (java_bridge_.is_null()) {
    JNIEnv* env = base::android::AttachCurrentThread();
    java_bridge_.Reset(Java_RagIngestionBridge_create(
        env, reinterpret_cast<intptr_t>(this)));
  }
}

void RagIngestionControllerAndroid::ShowOfferPrompt(const GURL& url) {
  current_url_ = url;
  ShowOfferState();
}

void RagIngestionControllerAndroid::ShowOfferState() {
  current_state_ = 1;  // kOffer
  EnsureBridgeInitialized();
  
  JNIEnv* env = base::android::AttachCurrentThread();
  Java_RagIngestionBridge_onStateChanged(env, java_bridge_, current_state_);
  Java_RagIngestionBridge_showBottomSheet(env, java_bridge_,
                                          web_contents()->GetJavaWebContents());
}

void RagIngestionControllerAndroid::ShowActiveState() {
  current_state_ = 2;  // kActive
  EnsureBridgeInitialized();
  
  JNIEnv* env = base::android::AttachCurrentThread();
  Java_RagIngestionBridge_onStateChanged(env, java_bridge_, current_state_);
}

void RagIngestionControllerAndroid::ShowDisabledState() {
  current_state_ = 3;  // kDisabled
  EnsureBridgeInitialized();
  
  JNIEnv* env = base::android::AttachCurrentThread();
  Java_RagIngestionBridge_onStateChanged(env, java_bridge_, current_state_);
}

void RagIngestionControllerAndroid::Hide() {
  current_state_ = 0;  // kHidden
  
  // No need to initialize the bridge just to hide it, only fire if it exists.
  if (!java_bridge_.is_null()) {
    JNIEnv* env = base::android::AttachCurrentThread();
    Java_RagIngestionBridge_onStateChanged(env, java_bridge_, current_state_);
  }
}

jint RagIngestionControllerAndroid::GetToolbarState(JNIEnv* env) {
  return current_state_;
}

void RagIngestionControllerAndroid::OpenSettings(JNIEnv* env) {
  content::WebContents* wc = web_contents();
  if (!wc || wc->IsBeingDestroyed()) {
    return;
  }

  const GURL& url = wc->GetLastCommittedURL();
  if (!url.is_valid() || !url.SchemeIsHTTPOrHTTPS() || url.host().empty()) {
    return;
  }

  // --- INTERCEPT STATE 1 (OFFER / UNDECIDED) ---
  // If the user dismissed/ignored the initial prompt and clicks the toolbar icon,
  // re-trigger the Allow / Don't Allow message banner instead of the management sheet.
  if (current_state_ == IngestionState::kOffer) {
    ShowOfferPrompt(url);
    return;
  }
  // ---------------------------------------------

  // Otherwise, we are in State 2 (Active) or State 3 (Disabled). Show Management Sheet.
  bool is_allowed = (current_state_ == IngestionState::kActive);
  std::string host = std::string(url.host());

  base::android::ScopedJavaLocalRef<jstring> j_domain =
      base::android::ConvertUTF8ToJavaString(env, host);

  Java_RagIngestionBridge_showManagementBottomSheet(
      env, java_bridge_, wc->GetJavaWebContents(), j_domain, is_allowed);
}

void RagIngestionControllerAndroid::OnQuerySubmitted(
    JNIEnv* env, 
    const base::android::JavaRef<jstring>& j_query) { 
  content::WebContents* wc = web_contents();
  if (!wc) return;

  std::string query = base::android::ConvertJavaStringToUTF8(env, j_query);
  if (query.empty()) return;

  // A. URL-encode the user input
  std::string escaped_query = base::EscapeQueryParamValue(query, true);

  // B. Truncate to 512 characters max
  if (escaped_query.length() > 512) {
    escaped_query = escaped_query.substr(0, 512);
  }

  // C. Construct the URL (Using same format as desktop)
  std::string url_str = jatter::kAppPromptUrl + escaped_query;

  // D. Open in a new foreground tab
  content::OpenURLParams params(
      GURL(url_str), content::Referrer(),
      WindowOpenDisposition::NEW_FOREGROUND_TAB,
      ui::PAGE_TRANSITION_GENERATED, false);
      
  wc->OpenURL(params, /*navigation_handle_callback=*/{});
}

void RagIngestionControllerAndroid::PrimaryPageChanged(content::Page& page) {
  // Whenever the user navigates to a new page, hide the toolbar icon
  // and reset the state until the TabHelper finishes heuristics.
  Hide();
}

void RagIngestionControllerAndroid::OnUserDecision(JNIEnv* env, bool allowed) {
  content::WebContents* wc = web_contents();
  if (!wc) return;

  Profile* profile = Profile::FromBrowserContext(wc->GetBrowserContext());
  RagIngestionService* service =
      RagIngestionServiceFactory::GetForProfile(profile);
  if (!service) return;

  // 1. Save the user's decision to preferences
  service->SetUserPermission(
      wc->GetLastCommittedURL(),
      allowed ? RagIngestionService::UserPermission::kGranted
              : RagIngestionService::UserPermission::kDenied);

  // 2. React to decision
  if (allowed) {
    ShowActiveState();  // Signals the adaptive button to appear
    if (auto* helper = RagIngestionTabHelper::FromWebContents(wc)) {
      helper->IngestCurrentPage();
    }
  } else {
    ShowDisabledState();  // Signals the adaptive button to stay visible, but greyed/off
  }
}

WEB_CONTENTS_USER_DATA_KEY_IMPL(RagIngestionControllerAndroid);

// =========================================================================
// Native JNI Export Bridges
// =========================================================================

jlong JNI_RagIngestionBridge_GetBridgeForWebContents(
    JNIEnv* env,
    const base::android::JavaRef<jobject>& web_contents) {
  content::WebContents* wc = content::WebContents::FromJavaWebContents(web_contents);
  if (!wc) return 0;

  // Retrieve the instance bound to this WebContents' lifecycle
  auto* controller = RagIngestionControllerAndroid::FromWebContents(wc);
  return reinterpret_cast<intptr_t>(controller);
}

DEFINE_JNI(RagIngestionBridge)
