// Copyright 2024 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package org.chromium.chrome.browser.jatter.analytics;

import org.jni_zero.NativeMethods;

import org.chromium.chrome.browser.profiles.Profile;

/**
 * Bridge between Java and C++ for Jatter Analytics logging.
 */
public class JatterAnalyticsServiceBridge {

    public static void recordDefaultBrowserSet(Profile profile) {
        if (profile == null) return;
        JatterAnalyticsServiceBridgeJni.get().recordDefaultBrowserSet(profile);
    }

    public static void recordSessionEnded(Profile profile, long sessionLengthMillis) {
        if (profile == null) return;
        JatterAnalyticsServiceBridgeJni.get().recordSessionEnded(profile, sessionLengthMillis);
    }

    public static void recordCustomEvent(Profile profile, String eventName, String jsonParams) {
        if (profile == null || eventName == null) return;
        JatterAnalyticsServiceBridgeJni.get().recordCustomEvent(profile, eventName, jsonParams != null ? jsonParams : "{}");
    }

    @NativeMethods
    public interface Natives {
        void recordDefaultBrowserSet(Profile profile);
        void recordSessionEnded(Profile profile, long sessionLengthMillis);
        void recordCustomEvent(Profile profile, String eventName, String jsonParams);
    }
}