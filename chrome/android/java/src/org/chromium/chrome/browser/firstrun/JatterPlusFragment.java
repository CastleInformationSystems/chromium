package org.chromium.chrome.browser.firstrun;

import android.os.Bundle;
import android.view.LayoutInflater;
import android.view.View;
import android.view.ViewGroup;

import androidx.annotation.NonNull;
import androidx.annotation.Nullable;
import androidx.fragment.app.Fragment;

import org.chromium.chrome.R;
import org.chromium.chrome.browser.profiles.Profile;
import org.chromium.chrome.browser.profiles.ProfileManager;

public class JatterPlusFragment extends Fragment implements FirstRunFragment {

    @Override
    public View onCreateView(
            LayoutInflater inflater, ViewGroup container, Bundle savedInstanceState) {
        return inflater.inflate(R.layout.jatter_plus_fragment, container, false);
    }

    @Override
    public void onViewCreated(@NonNull View view, @Nullable Bundle savedInstanceState) {
        super.onViewCreated(view, savedInstanceState);

        view.findViewById(R.id.plus_upgrade_button).setOnClickListener(v -> {
            Profile profile = ProfileManager.getLastUsedRegularProfile();
            JatterFreJni.setShowPlusUpgradeOnStartup(profile, true);
            finishFre();
        });

        view.findViewById(R.id.plus_skip_button).setOnClickListener(v -> {
            Profile profile = ProfileManager.getLastUsedRegularProfile();
            JatterFreJni.setShowPlusUpgradeOnStartup(profile, false);
            finishFre();
        });
    }

    private void finishFre() {
        // Marks the TOS as accepted, satisfying Chromium's internal FirstRunStatus checks.
        // The boolean parameter controls metric reporting opt-in.
        getPageDelegate().acceptTermsOfService(true);
        
        // Because this is the last page, advancing automatically triggers completeFirstRunExperience()
        getPageDelegate().advanceToNextPage();
    }
    
    @Override
    public void setInitialA11yFocus() {
        // No-op for now, satisfies the FirstRunFragment interface
    }
}
