#include "chrome/browser/ui/views/rag_ingestion/rag_ingestion_bubble_view.h"

#include "base/strings/escape.h"
#include "base/i18n/time_formatting.h"
#include "chrome/app/vector_icons/vector_icons.h" // For kSettingsIcon
#include "chrome/browser/jatter/jatter_environment.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/browser_window.h"
#include "chrome/browser/platform_util.h"
#include "chrome/browser/ui/views/chrome_layout_provider.h"
#include "chrome/browser/ui/views/frame/browser_view.h"
#include "chrome/browser/ui/views/location_bar/location_bar_view.h"
#include "chrome/browser/ui/views/page_action/page_action_icon_view.h"
#include "chrome/browser/ui/views/page_action/page_action_icon_controller.h"
#include "chrome/browser/ui/rag_ingestion/rag_ingestion_page_action_controller.h"
#include "chrome/browser/rag_ingestion/grit/rag_ingestion_strings.h"
#include "components/strings/grit/components_strings.h"
#include "components/vector_icons/vector_icons.h" // Sometimes needed for generic icons
#include "content/public/browser/page_navigator.h"
#include "ui/base/l10n/l10n_util.h"
#include "ui/base/metadata/metadata_impl_macros.h"
#include "ui/base/ui_base_types.h"
#include "ui/events/event.h"
#include "ui/views/background.h"
#include "ui/views/border.h"
#include "ui/views/controls/button/md_text_button.h" // [NEW] For custom buttons
#include "ui/views/controls/label.h"
#include "ui/views/controls/link.h" // [NEW] For the "Learn more" link
#include "ui/views/controls/textfield/textfield.h"
#include "ui/views/controls/throbber.h"
#include "ui/views/controls/styled_label.h"
#include "ui/views/controls/button/image_button_factory.h" // For CreateVectorImageButton
#include "ui/views/controls/highlight_path_generator.h"
#include "ui/views/layout/box_layout.h" // [NEW] For vertical stacking
#include "ui/views/layout/layout_provider.h"
#include "ui/views/view_class_properties.h"


// Define the Brand Purple Color (matches your icon file)
constexpr SkColor kBrandPurple = SkColorSetRGB(73, 47, 140);
constexpr SkColor kWhiteBG = SkColorSetRGB(255,255,255);
constexpr SkColor kGreyStroke = SkColorSetRGB(211,211,211);

constexpr SkColor kBrandTerracotta = SkColorSetRGB(194, 110, 96);

// ... [Show() method remains unchanged] ...
void RagIngestionBubbleView::Show(Browser* browser, 
                                  RagIngestionPageActionController* controller, 
                                  bool show_management_ui) {
  if (!browser || !controller) return;

  // [FIX] Extract WebContents from the controller
  content::WebContents* web_contents = controller->web_contents();
  if (!web_contents) return;

  BrowserView* browser_view = BrowserView::GetBrowserViewForBrowser(browser);
  if (!browser_view) return;

  LocationBarView* location_bar = browser_view->GetLocationBarView();
  if (!location_bar) return;

  PageActionIconController* icon_controller = location_bar->page_action_icon_controller();
  if (!icon_controller) return;

  views::View* anchor_view = icon_controller->GetIconView(PageActionIconType::kRagIngestion);
  if (!anchor_view) return;

  RagIngestionBubbleView* bubble = 
      new RagIngestionBubbleView(anchor_view, web_contents, show_management_ui);
  views::BubbleDialogDelegateView::CreateBubble(bubble)->Show();
}

RagIngestionBubbleView::RagIngestionBubbleView(
    views::View* anchor_view,
    content::WebContents* web_contents,
    bool show_management_ui)
    : LocationBarBubbleDelegateView(anchor_view, web_contents),
      web_contents_(web_contents->GetWeakPtr()) {
  int bubble_width = ChromeLayoutProvider::Get()->GetDistanceMetric(
      views::DISTANCE_BUBBLE_PREFERRED_WIDTH);
  set_fixed_width(bubble_width);

  bool is_disabled = false;
  
  auto* controller = RagIngestionPageActionController::FromWebContents(web_contents_.get());
  if (controller) {
    is_disabled = (controller->GetIconState() == RagIngestionPageActionController::IconState::kDisabled);
  }

  if (show_management_ui || is_disabled) {
    BuildActiveUI(); // State B: Already Allowed (Management)
  } else {
    BuildPermissionUI(); // State A: Requesting Permission
  }
}

// -----------------------------------------------------------------------------
// UI Builders
// -----------------------------------------------------------------------------

void RagIngestionBubbleView::BuildPermissionUI() {
  auto* controller = RagIngestionPageActionController::FromWebContents(web_contents_.get());
  RagIngestionService::BackendPermissionInfo info;
  if (controller) {
      info = controller->GetBackendInfo();
  }
  // 1. DISABLE STANDARD DIALOG BUTTONS
  // We do this to gain full control over the button styling (purple background).
  SetButtons(static_cast<int>(ui::mojom::DialogButton::kNone));

  // [NEW] Fetch the site name and inject it into the title
  std::u16string site_name = web_contents_->GetTitle();

  ChromeLayoutProvider* provider = ChromeLayoutProvider::Get();
  
  // If the title is empty (common on new tabs), fallback to the host name
  if (site_name.empty()) {
    site_name = base::UTF8ToUTF16(web_contents_->GetLastCommittedURL().host());
  }
  set_title_margins(provider->GetInsetsMetric(views::INSETS_DIALOG_TITLE));
  SetSubtitleAllowCharacterBreak(true);  // For extremely long words without spaces

  // Allow the bubble's title string to wrap to multiple lines
  SetTitle(l10n_util::GetStringFUTF16(IDS_RAG_INGESTION_BUBBLE_TITLE, site_name));
  SetShowCloseButton(true);

  // 2. SWITCH TO BOX LAYOUT (Vertical Stack)
  SetLayoutManager(std::make_unique<views::BoxLayout>(
      views::BoxLayout::Orientation::kVertical,
      gfx::Insets(), // Insets handled by the bubble frame usually, or add provider->GetInsetsMetric(views::INSETS_DIALOG)
      provider->GetDistanceMetric(views::DISTANCE_RELATED_CONTROL_VERTICAL)));

  // 3. BODY TEXT
  auto label = std::make_unique<views::Label>(
      l10n_util::GetStringUTF16(IDS_RAG_INGESTION_BUBBLE_TEXT),
      views::style::CONTEXT_DIALOG_BODY_TEXT,
      views::style::STYLE_SECONDARY);
  label->SetMultiLine(true);
  label->SetHorizontalAlignment(gfx::ALIGN_LEFT);
  AddChildView(std::move(label));

  // 4. LEARN MORE LINK
  auto link = std::make_unique<views::Link>(
      u"Learn more about personal answers."); // Replace with IDS string
  link->SetHorizontalAlignment(gfx::ALIGN_LEFT);
  link->SetCallback(base::BindRepeating([](){ 
    platform_util::OpenExternal(GURL("https://www.jatter.ai/help/features/personal-answers/"));
  }));
  // Optional: Force purple link color if strictly required
  link->SetEnabledColor(kBrandPurple); 
  AddChildView(std::move(link));

  // 5. CUSTOM BUTTON CONTAINER
  auto* button_container = AddChildView(std::make_unique<views::View>());
  auto* button_layout = button_container->SetLayoutManager(std::make_unique<views::BoxLayout>(
      views::BoxLayout::Orientation::kHorizontal,
      gfx::Insets::TLBR(20, 0, 0, 0), // Add top spacing
      provider->GetDistanceMetric(views::DISTANCE_RELATED_BUTTON_HORIZONTAL)));
  button_layout->set_main_axis_alignment(views::BoxLayout::MainAxisAlignment::kEnd);

  // 6. "NEVER" BUTTON (Outlined/Ghost style)
  auto never_button = std::make_unique<views::MdTextButton>(
      base::BindRepeating(&RagIngestionBubbleView::OnBlock, base::Unretained(this)),
      l10n_util::GetStringUTF16(IDS_RAG_INGESTION_BLOCK_BUTTON));
  never_button->SetCornerRadius(5);
  never_button->SetBgColorOverrideDeprecated(kWhiteBG); // Force the background purple
  never_button->SetStrokeColorOverrideDeprecated(kGreyStroke);
  never_button->SetStyle(ui::ButtonStyle::kProminent); // Makes it look like a ghost button
  never_button->SetEnabledTextColors(kBrandPurple); // Purple text
  button_container->AddChildView(std::move(never_button));

  // 7. "ENABLE" BUTTON (Filled/Prominent style)
  auto enable_button = std::make_unique<views::MdTextButton>(
      base::BindRepeating(&RagIngestionBubbleView::OnAllow, base::Unretained(this)),
      l10n_util::GetStringUTF16(IDS_RAG_INGESTION_ALLOW_BUTTON));
  enable_button->SetStyle(ui::ButtonStyle::kProminent); // Makes it a filled button
  enable_button->SetCornerRadius(2);
  enable_button->SetBgColorOverrideDeprecated(kBrandPurple); // Force the background purple
  enable_button->SetEnabledTextColors(SK_ColorWHITE);
  button_container->AddChildView(std::move(enable_button));
}

void RagIngestionBubbleView::BuildActiveUI() {
  SetTitle(std::u16string()); // No default title bar
  SetButtons(static_cast<int>(ui::mojom::DialogButton::kNone)); // No system buttons

  SetLayoutManager(std::make_unique<views::BoxLayout>(
      views::BoxLayout::Orientation::kVertical,
      gfx::Insets(), // Internal padding handled by child views
      12)); // Vertical gap between main sections (Header, Input, Footer

  
  // Get Data
  auto* controller = RagIngestionPageActionController::FromWebContents(web_contents_.get());
  RagIngestionService::BackendPermissionInfo info;
  bool is_enabled = false;

  if (controller) {
      info = controller->GetBackendInfo();
      
      // If the controller is in the Active state, we are Enabled. 
      // This bypasses the stale 'kUnknown' backend status completely.
      is_enabled = (controller->GetIconState() == RagIngestionPageActionController::IconState::kActive);
  }

  std::string host = info.canonical_host.empty() ? 
      std::string(web_contents_->GetLastCommittedURL().host()) : info.canonical_host;
  std::u16string host16 = base::UTF8ToUTF16(host);
  std::u16string site_name16 = info.site_name.empty() ? web_contents_->GetTitle() : base::UTF8ToUTF16(info.site_name);

  // =========================================================================
  // 1. TOP ROW: [ Icon | "Enabled for Host" | Settings ]
  // =========================================================================
  auto header_row = std::make_unique<views::View>();
  auto* header_layout = header_row->SetLayoutManager(std::make_unique<views::BoxLayout>(
      views::BoxLayout::Orientation::kHorizontal,
      gfx::Insets(), 
      10)); // Gap between icon and text

  // A. Large Squircle Icon (Left)
  auto icon = std::make_unique<views::ImageView>();
  
  // 1. Set the icon to be pure white and 24px wide
  icon->SetImage(ui::ImageModel::FromVectorIcon(
      kRagIngestionBellIcon, 
      SK_ColorWHITE, 
      24));
      
  // 2. Apply the Terracotta background with a 12px corner radius
  icon->SetBackground(views::CreateRoundedRectBackground(kBrandTerracotta, 12));
  
  // 3. Add 10px of padding inside the background so the icon isn't cramped.
  // This makes the total visual size 44x44 (10 + 24 + 10)
  icon->SetBorder(views::CreateEmptyBorder(gfx::Insets(10)));
  header_layout->SetFlexForView(header_row->AddChildView(std::move(icon)), 0);

  // B. Status Text (Middle - Flex)
  std::u16string base_text;
  if (is_enabled) {
      base_text = l10n_util::GetStringFUTF16(
          IDS_RAG_INGESTION_TRANSITION_BUBBLE_DOMAIN_ENABLED_FOR_TEXT, host16);
  } else {
      base_text = l10n_util::GetStringFUTF16(
          IDS_RAG_INGESTION_TRANSITION_BUBBLE_DOMAIN_DISABLED_FOR_TEXT, host16);
  }
  size_t offset = base_text.find(host16);

  auto status_label = std::make_unique<views::StyledLabel>();
  status_label->SetText(base_text);
  status_label->SetHorizontalAlignment(gfx::ALIGN_LEFT);

  // Force the StyledLabel to wrap text and break long URLs
  status_label->SetDefaultTextStyle(views::style::STYLE_SECONDARY);
  
  // Calculate max width: Bubble Width (320px) - Icon (44px) - Cog (~30px) - Padding (20px)
  int max_label_width = ChromeLayoutProvider::Get()->GetDistanceMetric(
      views::DISTANCE_BUBBLE_PREFERRED_WIDTH) - 94;
  
  status_label->SizeToFit(max_label_width); 

  if (offset != std::u16string::npos) {
    views::StyledLabel::RangeStyleInfo bold_style;
    bold_style.text_style = views::style::STYLE_EMPHASIZED;
    status_label->AddStyleRange(gfx::Range(offset, offset + host16.length()), bold_style);
  }
  
  // Critical: Set Flex=1 so this label consumes all available middle space
  header_layout->SetFlexForView(header_row->AddChildView(std::move(status_label)), 1);

  // C. Settings Cog (Right)
  auto settings_button = views::CreateVectorImageButtonWithNativeTheme(
      base::BindRepeating(&RagIngestionBubbleView::OnSettingsClicked, base::Unretained(this)),
      vector_icons::kSettingsIcon);
  settings_button->SetTooltipText(l10n_util::GetStringUTF16(IDS_SETTINGS_TITLE));
  header_layout->SetFlexForView(header_row->AddChildView(std::move(settings_button)), 0);

  AddChildView(std::move(header_row));

  // =========================================================================
  // CONDITION: Only show Input Field and Learning Status if enabled
  // =========================================================================
  if (is_enabled) {
    // =========================================================================
    // 2. MIDDLE: Input Field
    // =========================================================================
    auto textfield = std::make_unique<views::Textfield>();
    textfield->SetPlaceholderText(l10n_util::GetStringFUTF16(
        IDS_RAG_INGESTION_TRANSITION_BUBBLE_ASK_ABOUT_TEXT, site_name16));
    textfield->SetEnabled(info.can_prompt);
    textfield->SetAccessibleName(u"Ask a question");
    textfield->set_controller(this);
    
    // Add some padding to match the whitespace in the screenshot
    auto* input_container = AddChildView(std::make_unique<views::View>());
    input_container->SetLayoutManager(std::make_unique<views::BoxLayout>(
        views::BoxLayout::Orientation::kVertical, 
        gfx::Insets::VH(4, 0))); // Vertical padding
    input_container->AddChildView(std::move(textfield));


    // // =========================================================================
    // // 3. BOTTOM: Learning Status
    // // =========================================================================
    // auto footer_row = std::make_unique<views::View>();
    // footer_row->SetLayoutManager(std::make_unique<views::BoxLayout>(
    //     views::BoxLayout::Orientation::kHorizontal,
    //     gfx::Insets(), 
    //     6)); // Gap between spinner and text

    // auto status_text = std::make_unique<views::Label>();
    // status_text->SetHorizontalAlignment(gfx::ALIGN_LEFT);
    // status_text->SetEnabledColor(kGreyStroke); // Use a lighter grey text color (optional)

    // if (info.is_learning) {
    //     // Spinner
    //     auto throbber = std::make_unique<views::Throbber>();
    //     throbber->Start();
    //     footer_row->AddChildView(std::move(throbber));
        
    //     // Text
    //     status_text->SetText(l10n_util::GetStringUTF16(IDS_RAG_INGESTION_TRANSITION_BUBBLE_STATUS_TEXT)); // "Learning"
    // } else {
    //     // Static Date
    //     base::Time time = base::Time::FromMillisecondsSinceUnixEpoch(info.last_success_at_ms);
    //     // "Last learned on Feb 18"
    //     status_text->SetText(u"Last learned on " + base::TimeFormatShortDate(time));
    // }
    
    // footer_row->AddChildView(std::move(status_text));
    // AddChildView(std::move(footer_row));
  }
}

void RagIngestionBubbleView::TransitionToLoadingState() {
  // 1. RESET VIEW
  RemoveAllChildViews();
  BuildActiveUI();
  SizeToContents();
}

void RagIngestionBubbleView::OnSettingsClicked(const ui::Event& event) {
  if (web_contents_) {
    auto* controller = RagIngestionPageActionController::FromWebContents(web_contents_.get());
    if (controller) {
      controller->OpenSettings();
    }
  }
  GetWidget()->Close();
}

RagIngestionBubbleView::~RagIngestionBubbleView() = default;

// ... [Event Handlers remain unchanged] ...
void RagIngestionBubbleView::OnWidgetDestroying(views::Widget* widget) {
  LocationBarBubbleDelegateView::OnWidgetDestroying(widget);
}

void RagIngestionBubbleView::OnAllow() {
  if (web_contents_) {
    auto* controller = RagIngestionPageActionController::FromWebContents(web_contents_.get());
    if (controller) {
      // 1. Fetch the backend info to check the requiresUpgrade flag
      RagIngestionService::BackendPermissionInfo info = controller->GetBackendInfo();

      // 2. Branch logic based on the flag
      if (info.requires_upgrade) {
        // [NEW] Upgrade flow: Open the tab and DO NOT grant permission
        controller->OnUpgradeRequired();
        GetWidget()->Close(); 
        return; 
      }

      // 3. Normal flow: Grant permission
      controller->OnPermissionUserDecision(true);
    }
  }

  TransitionToLoadingState();
}

void RagIngestionBubbleView::OnBlock() {
  if (web_contents_) {
    auto* controller = RagIngestionPageActionController::FromWebContents(web_contents_.get());
    if (controller) controller->OnPermissionUserDecision(false);
  }
  GetWidget()->Close();
}

bool RagIngestionBubbleView::HandleKeyEvent(views::Textfield* sender,
                                            const ui::KeyEvent& key_event) {
  // [REQ 7] When the user submits a query (Hits Enter)
  if (key_event.type() == ui::EventType::kKeyPressed &&
      key_event.key_code() == ui::VKEY_RETURN) {
      
    std::u16string_view query = sender->GetText();
    if (!query.empty() && web_contents_) {
      
      // A. URL-encode the user input
      std::string escaped_query = base::EscapeQueryParamValue(
          base::UTF16ToUTF8(query), true);

      // B. Truncate to 512 characters max
      if (escaped_query.length() > 512) {
          escaped_query = escaped_query.substr(0, 512);
      }

      // C. Construct the URL (Using production host as default)
      // Note: In a full implementation, you would read the {jatter-host} from a build flag 
      // or command line switch as requested in 7a.
      std::string url_str = jatter::kAppPromptUrl + escaped_query;

      // D. Open in a new tab
      content::OpenURLParams params(
          GURL(url_str), content::Referrer(),
          WindowOpenDisposition::NEW_FOREGROUND_TAB,
          ui::PAGE_TRANSITION_GENERATED, false);
      
      web_contents_->OpenURL(params, /*navigation_handle_callback=*/{});
    }

    // Close the bubble after submission
    GetWidget()->CloseWithReason(views::Widget::ClosedReason::kAcceptButtonClicked);
    return true; // Event handled
  }
  
  return false; // Let default textfield behavior handle other keys
}

BEGIN_METADATA(RagIngestionBubbleView)
END_METADATA