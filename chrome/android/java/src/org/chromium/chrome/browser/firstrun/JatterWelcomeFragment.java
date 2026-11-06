package org.chromium.chrome.browser.firstrun;

import android.app.Activity;
import android.app.role.RoleManager;
import android.content.Context;
import android.content.Intent;
import android.os.Build;
import android.os.Bundle;
import android.view.LayoutInflater;
import android.view.View;
import android.view.ViewGroup;
import android.widget.Button;

import androidx.annotation.NonNull;
import androidx.annotation.Nullable;
import androidx.fragment.app.Fragment;

import org.chromium.base.Callback;
import org.chromium.chrome.R;

public class JatterWelcomeFragment extends Fragment implements FirstRunFragment {

    private static final int DEFAULT_BROWSER_REQUEST_CODE = 147;
    private Button mDefaultButton;
    private Button mSkipButton;

    @Override
    public View onCreateView(
            LayoutInflater inflater, ViewGroup container, Bundle savedInstanceState) {
        return inflater.inflate(R.layout.jatter_welcome_fragment, container, false);
    }

    @Override
    public void onViewCreated(@NonNull View view, @Nullable Bundle savedInstanceState) {
        super.onViewCreated(view, savedInstanceState);

        mDefaultButton = view.findViewById(R.id.welcome_default_button);
        mSkipButton = view.findViewById(R.id.welcome_skip_button);

        // Lock UI until C++ engine is ready for Step 2
        setButtonsEnabled(false);
        getPageDelegate().getNativeInitializationPromise().then(
                (Callback<Void>) v -> setButtonsEnabled(true));

        mDefaultButton.setOnClickListener(v -> requestDefaultBrowserRole());
        mSkipButton.setOnClickListener(v -> getPageDelegate().advanceToNextPage());
    }

    private void setButtonsEnabled(boolean enabled) {
        mDefaultButton.setEnabled(enabled);
        mSkipButton.setEnabled(enabled);
    }

    private void requestDefaultBrowserRole() {
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.Q) {
            RoleManager roleManager = (RoleManager) getContext().getSystemService(Context.ROLE_SERVICE);
            if (roleManager != null && roleManager.isRoleAvailable(RoleManager.ROLE_BROWSER)) {
                if (roleManager.isRoleHeld(RoleManager.ROLE_BROWSER)) {
                    // Already the default browser, proceed directly
                    getPageDelegate().advanceToNextPage();
                } else {
                    // Trigger the OS bottom sheet prompt
                    Intent intent = roleManager.createRequestRoleIntent(RoleManager.ROLE_BROWSER);
                    startActivityForResult(intent, DEFAULT_BROWSER_REQUEST_CODE);
                }
                return;
            }
        }
        // Fallback for older Android versions where RoleManager isn't supported
        getPageDelegate().advanceToNextPage();
    }

    @Override
    public void onActivityResult(int requestCode, int resultCode, Intent data) {
        super.onActivityResult(requestCode, resultCode, data);
        if (requestCode == DEFAULT_BROWSER_REQUEST_CODE) {
            // Regardless of whether they accepted or denied the OS prompt, we move to Step 2
            getPageDelegate().advanceToNextPage();
        }
    }
    
    @Override
    public void setInitialA11yFocus() {
        // No-op for now, satisfies the FirstRunFragment interface
    }
}
