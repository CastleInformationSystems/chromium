// Copyright 2024 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/android/jni_string.h"
#include "base/android/scoped_java_ref.h" // <-- NEW: Include standard JavaRef
#include "base/json/json_reader.h"
#include "base/time/time.h"
#include "chrome/browser/jatter/analytics/jatter_analytics_service.h"
#include "chrome/browser/jatter/analytics/jatter_analytics_service_factory.h"
#include "chrome/browser/profiles/profile.h"

// Generated header from generate_jni target
#include "chrome/browser/jatter/analytics/android/jni_headers/JatterAnalyticsServiceBridge_jni.h"

// Use the modern JavaRef instead of the purged JavaParamRef
using base::android::JavaRef;

static void JNI_JatterAnalyticsServiceBridge_RecordDefaultBrowserSet(
    JNIEnv* env,
    const JavaRef<jobject>& j_profile) {
  Profile* profile = Profile::FromJavaObject(j_profile);
  if (!profile) return;

  if (auto* analytics = JatterAnalyticsServiceFactory::GetForProfile(profile)) {
    analytics->RecordDefaultBrowserSet();
  }
}

static void JNI_JatterAnalyticsServiceBridge_RecordSessionEnded(
    JNIEnv* env,
    const JavaRef<jobject>& j_profile,
    jlong session_length_millis) {
  Profile* profile = Profile::FromJavaObject(j_profile);
  if (!profile) return;

  if (auto* analytics = JatterAnalyticsServiceFactory::GetForProfile(profile)) {
    analytics->RecordSessionEnded(base::Milliseconds(session_length_millis));
  }
}

static void JNI_JatterAnalyticsServiceBridge_RecordCustomEvent(
    JNIEnv* env,
    const JavaRef<jobject>& j_profile,
    const JavaRef<jstring>& j_event_name,
    const JavaRef<jstring>& j_json_params) {
  Profile* profile = Profile::FromJavaObject(j_profile);
  if (!profile) return;

  std::string event_name = base::android::ConvertJavaStringToUTF8(env, j_event_name);
  std::string json_params_str = base::android::ConvertJavaStringToUTF8(env, j_json_params);

  base::DictValue params_dict;
  
  // Passed base::JSON_PARSE_RFC to satisfy the strict JSONReader API
  auto parsed_json = base::JSONReader::Read(json_params_str, base::JSON_PARSE_RFC);
  
  if (parsed_json && parsed_json->is_dict()) {
    // Note: If this line throws a compile error next, it means your branch 
    // also purged base::DictValue in favor of base::Value::Dict.
    params_dict = std::move(parsed_json->GetDict());
  }

  if (auto* analytics = JatterAnalyticsServiceFactory::GetForProfile(profile)) {
    analytics->RecordCustomEvent(event_name, std::move(params_dict));
  }
}

DEFINE_JNI(JatterAnalyticsServiceBridge)