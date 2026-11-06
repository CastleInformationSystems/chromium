#include "base/android/jni_string.h"
#include "chrome/android/chrome_jni_headers/JatterFreJni_jni.h"
#include "chrome/browser/jatter/jatter_environment.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/common/pref_names.h"
#include "components/prefs/pref_service.h"
#include "third_party/jni_zero/jni_zero.h"

static base::android::ScopedJavaLocalRef<jstring> JNI_JatterFreJni_GetAppUrl(JNIEnv* env) {
  return base::android::ConvertUTF8ToJavaString(env, jatter::kAppUrl);
}

jboolean JNI_JatterFreJni_GetAndClearShowPlusUpgradeOnStartup(
    JNIEnv* env,
    const base::android::JavaRef<jobject>&  j_profile) {
  Profile* profile = Profile::FromJavaObject(j_profile);
  if (!profile || !profile->GetPrefs()) {
    return false;
  }

  bool should_show = profile->GetPrefs()->GetBoolean(prefs::kJatterShowPlusUpgradeOnStartup);
  if (should_show) {
    profile->GetPrefs()->SetBoolean(prefs::kJatterShowPlusUpgradeOnStartup, false);
  }
  
  return should_show;
}

void JNI_JatterFreJni_SetAlwaysAllowPersonalization(
    JNIEnv* env,
    const base::android::JavaRef<jobject>&  j_profile,
    jboolean allow) {
  Profile* profile = Profile::FromJavaObject(j_profile);
  if (profile && profile->GetPrefs()) {
    profile->GetPrefs()->SetBoolean(prefs::kJatterAlwaysAllowPersonalization, allow);
  }
}

void JNI_JatterFreJni_SetShowPlusUpgradeOnStartup(
    JNIEnv* env,
    const base::android::JavaRef<jobject>&  j_profile,
    jboolean show) {
  Profile* profile = Profile::FromJavaObject(j_profile);
  if (profile && profile->GetPrefs()) {
    profile->GetPrefs()->SetBoolean(prefs::kJatterShowPlusUpgradeOnStartup, show);
  }
}

jboolean JNI_JatterFreJni_GetAlwaysAllowPersonalization(
    JNIEnv* env,
    const base::android::JavaRef<jobject>& j_profile) {
  Profile* profile = Profile::FromJavaObject(j_profile);
  if (profile && profile->GetPrefs()) {
    return profile->GetPrefs()->GetBoolean(prefs::kJatterAlwaysAllowPersonalization);
  }
  return false; // Default safe fallback
}

DEFINE_JNI(JatterFreJni)
