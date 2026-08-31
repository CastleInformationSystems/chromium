// Copyright 2024 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package org.chromium.chrome.browser.rag_ingestion;

import android.content.Context;
import android.graphics.Color;
import android.view.View;
import android.widget.Button;
import android.widget.LinearLayout;
import android.widget.TextView;

import org.chromium.components.browser_ui.bottomsheet.BottomSheetContent;

/**
 * The visual UI definition for the RAG Ingestion offer prompt.
 */
public class RagIngestionBottomSheetContent implements BottomSheetContent {
    private final View mContentView;
    private final RagIngestionBridge mBridge;

    public RagIngestionBottomSheetContent(Context context, RagIngestionBridge bridge) {
        mBridge = bridge;
        
        // Build a simple programmatic layout (No XML required for testing!)
        LinearLayout layout = new LinearLayout(context);
        layout.setOrientation(LinearLayout.VERTICAL);
        layout.setPadding(50, 50, 50, 50);
        layout.setBackgroundColor(Color.WHITE);

        TextView title = new TextView(context);
        title.setText("Allow RAG Ingestion?");
        title.setTextSize(20f);
        title.setTextColor(Color.BLACK);
        layout.addView(title);

        TextView desc = new TextView(context);
        desc.setText("This allows Chrome to read this page and send it to your AI backend.");
        desc.setPadding(0, 20, 0, 40);
        layout.addView(desc);

        Button allowButton = new Button(context);
        allowButton.setText("Allow");
        allowButton.setOnClickListener((v) -> {
            mBridge.onUserDecision(true);
        });
        layout.addView(allowButton);

        Button denyButton = new Button(context);
        denyButton.setText("Don't Allow");
        denyButton.setOnClickListener((v) -> {
            mBridge.onUserDecision(false);
        });
        layout.addView(denyButton);

        mContentView = layout;
    }

    @Override
    public View getContentView() {
        return mContentView;
    }

    @Override
    public View getToolbarView() {
        return null; // No custom toolbar needed
    }

    @Override
    public int getVerticalScrollOffset() {
        return 0;
    }

    @Override
    public void destroy() {
        // Called when the sheet is completely destroyed
    }

    @Override
    public int getPriority() {
        return BottomSheetContent.ContentPriority.HIGH;
    }

    @Override
    public boolean swipeToDismissEnabled() {
        return true;
    }

    @Override
    public int getPeekHeight() {
        return BottomSheetContent.HeightMode.DISABLED;
    }

    @Override
    public float getHalfHeightRatio() {
        return BottomSheetContent.HeightMode.DISABLED;
    }

    @Override
    public float getFullHeightRatio() {
        return BottomSheetContent.HeightMode.WRAP_CONTENT;
    }

    @Override
    public String getSheetContentDescription(Context context) {
        return "RAG Ingestion Prompt";
    }

    @Override
    public int getSheetHalfHeightAccessibilityStringId() {
        // Use a safe existing resource ID from the Android/Chromium framework
        return android.R.string.ok; 
    }

    @Override
    public int getSheetFullHeightAccessibilityStringId() {
        return android.R.string.ok;
    }

    @Override
    public int getSheetClosedAccessibilityStringId() {
        return android.R.string.ok;
    }
}