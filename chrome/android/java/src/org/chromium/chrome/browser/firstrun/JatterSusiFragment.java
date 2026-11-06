package org.chromium.chrome.browser.firstrun;

import android.os.Bundle;
import android.os.Handler;
import android.os.Looper;
import android.view.LayoutInflater;
import android.view.View;
import android.view.ViewGroup;
import android.widget.FrameLayout;
import android.widget.LinearLayout;

import androidx.annotation.NonNull;
import androidx.annotation.Nullable;
import androidx.fragment.app.Fragment;

import org.chromium.chrome.R;
import org.chromium.chrome.browser.profiles.Profile;
import org.chromium.chrome.browser.profiles.ProfileManager;
import org.chromium.chrome.browser.content.WebContentsFactory;
import org.chromium.components.embedder_support.delegate.WebContentsDelegateAndroid;
import org.chromium.components.embedder_support.view.ContentView;
import org.chromium.components.thinwebview.ThinWebView;
import org.chromium.components.thinwebview.ThinWebViewConstraints;
import org.chromium.components.thinwebview.ThinWebViewFactory;
import org.chromium.content_public.browser.LoadUrlParams;
import org.chromium.content_public.browser.NavigationHandle;
import org.chromium.content_public.browser.WebContents;
import org.chromium.content_public.browser.WebContentsObserver;
import org.chromium.ui.base.ViewAndroidDelegate;
import org.chromium.ui.base.WindowAndroid;
import org.chromium.url.GURL;

/**
 * Step 2 of the Jatter FRE.
 * Shows a native prompt. If accepted, hosts a native Chromium WebContents 
 * to load the web-based authentication flow.
 */
public class JatterSusiFragment extends Fragment implements FirstRunFragment {

    private static final String JATTER_APP_HOST = "beacon-staging-df5f2.firebaseapp.com";
    
    private LinearLayout mPromptLayout;
    private FrameLayout mWebContainer;
    
    private ContentView mContentView;
    private ThinWebView mThinWebView;
    private WebContents mWebContents;
    private WebContentsObserver mWebContentsObserver;

    @Override
    public View onCreateView(
            LayoutInflater inflater, ViewGroup container, Bundle savedInstanceState) {
        return inflater.inflate(R.layout.jatter_susi_fragment, container, false);
    }

    @Override
    public void onViewCreated(@NonNull View view, @Nullable Bundle savedInstanceState) {
        super.onViewCreated(view, savedInstanceState);
        
        mPromptLayout = view.findViewById(R.id.susi_prompt_layout);
        mWebContainer = view.findViewById(R.id.susi_web_container);

        view.findViewById(R.id.susi_skip_button).setOnClickListener(v -> {
            getPageDelegate().advanceToNextPage();
        });

        view.findViewById(R.id.susi_sign_in_button).setOnClickListener(v -> {
            // Hide prompt, show white web background
            mPromptLayout.setVisibility(View.GONE);
            mWebContainer.setVisibility(View.VISIBLE);
            
            startWebAuthFlow();
        });
    }

    private void startWebAuthFlow() {
        Profile profile = ProfileManager.getLastUsedRegularProfile();

        mWebContents = WebContentsFactory.createWebContents(
                profile, /* initiallyHidden= */ false, /* initializeRenderer= */ true);
        mContentView = ContentView.createContentView(getContext(), mWebContents);

        FirstRunActivity activity = (FirstRunActivity) getActivity();
        WindowAndroid window = activity != null ? activity.getWindowAndroid() : null;

        mWebContents.setDelegates(
                "", 
                ViewAndroidDelegate.createBasicDelegate(mContentView),
                mContentView, 
                window,
                WebContents.createDefaultInternalsHolder()
        );

        mThinWebView = ThinWebViewFactory.create(
                getContext(), 
                new ThinWebViewConstraints(), 
                window != null ? window.getIntentRequestTracker() : null
        );
        
        mThinWebView.attachWebContents(mWebContents, mContentView, new WebContentsDelegateAndroid() {});

        mWebContainer.addView(mThinWebView.getView(), new FrameLayout.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT,
                ViewGroup.LayoutParams.MATCH_PARENT));

        mWebContentsObserver = new WebContentsObserver(mWebContents) {
            @Override
            public void didStartNavigationInPrimaryMainFrame(NavigationHandle navigation) {
                GURL url = navigation.getUrl();
                if (url == null) return;

                if (JATTER_APP_HOST.equals(url.getHost())) {
                    String path = url.getPath();
                    if ("/sign_in/complete".equals(path) || "/sign_in/cancel".equals(path)) {
                        mWebContents.stop();
                        new Handler(Looper.getMainLooper()).post(() -> {
                            getPageDelegate().advanceToNextPage();
                        });
                    }
                }
            }
        };

        mWebContents.getNavigationController().loadUrl(
                new LoadUrlParams("https://" + JATTER_APP_HOST + "/sign_in?fre=true&client=android")
        );
    }

    @Override
    public void onDestroyView() {
        super.onDestroyView();
        if (mWebContentsObserver != null) mWebContentsObserver = null;
        if (mThinWebView != null) {
            mThinWebView.destroy();
            mThinWebView = null;
        }
        if (mWebContents != null) {
            mWebContents.destroy();
            mWebContents = null;
        }
    }
    
    @Override
    public void setInitialA11yFocus() {
        // No-op
    }
}
