package org.chromium.chrome.browser.settings;

import android.os.Bundle;
import androidx.preference.PreferenceFragmentCompat;
import org.chromium.chrome.R;
import org.chromium.chrome.browser.firstrun.JatterFreJniJni;
import org.chromium.chrome.browser.profiles.Profile;
import org.chromium.chrome.browser.profiles.ProfileManager;
import org.chromium.components.browser_ui.settings.ChromeSwitchPreference;

public class JatterAiPreferenceFragment extends PreferenceFragmentCompat {

    @Override
    public void onCreatePreferences(Bundle savedInstanceState, String rootKey) {
        // Inflate the XML layout
        setPreferencesFromResource(R.xml.jatter_ai_preferences, rootKey);
        getActivity().setTitle("Jatter AI");

        Profile profile = ProfileManager.getLastUsedRegularProfile();
        ChromeSwitchPreference personalizationToggle = findPreference("jatter_always_allow_personalization");

        if (personalizationToggle != null && profile != null) {
            // Set initial state from C++
            boolean isEnabled = JatterFreJniJni.get().getAlwaysAllowPersonalization(profile);
            personalizationToggle.setChecked(isEnabled);

            // Listen for user toggles and send back to C++
            personalizationToggle.setOnPreferenceChangeListener((preference, newValue) -> {
                boolean isChecked = (boolean) newValue;
                JatterFreJniJni.get().setAlwaysAllowPersonalization(profile, isChecked);
                return true;
            });
        }
    }
}
