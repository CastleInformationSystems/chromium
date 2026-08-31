// Copyright 2026 Jatter
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_UI_ANDROID_RAG_INGESTION_RAG_INGESTION_CONTROLLER_ANDROID_H_
#define CHROME_BROWSER_UI_ANDROID_RAG_INGESTION_RAG_INGESTION_CONTROLLER_ANDROID_H_

#include "base/android/jni_android.h"
#include "base/android/scoped_java_ref.h"
#include "content/public/browser/web_contents_observer.h"
#include "content/public/browser/web_contents_user_data.h"
#include "url/gurl.h"

enum IngestionState {
  kHidden = 0,
  kOffer = 1,
  kActive = 2,
  kDisabled = 3,
};

class RagIngestionControllerAndroid
    : public content::WebContentsObserver,
      public content::WebContentsUserData<RagIngestionControllerAndroid> {
 public:
  ~RagIngestionControllerAndroid() override;

  // Called by TabHelper to trigger the Java UI. Retained so TabHelper doesn't
  // break if it passes the URL directly.
  void ShowOfferPrompt(const GURL& url);

  // --- STATE MANAGEMENT (Matches Desktop PageActionController) ---
  void ShowOfferState();
  void ShowActiveState();
  void ShowDisabledState();
  void Hide();

  // --- JNI CALLS FROM JAVA ---
  // Called by Java via JNI when the user taps a button in the Bottom Sheet
  void OnUserDecision(JNIEnv* env, bool allowed);

  // Called by Java (AdaptiveToolbarButtonController) to poll current state
  jint GetToolbarState(JNIEnv* env);

  // Called by Java when the adaptive toolbar button is clicked
  void OpenSettings(JNIEnv* env);

  // Called by Java when the user submits a question in the Management Bottom Sheet
  void OnQuerySubmitted(JNIEnv* env, const base::android::JavaRef<jstring>& query);

  // content::WebContentsObserver implementation:
  void PrimaryPageChanged(content::Page& page) override;

 private:
  friend class content::WebContentsUserData<RagIngestionControllerAndroid>;
  explicit RagIngestionControllerAndroid(content::WebContents* web_contents);

  // Lazily initializes the JNI bridge if it hasn't been created yet.
  void EnsureBridgeInitialized();

  // The URL we are currently asking about
  GURL current_url_;

  // Tracks the current state to pass back to the Adaptive Toolbar.
  // 0 = Hidden, 1 = Offer, 2 = Active, 3 = Disabled
  int current_state_ = 0;

  // A pointer to our Java counterpart (RagIngestionBridge.java)
  base::android::ScopedJavaGlobalRef<jobject> java_bridge_;

  WEB_CONTENTS_USER_DATA_KEY_DECL();
};

#endif  // CHROME_BROWSER_UI_ANDROID_RAG_INGESTION_RAG_INGESTION_CONTROLLER_ANDROID_H_