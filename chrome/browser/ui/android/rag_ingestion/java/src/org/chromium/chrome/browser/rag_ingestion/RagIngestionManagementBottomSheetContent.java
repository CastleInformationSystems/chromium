// Copyright 2026 Jatter
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package org.chromium.chrome.browser.rag_ingestion;

import android.content.Context;
import android.graphics.Color;
import android.view.Gravity;
import android.view.KeyEvent;
import android.view.View;
import android.view.inputmethod.EditorInfo;
import android.widget.EditText;
import android.widget.ImageButton;
import android.widget.LinearLayout;
import android.widget.TextView;

import org.chromium.components.browser_ui.bottomsheet.BottomSheetContent;

/**
 * The visual UI definition for the RAG Ingestion Active/Management prompt on Android.
 * Mirrors Desktop's BuildActiveUI() in RagIngestionBubbleView.cc.
 */
public class RagIngestionManagementBottomSheetContent implements BottomSheetContent {
    private final View mContentView;
    private final RagIngestionBridge mBridge;

    public RagIngestionManagementBottomSheetContent(
            Context context, 
            RagIngestionBridge bridge, 
            String domain, 
            boolean isAllowed) {
        mBridge = bridge;
        
        // 1. Root Container
        LinearLayout rootLayout = new LinearLayout(context);
        rootLayout.setOrientation(LinearLayout.VERTICAL);
        rootLayout.setPadding(50, 40, 50, 60);
        rootLayout.setBackgroundColor(Color.WHITE);

        // =========================================================================
        // 2. HEADER ROW: [ Status Text ("Enabled for host") | Settings Cog ]
        // =========================================================================
        LinearLayout headerRow = new LinearLayout(context);
        headerRow.setOrientation(LinearLayout.HORIZONTAL);
        headerRow.setGravity(Gravity.CENTER_VERTICAL);

        TextView statusText = new TextView(context);
        statusText.setText(isAllowed ? "Enabled for " + domain : "Disabled for " + domain);
        statusText.setTextSize(16f);
        statusText.setTextColor(isAllowed ? Color.parseColor("#492F8C") : Color.GRAY); // Brand purple or grey
        statusText.setTypeface(null, android.graphics.Typeface.BOLD);
        
        // Push settings button to the right
        LinearLayout.LayoutParams textParams = new LinearLayout.LayoutParams(
                0, LinearLayout.LayoutParams.WRAP_CONTENT, 1.0f);
        statusText.setLayoutParams(textParams);
        headerRow.addView(statusText);

        // Settings Gear Button
        ImageButton settingsButton = new ImageButton(context);
        settingsButton.setImageResource(android.R.drawable.ic_menu_preferences); // Fallback framework icon
        settingsButton.setBackgroundColor(Color.TRANSPARENT);
        settingsButton.setOnClickListener((v) -> {
            mBridge.onSettingsButtonClicked();
        });
        headerRow.addView(settingsButton);

        rootLayout.addView(headerRow);

        // =========================================================================
        // 3. INPUT FIELD (Only show if allowed, exactly like Desktop!)
        // =========================================================================
        if (isAllowed) {
            EditText queryInput = new EditText(context);
            queryInput.setHint("Ask a question about this page...");
            queryInput.setTextSize(14f);
            queryInput.setMaxLines(1);
            queryInput.setSingleLine(true);
            queryInput.setImeOptions(EditorInfo.IME_ACTION_SEND);
            
            LinearLayout.LayoutParams inputParams = new LinearLayout.LayoutParams(
                    LinearLayout.LayoutParams.MATCH_PARENT, LinearLayout.LayoutParams.WRAP_CONTENT);
            inputParams.setMargins(0, 30, 0, 10);
            queryInput.setLayoutParams(inputParams);

            // Listen for the Enter/Send key on the virtual keyboard (Parity with HandleKeyEvent!)
            queryInput.setOnEditorActionListener((v, actionId, event) -> {
                if (actionId == EditorInfo.IME_ACTION_SEND || 
                    (event != null && event.getKeyCode() == KeyEvent.KEYCODE_ENTER)) {
                    
                    String query = v.getText().toString().trim();
                    if (!query.isEmpty()) {
                        mBridge.onQuerySubmitted(query);
                    }
                    return true;
                }
                return false;
            });

            rootLayout.addView(queryInput);
        }

        mContentView = rootLayout;
    }

    @Override
    public View getContentView() { return mContentView; }

    @Override
    public View getToolbarView() { return null; }

    @Override
    public int getVerticalScrollOffset() { return 0; }

    @Override
    public void destroy() {}

    @Override
    public int getPriority() { return BottomSheetContent.ContentPriority.HIGH; }

    @Override
    public boolean swipeToDismissEnabled() { return true; }

    @Override
    public int getPeekHeight() { return BottomSheetContent.HeightMode.DISABLED; }

    @Override
    public float getHalfHeightRatio() { return BottomSheetContent.HeightMode.DISABLED; }

    @Override
    public float getFullHeightRatio() { return BottomSheetContent.HeightMode.WRAP_CONTENT; }

    @Override
    public String getSheetContentDescription(Context context) { return "RAG Ingestion Management Prompt"; }

    @Override
    public int getSheetHalfHeightAccessibilityStringId() { return android.R.string.ok; }

    @Override
    public int getSheetFullHeightAccessibilityStringId() { return android.R.string.ok; }

    @Override
    public int getSheetClosedAccessibilityStringId() { return android.R.string.ok; }
}
