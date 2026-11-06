package org.chromium.chrome.browser.firstrun;

import org.jni_zero.NativeMethods;
import org.chromium.chrome.browser.profiles.Profile;

/**
 * JNI bridge connecting the Jatter Android FRE to the C++ PrefService.
 */
public class JatterFreJni {
    
    public static void setAlwaysAllowPersonalization(Profile profile, boolean allow) {
        JatterFreJniJni.get().setAlwaysAllowPersonalization(profile, allow);
    }

    public static void setShowPlusUpgradeOnStartup(Profile profile, boolean show) {
        JatterFreJniJni.get().setShowPlusUpgradeOnStartup(profile, show);
    }

    public static boolean getAndClearShowPlusUpgradeOnStartup(Profile profile) {
        return JatterFreJniJni.get().getAndClearShowPlusUpgradeOnStartup(profile);
    }

    @NativeMethods
    public interface Natives {
        boolean getAlwaysAllowPersonalization(Profile profile);
        void setAlwaysAllowPersonalization(Profile profile, boolean allow);
        void setShowPlusUpgradeOnStartup(Profile profile, boolean show);
        boolean getAndClearShowPlusUpgradeOnStartup(Profile profile);
        String getAppUrl();
    }
}
