// Copyright 2024 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_UI_VIEWS_RAG_INGESTION_RAG_INGESTION_PAGE_ACTION_ICON_VIEW_H_
#define CHROME_BROWSER_UI_VIEWS_RAG_INGESTION_RAG_INGESTION_PAGE_ACTION_ICON_VIEW_H_

#include "chrome/browser/ui/views/page_action/page_action_icon_view.h"

class RagIngestionPageActionIconView : public PageActionIconView {
 public:
  RagIngestionPageActionIconView(
      CommandUpdater* command_updater,
      IconLabelBubbleView::Delegate* icon_label_bubble_delegate,
      PageActionIconView::Delegate* page_action_icon_delegate);
  ~RagIngestionPageActionIconView() override;

  // [FIX] Required by abstract base class PageActionIconView
  views::BubbleDialogDelegate* GetBubble() const override;

 protected:
  // PageActionIconView:
  void UpdateImpl() override;
  void OnExecuting(PageActionIconView::ExecuteSource execute_source) override;
  const gfx::VectorIcon& GetVectorIcon() const override;
  void PaintChildren(const views::PaintInfo& paint_info) override;
};

#endif  // CHROME_BROWSER_UI_VIEWS_RAG_INGESTION_RAG_INGESTION_PAGE_ACTION_ICON_VIEW_H_