package org.chromium.chrome.browser.rag_ingestion;

import android.content.Context;
import android.os.Bundle;

import androidx.annotation.Nullable;

import org.jni_zero.CalledByNative;
import org.jni_zero.NativeMethods;
import org.chromium.content_public.browser.WebContents;
import org.chromium.components.browser_ui.bottomsheet.BottomSheetContent;
import org.chromium.components.browser_ui.bottomsheet.BottomSheetController;
import org.chromium.components.browser_ui.bottomsheet.BottomSheetControllerProvider;
import org.chromium.components.messages.MessageDispatcher;
import org.chromium.components.messages.MessageDispatcherProvider;
import org.chromium.ui.base.WindowAndroid;
import org.chromium.ui.modelutil.PropertyModel;

// Required imports for launching Site Settings (Phase 5)
import org.chromium.chrome.browser.settings.SettingsNavigationFactory;
import org.chromium.components.browser_ui.site_settings.R;
import org.chromium.components.browser_ui.site_settings.SingleWebsiteSettings;
import org.chromium.components.browser_ui.site_settings.SiteSettingsCategory;
import org.chromium.components.browser_ui.styles.SemanticColorUtils;

// Chromium Messages API components for the initial offer prompt
import org.chromium.components.messages.MessageBannerProperties;
import org.chromium.components.messages.MessageDispatcher;
import org.chromium.components.messages.MessageDispatcherProvider;
import org.chromium.components.messages.MessageIdentifier;
import org.chromium.components.messages.MessageScopeType;
import org.chromium.components.messages.PrimaryActionClickBehavior;

public class RagIngestionBridge {
    private long mNativePtr;
    private BottomSheetController mBottomSheetController;
    private BottomSheetContent mBottomSheetContent;
    private Observer mObserver; // The observer for the adaptive toolbar
    private BottomSheetContent mCurrentSheetContent;
    // Hold a static reference to the toolbar's button controller
    private static @Nullable Observer sToolbarObserver;
    // Message properties for the initial "Allow/Deny" offer
    private PropertyModel mOfferMessageModel;
    private MessageDispatcher mMessageDispatcher;

    // The observer interface that our Toolbar Button will listen to
    public interface Observer {
        void onToolbarStateChanged(RagIngestionBridge bridge, int state);
    }

    @CalledByNative
    private static RagIngestionBridge create(long nativePtr) {
        return new RagIngestionBridge(nativePtr);
    }

    private RagIngestionBridge(long nativePtr) {
        mNativePtr = nativePtr;
    }

    public static RagIngestionBridge fromWebContents(WebContents webContents) {
        if (webContents == null) return null;
        long ptr = RagIngestionBridgeJni.get().getBridgeForWebContents(webContents);
        return ptr == 0 ? null : RagIngestionBridge.create(ptr);
    }

    // =========================================================================
    // STATE & TOOLBAR MANAGEMENT
    // =========================================================================

    // Allows the Toolbar Button to subscribe to state changes
    public void setObserver(Observer observer) {
        mObserver = observer;
    }

    public static void setToolbarObserver(@Nullable Observer observer) {
        sToolbarObserver = observer;
    }

    // Called by C++ whenever the state (kOffer, kActive, kDisabled, kHidden) changes
    @CalledByNative
    private void onStateChanged(int state) {
        if (sToolbarObserver != null) {
            // CRITICAL: Pass 'this' so the toolbar controller can capture the active bridge!
            sToolbarObserver.onToolbarStateChanged(this, state);
        }
    }

    // Polling method for the Toolbar Button to check current state
    public int getToolbarState() {
        if (mNativePtr == 0) return 0; // 0 = kHidden
        return RagIngestionBridgeJni.get().getToolbarState(mNativePtr); // Removed 'caller'
    }

    // Called by the Toolbar Button when the user clicks it
    public void handleIconClick() {
        if (mNativePtr != 0) {
            RagIngestionBridgeJni.get().openSettings(mNativePtr); // Removed 'caller'
        }
    }

    // Called by C++ in response to handleIconClick()
   @CalledByNative
    private void openSiteSettings(WebContents webContents, String url) {
        if (webContents == null || webContents.getTopLevelNativeWindow() == null) return;
        Context context = webContents.getTopLevelNativeWindow().getActivity().get();
        if (context == null) return;

        // Use the explicit intent factory approach which is more stable
        Bundle arguments = new Bundle();
        arguments.putString(SingleWebsiteSettings.EXTRA_SITE_ADDRESS, url);
        
        // SettingsNavigation has a standard method to start settings fragments
        SettingsNavigationFactory.createSettingsNavigation()
                .startSettings(context, SingleWebsiteSettings.class, arguments);
    }

    // Called when the user clicks the Settings gear inside the bottom sheet
    public void onSettingsButtonClicked() {
        if (mBottomSheetController != null && mCurrentSheetContent != null) {
            mBottomSheetController.hideContent(mCurrentSheetContent, true);
        }
        if (mNativePtr != 0) {
            RagIngestionBridgeJni.get().openSettings(mNativePtr);
        }
    }

    // Called when the user submits a question in the EditText
    public void onQuerySubmitted(String query) {
        if (mBottomSheetController != null && mCurrentSheetContent != null) {
            mBottomSheetController.hideContent(mCurrentSheetContent, true);
        }
        if (mNativePtr != 0) {
            RagIngestionBridgeJni.get().onQuerySubmitted(mNativePtr, query);
        }
    }

    // =========================================================================
    // UI TIER 1: INITIAL OFFER PROMPT (MESSAGES API)
    // =========================================================================

    @CalledByNative
    private void showBottomSheet(WebContents webContents) {
        if (webContents == null || webContents.isDestroyed()) return;
        WindowAndroid windowAndroid = webContents.getTopLevelNativeWindow();
        if (windowAndroid == null) return;

        mMessageDispatcher = MessageDispatcherProvider.from(windowAndroid);
        if (mMessageDispatcher == null) return;

        Context context = windowAndroid.getContext().get();
        if (context == null) return;

        String siteName = webContents.getTitle();

        // Build the interactive message banner
        mOfferMessageModel = new PropertyModel.Builder(MessageBannerProperties.ALL_KEYS)
                .with(MessageBannerProperties.MESSAGE_IDENTIFIER, 
                        org.chromium.components.messages.MessageIdentifier.TEST_MESSAGE)
                .with(MessageBannerProperties.TITLE, 
                        "Enable personal answers for " + siteName + "?")
                .with(MessageBannerProperties.DESCRIPTION, 
                        "Jatter can securely learn about websites you log into in order to provide personal answers and assistance.")
                // .with(MessageBannerProperties.ICON_RESOURCE_ID, 
                //         org.chromium.components.browser_ui.site_settings.R.drawable.ic_rag_ingestion_24)
                // .with(MessageBannerProperties.ICON_TINT_COLOR, MessageBannerProperties.TINT_NONE)
                
                // 1. PRIMARY ACTION: Returns PrimaryActionClickBehavior
                .with(MessageBannerProperties.PRIMARY_BUTTON_TEXT, "Enable")
                .with(MessageBannerProperties.ON_PRIMARY_ACTION, () -> {
                    onUserDecision(true);
                    return PrimaryActionClickBehavior.DISMISS_IMMEDIATELY;
                })
                
                // 2. SECONDARY ACTION: Using our custom "text" vector graphic
                .with(MessageBannerProperties.SECONDARY_ICON_RESOURCE_ID, 
                        org.chromium.components.browser_ui.site_settings.R.drawable.ic_block_24dp)
                .with(MessageBannerProperties.SECONDARY_ICON_CONTENT_DESCRIPTION, "Don't Allow")
                .with(MessageBannerProperties.ON_SECONDARY_ACTION, () -> {
                    onUserDecision(false);
                    if (mMessageDispatcher != null && mOfferMessageModel != null) {
                        mMessageDispatcher.dismissMessage(mOfferMessageModel, 
                                org.chromium.components.messages.DismissReason.PRIMARY_ACTION);
                    }
                })
                
                // 3. PASSIVE DISMISSAL: User swiped away or timed out.
                // C++ stays at State 1 (kOffer) so clicking the adaptive icon brings this back!
                .with(MessageBannerProperties.ON_DISMISSED, (dismissReason) -> {
                    mOfferMessageModel = null;
                })
                .build();

        mMessageDispatcher.enqueueMessage(
                mOfferMessageModel,
                webContents,
                MessageScopeType.WEB_CONTENTS,
                false /* isHighPriority */);
    }

    // Called by C++ to show our new Active/Denied management UI
    @CalledByNative
    private void showManagementBottomSheet(WebContents webContents, String domain, boolean isAllowed) {
        if (webContents == null || webContents.isDestroyed()) return;
        WindowAndroid windowAndroid = webContents.getTopLevelNativeWindow();
        if (windowAndroid == null) return;

        mBottomSheetController = BottomSheetControllerProvider.from(windowAndroid);
        if (mBottomSheetController == null) return;

        // Instantiate our new Management sheet!
        mCurrentSheetContent = new RagIngestionManagementBottomSheetContent(
                windowAndroid.getContext().get(), this, domain, isAllowed);
                
        mBottomSheetController.requestShowContent(mCurrentSheetContent, true);
    }

    // =========================================================================
    // LIFECYCLE & CLEANUP
    // =========================================================================

    public void onUserDecision(boolean allowed) {
        // Programmatically dismiss the message if triggered externally
        if (mMessageDispatcher != null && mOfferMessageModel != null) {
            mMessageDispatcher.dismissMessage(mOfferMessageModel, 
                    org.chromium.components.messages.DismissReason.PRIMARY_ACTION);
            mOfferMessageModel = null;
        }
        
        if (mBottomSheetController != null && mCurrentSheetContent != null) {
            mBottomSheetController.hideContent(mCurrentSheetContent, true);
        }

        if (mNativePtr != 0) {
            RagIngestionBridgeJni.get().onUserDecision(mNativePtr, allowed);
        }
    }

    @CalledByNative
    private void destroy() {
        mNativePtr = 0;
        mObserver = null;
        
        // Clean up message banner if the C++ object dies prematurely
        if (mMessageDispatcher != null && mOfferMessageModel != null) {
            mMessageDispatcher.dismissMessage(mOfferMessageModel, 
                    org.chromium.components.messages.DismissReason.SCOPE_DESTROYED);
            mOfferMessageModel = null;
        }

        if (mBottomSheetController != null && mCurrentSheetContent != null) {
            mBottomSheetController.hideContent(mCurrentSheetContent, true);
        }
    }

    @NativeMethods
    public interface Natives {
        void onUserDecision(long nativeRagIngestionControllerAndroid, boolean allowed);
        int getToolbarState(long nativeRagIngestionControllerAndroid);
        void openSettings(long nativeRagIngestionControllerAndroid);
        long getBridgeForWebContents(WebContents webContents);
        void onQuerySubmitted(long nativeRagIngestionControllerAndroid, String query);
    }
}
