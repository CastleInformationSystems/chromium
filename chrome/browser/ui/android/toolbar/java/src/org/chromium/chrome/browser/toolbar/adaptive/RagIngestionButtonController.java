package org.chromium.chrome.browser.toolbar.adaptive;

import android.content.Context;
import android.graphics.drawable.Drawable;

import androidx.annotation.Nullable;
import androidx.appcompat.content.res.AppCompatResources;

import org.chromium.base.Callback;
import org.chromium.base.ObserverList;
import org.chromium.base.supplier.ObservableSuppliers;
import org.chromium.base.supplier.MonotonicObservableSupplier;
import org.chromium.base.supplier.NullableObservableSupplier;

import org.chromium.chrome.browser.rag_ingestion.RagIngestionBridge;
import org.chromium.chrome.browser.tab.Tab;
import org.chromium.chrome.browser.toolbar.R;

import org.chromium.chrome.browser.toolbar.optional_button.ButtonData;
import org.chromium.chrome.browser.toolbar.optional_button.ButtonDataImpl;
import org.chromium.chrome.browser.toolbar.optional_button.ButtonDataProvider;
import org.chromium.chrome.browser.toolbar.optional_button.ButtonDataProvider.ButtonDataObserver;

public class RagIngestionButtonController implements ButtonDataProvider, RagIngestionBridge.Observer {
    private final NullableObservableSupplier<Tab> mTabSupplier;
    private final ButtonDataImpl mButtonData;
    private final ObserverList<ButtonDataObserver> mObservers = new ObserverList<>();
    private final Callback<Tab> mTabCallback = this::onTabChanged;
    
    // NEW: Keep a reference to the main toolbar orchestrator
    private final AdaptiveToolbarButtonController mToolbarOrchestrator;
    
    private @Nullable RagIngestionBridge mCurrentBridge;
    private boolean mCanShow;

    // Updated constructor taking the orchestrator
    public RagIngestionButtonController(
            Context context, 
            NullableObservableSupplier<Tab> tabSupplier,
            AdaptiveToolbarButtonController toolbarOrchestrator) {
        mTabSupplier = tabSupplier;
        mToolbarOrchestrator = toolbarOrchestrator; // Store it!

        RagIngestionBridge.setToolbarObserver(this);
        
        Drawable icon = AppCompatResources.getDrawable(context, R.drawable.ic_rag_ingestion_24);
        
        mButtonData = new ButtonDataImpl(
                /* canShow= */ false,
                /* drawable= */ icon,
                /* onClickListener= */ (view) -> { 
                    if (mCurrentBridge != null) mCurrentBridge.handleIconClick(); 
                },
                /* contentDescription= */ "RAG Ingestion",
                /* supportsTinting= */ false,
                /* iphCommandBuilder= */ null,
                /* isEnabled= */ true,
                /* buttonVariant= */ AdaptiveToolbarButtonVariant.RAG_INGESTION,
                /* tooltipTextResId= */ 0
        );

        mTabSupplier.addObserver(mTabCallback, 1 /* NOTIFY_ON_ADD */);
    }

    private void onTabChanged(@Nullable Tab tab) {
        if (mCurrentBridge != null) {
            mCurrentBridge.setObserver(null);
            mCurrentBridge = null;
        }

        if (tab != null && tab.getWebContents() != null) {
            mCurrentBridge = RagIngestionBridge.fromWebContents(tab.getWebContents());
            
            if (mCurrentBridge != null) {
                mCurrentBridge.setObserver(this);
                // Immediately poll and apply the active tab's state
                checkToolbarState(mCurrentBridge.getToolbarState());
                return;
            }
        }
        
        // CRITICAL FIX: If tab is null, destroyed, or has no RAG bridge (normal page),
        // explicitly force state 0 (kHidden) so mButtonData is set to FALSE!
        checkToolbarState(0);
    }

    @Override
    public void onToolbarStateChanged(RagIngestionBridge bridge, int state) {
        // CRITICAL FIX: Always capture the bridge that sent this state!
        if (bridge != null) {
            mCurrentBridge = bridge;
        }
        checkToolbarState(state);
    }

    private void checkToolbarState(int state) {
        // Show the button for ANY state as long as it isn't 0 (kHidden)
        // 1 = kOffer, 2 = kActive, 3 = kDisabled
        boolean newCanShow = (state != 0); 
        
        if (newCanShow != mCanShow) {
            mCanShow = newCanShow;
            
            // CRITICAL: Update the actual ButtonData object read by the orchestrator!
            mButtonData.setCanShow(mCanShow);
            notifyObservers(mCanShow);
            
            if (mCanShow) {
                // Force the Adaptive Toolbar to override default buttons immediately
                mToolbarOrchestrator.showDynamicAction(AdaptiveToolbarButtonVariant.RAG_INGESTION);
            } else {
                // Relinquish control back to standard session buttons (New Tab / Share)
                mToolbarOrchestrator.showDynamicAction(AdaptiveToolbarButtonVariant.UNKNOWN);
            }
        }
    }

    @Override
    public ButtonData get(Tab tab) {
        return mButtonData;
    }

    @Override
    public void destroy() {
        RagIngestionBridge.setToolbarObserver(null);
        mTabSupplier.removeObserver(mTabCallback);
        if (mCurrentBridge != null) mCurrentBridge.setObserver(null);
        mObservers.clear();
    }

    @Override
    public void addObserver(ButtonDataObserver obs) {
        mObservers.addObserver(obs);
    }

    @Override
    public void removeObserver(ButtonDataObserver obs) {
        mObservers.removeObserver(obs);
    }

    private void notifyObservers(boolean canShow) {
        for (ButtonDataObserver obs : mObservers) {
            obs.buttonDataChanged(canShow);
        }
    }
}