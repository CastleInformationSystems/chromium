// Copyright 2024 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/views/rag_ingestion/rag_ingestion_page_action_icon_view.h"

#include "chrome/app/vector_icons/vector_icons.h" // Includes kRagIngestionIcon
#include "chrome/browser/ui/rag_ingestion/rag_ingestion_page_action_controller.h"
#include "ui/compositor/paint_recorder.h"
#include "ui/gfx/canvas.h"
#include "ui/gfx/color_palette.h"
#include "ui/gfx/paint_vector_icon.h"

RagIngestionPageActionIconView::RagIngestionPageActionIconView(
    CommandUpdater* command_updater,
    IconLabelBubbleView::Delegate* icon_label_bubble_delegate,
    PageActionIconView::Delegate* page_action_icon_delegate)
    : PageActionIconView(nullptr,
                         0,
                         icon_label_bubble_delegate,
                         page_action_icon_delegate,
                         "RagIngestion") {
  SetAccessibleName(u"RAG Ingestion");
}

RagIngestionPageActionIconView::~RagIngestionPageActionIconView() = default;

views::BubbleDialogDelegate* RagIngestionPageActionIconView::GetBubble() const {
  // TODO(Phase 4b): Return the actual bubble controller here when we build it.
  return nullptr;
}

void RagIngestionPageActionIconView::UpdateImpl() {
  content::WebContents* web_contents = GetWebContents();
  if (!web_contents) return;

  auto* controller = RagIngestionPageActionController::FromWebContents(web_contents);
  bool visible = controller && controller->ShouldShowIcon();
  
  SetVisible(visible);

  if (visible) {
    // [CUSTOM COLOR APPLICATION]
    // RGB(73, 47, 140) matches your SVG
    SkColor brand_purple = SkColorSetRGB(73, 47, 140);
    SetImageModel(ui::ImageModel::FromVectorIcon(
        GetVectorIcon(), brand_purple));
  }
}

void RagIngestionPageActionIconView::OnExecuting(
    PageActionIconView::ExecuteSource execute_source) {
  content::WebContents* web_contents = GetWebContents();
  if (!web_contents) return;

  auto* controller = RagIngestionPageActionController::FromWebContents(web_contents);
  if (controller) {
    controller->OnIconClicked();
  }
}

void RagIngestionPageActionIconView::PaintChildren(const views::PaintInfo& paint_info) {
  // 1. Let the base class paint the icon, label, and ink-drop bounds first
  PageActionIconView::PaintChildren(paint_info);

  // 2. Open a raw paint recorder over the entire, unclipped bounds of the view
  ui::PaintRecorder recorder(paint_info.context(), size());
  gfx::Canvas* canvas = recorder.canvas();

  // 3. Draw our separator line on the far left edge
  SkColor separator_color = SkColorSetA(SK_ColorBLACK, 0x30); // Clean light gray
  float y_padding = height() * 0.25f; // Leaves 25% padding on top and bottom

  canvas->Draw1pxLine(gfx::PointF(0, y_padding), 
                      gfx::PointF(0, height() - y_padding), 
                      separator_color);
}

const gfx::VectorIcon& RagIngestionPageActionIconView::GetVectorIcon() const {
  // Uses the specific icon we created in Step 1
  return kRagIngestionIcon; 
}