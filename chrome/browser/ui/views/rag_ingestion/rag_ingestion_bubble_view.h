#ifndef CHROME_BROWSER_UI_VIEWS_RAG_INGESTION_RAG_INGESTION_BUBBLE_VIEW_H_
#define CHROME_BROWSER_UI_VIEWS_RAG_INGESTION_RAG_INGESTION_BUBBLE_VIEW_H_

#include "chrome/browser/ui/views/location_bar/location_bar_bubble_delegate_view.h"
#include "content/public/browser/web_contents.h"
#include "ui/base/metadata/metadata_header_macros.h"
#include "ui/views/controls/textfield/textfield_controller.h"

class Browser;
class RagIngestionPageActionController;

class RagIngestionBubbleView : public LocationBarBubbleDelegateView,
                               public views::TextfieldController {
  METADATA_HEADER(RagIngestionBubbleView, LocationBarBubbleDelegateView)

 public:
  // Static method to create and show the bubble
  static void Show(Browser* browser, 
                   RagIngestionPageActionController* controller, 
                   bool show_management_ui);

  ~RagIngestionBubbleView() override;

  bool HandleKeyEvent(views::Textfield* sender,
                      const ui::KeyEvent& key_event) override;

  // WidgetDelegate:
  void OnWidgetDestroying(views::Widget* widget) override;

 private:
  // [UPDATED] Private constructor
  RagIngestionBubbleView(views::View* anchor_view,
                         content::WebContents* web_contents,
                         bool show_management_ui);

  // [NEW] Separated UI initialization logic
  void BuildPermissionUI(); // The "Allow/Block" view
  void BuildActiveUI();     // The "Learning... [Cog]" view

  void TransitionToLoadingState();

  // Helper to handle button presses
  void OnAllow();
  void OnBlock();

  void OnSettingsClicked(const ui::Event& event);

  // WeakPtr to WebContents for safe access in callbacks
  base::WeakPtr<content::WebContents> web_contents_;
};

#endif  // CHROME_BROWSER_UI_VIEWS_RAG_INGESTION_RAG_INGESTION_BUBBLE_VIEW_H_