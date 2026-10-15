// Copyright (c) 2026 The Brave Authors. All rights reserved.
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this file,
// You can obtain one at https://mozilla.org/MPL/2.0/.

#include "brave/browser/ui/views/frame/origin_address_pill.h"

#include <algorithm>
#include <string>
#include <utility>

#include "base/feature_list.h"
#include "base/functional/bind.h"
#include "base/functional/callback_helpers.h"
#include "brave/browser/ui/views/brave_actions/brave_shields_toolbar_button.h"
#include "brave/browser/ui/views/frame/origin_chrome_style.h"
#include "brave/browser/ui/webui/brave_shields/shields_panel_ui.h"
#include "brave/components/vector_icons/vector_icons.h"
#include "cc/paint/paint_flags.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/browser_window/public/browser_window_features.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/browser/ui/views/bubble/webui_bubble_manager.h"
#include "chrome/browser/ui/views/extensions/extensions_menu_view.h"
#include "chrome/browser/ui/views/extensions/extensions_toolbar_button.h"
#include "chrome/browser/ui/views/extensions/extensions_toolbar_desktop.h"
#include "chrome/browser/ui/views/frame/browser_view.h"
#include "chrome/browser/ui/views/page_info/page_info_bubble_specification.h"
#include "chrome/browser/ui/views/page_info/page_info_bubble_view.h"
#include "chrome/browser/ui/views/toolbar/toolbar_view.h"
#include "components/omnibox/browser/location_bar_model.h"
#include "components/security_state/core/security_state.h"
#include "components/url_formatter/url_formatter.h"
#include "content/public/browser/navigation_entry.h"
#include "content/public/browser/web_contents.h"
#include "extensions/common/extension_features.h"
#include "ui/base/accelerators/accelerator.h"
#include "ui/base/metadata/metadata_impl_macros.h"
#include "ui/base/models/image_model.h"
#include "ui/events/event_constants.h"
#include "ui/events/keycodes/keyboard_codes.h"
#include "ui/gfx/canvas.h"
#include "ui/gfx/text_constants.h"
#include "ui/views/accessibility/view_accessibility.h"
#include "ui/views/animation/ink_drop.h"
#include "ui/views/controls/button/button.h"
#include "ui/views/controls/button/image_button.h"
#include "ui/views/controls/image_view.h"
#include "ui/views/controls/label.h"
#include "ui/views/controls/menu/menu_runner.h"
#include "ui/views/layout/box_layout.h"
#include "ui/views/view_class_properties.h"
#include "url/gurl.h"
#include "url/url_constants.h"

namespace {

constexpr int kPillHeight = 34;
constexpr int kPillRadius = 10;
constexpr int kHostInset = 10;
constexpr int kButtonSize = 26;
constexpr int kIconSize = 16;
constexpr int kButtonGap = 2;
constexpr int kTrailingInset = 4;
// Buttons give way to a host this wide; a longer one may be cut short.
constexpr int kComfortableHostWidth = 150;

std::unique_ptr<views::ImageButton> MakeIconButton(
    views::Button::PressedCallback callback,
    const std::u16string& name) {
  auto button = std::make_unique<views::ImageButton>(std::move(callback));
  button->SetPreferredSize(gfx::Size(kButtonSize, kButtonSize));
  button->SetImageHorizontalAlignment(views::ImageButton::ALIGN_CENTER);
  button->SetImageVerticalAlignment(views::ImageButton::ALIGN_MIDDLE);
  button->SetAccessibleName(name);
  button->SetTooltipText(name);
  views::InkDrop::Get(button.get())
      ->SetMode(views::InkDropHost::InkDropMode::OFF);
  return button;
}

void SetIconButtonImage(views::ImageButton* button,
                        const gfx::VectorIcon& icon,
                        bool dark) {
  button->SetImageModel(
      views::Button::STATE_NORMAL,
      ui::ImageModel::FromVectorIcon(
          icon, origin_chrome_style::SecondaryText(dark), kIconSize));
  button->SetImageModel(
      views::Button::STATE_HOVERED,
      ui::ImageModel::FromVectorIcon(
          icon, origin_chrome_style::PrimaryText(dark), kIconSize));
  button->SetImageModel(
      views::Button::STATE_PRESSED,
      ui::ImageModel::FromVectorIcon(
          icon, origin_chrome_style::PrimaryText(dark), kIconSize));
}

// Arc shows the bare host: the scheme, "www." and the path are noise in a
// sidebar this narrow. Other pages show what the omnibox would.
std::u16string GetHostText(const LocationBarModel& model) {
  if (!model.ShouldDisplayURL()) {
    return std::u16string();
  }
  const GURL url = model.GetURL();
  if (url.SchemeIsHTTPOrHTTPS() && url.has_host()) {
    return url_formatter::IDNToUnicode(
        url_formatter::StripWWW(std::string(url.host())));
  }
  if (url.is_empty() || url.IsAboutBlank()) {
    return std::u16string();
  }
  return model.GetURLForDisplay();
}

}  // namespace

// The host, and a warning before it when the page is not secure. Pressing it
// edits the address.
class OriginAddressPill::HostButton : public views::Button {
  METADATA_HEADER(HostButton, views::Button)

 public:
  explicit HostButton(PressedCallback callback)
      : views::Button(std::move(callback)) {
    views::InkDrop::Get(this)->SetMode(views::InkDropHost::InkDropMode::OFF);
    auto* layout = SetLayoutManager(std::make_unique<views::BoxLayout>(
        views::BoxLayout::Orientation::kHorizontal,
        gfx::Insets::TLBR(0, kHostInset, 0, 4), 6));
    layout->set_cross_axis_alignment(
        views::BoxLayout::CrossAxisAlignment::kCenter);
    warning_ = AddChildView(std::make_unique<views::ImageView>());
    warning_->SetVisible(false);
    label_ = AddChildView(std::make_unique<views::Label>());
    label_->SetHorizontalAlignment(gfx::ALIGN_LEFT);
    // The end of a host is the part that names the site.
    label_->SetElideBehavior(gfx::ELIDE_HEAD);
    label_->SetFontList(
        origin_chrome_style::Font(13, gfx::Font::Weight::NORMAL));
    label_->SetAutoColorReadabilityEnabled(false);
    layout->SetFlexForView(label_, 1);
  }

  void SetHost(std::u16string host,
               security_state::SecurityLevel level,
               std::u16string security_text) {
    host_ = std::move(host);
    level_ = level;
    label_->SetText(host_.empty() ? u"Search or enter address" : host_);
    const bool warn = !host_.empty() && (level == security_state::WARNING ||
                                         level == security_state::DANGEROUS);
    warning_->SetVisible(warn);
    std::u16string name = host_.empty() ? u"Address" : u"Address: " + host_;
    if (warn && !security_text.empty()) {
      name += u", " + security_text;
    }
    GetViewAccessibility().SetName(name);
    SetTooltipText(host_.empty() ? u"Search or enter address (⌘L)"
                                 : u"Edit address (⌘L)");
    UpdateColors();
  }

  const std::u16string& host() const { return host_; }

  void UpdateColors() {
    const bool dark = origin_chrome_style::IsDark(this);
    label_->SetEnabledColor(host_.empty()
                                ? origin_chrome_style::MutedText(dark)
                                : origin_chrome_style::PrimaryText(dark));
    warning_->SetImage(ui::ImageModel::FromVectorIcon(
        kLeoWarningTriangleOutlineIcon,
        level_ == security_state::DANGEROUS
            ? origin_chrome_style::kAccent
            : origin_chrome_style::SecondaryText(dark),
        14));
  }

  // views::Button:
  void StateChanged(ButtonState old_state) override {
    views::Button::StateChanged(old_state);
    // The pill lights up as a whole, as a field would.
    if (parent()) {
      parent()->SchedulePaint();
    }
  }

  void OnThemeChanged() override {
    views::Button::OnThemeChanged();
    UpdateColors();
  }

 private:
  std::u16string host_;
  security_state::SecurityLevel level_ = security_state::NONE;
  raw_ptr<views::ImageView> warning_ = nullptr;
  raw_ptr<views::Label> label_ = nullptr;
};

BEGIN_METADATA(OriginAddressPill, HostButton)
END_METADATA

OriginAddressPill::Callbacks::Callbacks() = default;
OriginAddressPill::Callbacks::Callbacks(Callbacks&&) = default;
OriginAddressPill::Callbacks& OriginAddressPill::Callbacks::operator=(
    Callbacks&&) = default;
OriginAddressPill::Callbacks::~Callbacks() = default;

OriginAddressPill::OriginAddressPill(Browser* browser, Callbacks callbacks)
    : browser_(browser), callbacks_(std::move(callbacks)) {
  host_button_ = AddChildView(std::make_unique<HostButton>(base::BindRepeating(
      &OriginAddressPill::OnHostPressed, base::Unretained(this))));
  extensions_button_ = AddChildView(
      MakeIconButton(base::BindRepeating(&OriginAddressPill::ShowExtensions,
                                         base::Unretained(this)),
                     u"Extensions"));
  copy_button_ = AddChildView(
      MakeIconButton(base::BindRepeating(
                         [](OriginAddressPill* pill) {
                           pill->callbacks_.run_command.Run(kCopyLink);
                         },
                         base::Unretained(this)),
                     u"Copy Link (Y)"));
  // A right-click offers the other formats.
  copy_button_->set_context_menu_controller(this);
  shields_button_ = AddChildView(std::make_unique<BraveShieldsToolbarButton>(
      browser_,
      base::BindRepeating(&WebUIBubbleManager::Create<ShieldsPanelUI>)));
  shields_button_->SetOriginTitleBarStyle();
  // The hidden top bar keeps its own Shields button; tutorials and tests
  // that look Shields up by identifier should keep finding that one.
  shields_button_->ClearProperty(views::kElementIdentifierKey);
  menu_button_ = AddChildView(MakeIconButton(
      base::BindRepeating(&OriginAddressPill::OnMenuButtonPressed,
                          base::Unretained(this)),
      u"Page actions"));
  UpdateColors();
}

OriginAddressPill::~OriginAddressPill() = default;

void OriginAddressPill::OnHostPressed() {
  callbacks_.edit_address.Run();
}

void OriginAddressPill::Update() {
  const LocationBarModel* model = LocationBarModel::From(browser_.get());
  if (!model) {
    return;
  }
  const std::u16string host = GetHostText(*model);
  has_page_ = !host.empty();
  host_button_->SetHost(host, model->GetSecurityLevel(),
                        model->GetSecureDisplayText());
  shields_button_->Update();
  InvalidateLayout();
}

void OriginAddressPill::ShowSiteSettings() {
  content::WebContents* contents =
      browser_->tab_strip_model()->GetActiveWebContents();
  if (!contents || !GetWidget()) {
    return;
  }
  content::NavigationEntry* entry = contents->GetController().GetVisibleEntry();
  if (!entry || entry->IsInitialEntry()) {
    return;
  }
  std::unique_ptr<PageInfoBubbleSpecification> specification =
      PageInfoBubbleSpecification::Builder(views::BubbleAnchor(host_button_),
                                           GetWidget()->GetNativeWindow(),
                                           contents, entry->GetVirtualURL())
          .AddPageInfoClosingCallback(base::DoNothing())
          .Build();
  PageInfoBubbleView::CreatePageInfoBubble(std::move(specification))
      ->GetWidget()
      ->Show();
}

void OriginAddressPill::ShowExtensions() {
  auto* browser_view = BrowserView::GetBrowserViewForBrowser(browser_);
  ExtensionsToolbarDesktop* container =
      browser_view && browser_view->toolbar()
          ? browser_view->toolbar()->extensions_container()
          : nullptr;
  if (!container) {
    return;
  }
  views::View* anchor = extensions_button_->GetVisible()
                            ? extensions_button_.get()
                            : static_cast<views::View*>(menu_button_);
  // The top bar's container still owns the actions and their popups; only
  // the menu opens here, beside the address. The newer menu belongs to the
  // container's own button, so it opens from there.
  if (base::FeatureList::IsEnabled(
          extensions_features::kExtensionsMenuAccessControl)) {
    if (ExtensionsToolbarButton* button = container->GetExtensionsButton()) {
      button->ToggleExtensionsMenu();
    }
    return;
  }
  if (ExtensionsMenuView::IsShowing()) {
    ExtensionsMenuView::Hide();
    return;
  }
  ExtensionsMenuView::ShowBubble(views::BubbleAnchor(anchor), browser_,
                                 container->GetToolbarViewModel(), container);
}

const std::u16string& OriginAddressPill::GetHostTextForTesting() const {
  return host_button_->host();
}

views::Button* OriginAddressPill::host_button_for_testing() {
  return host_button_;
}

views::Button* OriginAddressPill::copy_button_for_testing() {
  return copy_button_;
}

views::Button* OriginAddressPill::extensions_button_for_testing() {
  return extensions_button_;
}

views::Button* OriginAddressPill::menu_button_for_testing() {
  return menu_button_;
}

std::unique_ptr<ui::SimpleMenuModel>
OriginAddressPill::BuildMenuModelForTesting() {
  return BuildMenuModel();
}

gfx::Size OriginAddressPill::CalculatePreferredSize(
    const views::SizeBounds& available_size) const {
  return gfx::Size(0, kPillHeight);
}

void OriginAddressPill::Layout(PassKey) {
  const gfx::Rect bounds = GetLocalBounds();
  const int center_y = bounds.CenterPoint().y();
  auto place = [&](views::View* view, int right) {
    const gfx::Size size = view->GetPreferredSize();
    view->SetBounds(right - size.width(), center_y - size.height() / 2,
                    size.width(), size.height());
    return right - size.width() - kButtonGap;
  };

  // Right to left: the menu and Shields always show; Copy Link and then
  // Extensions give way before the host is cut short, up to a comfortable
  // width. Both stay in the menu, as in Arc's narrow pill.
  const int host_width =
      std::min(host_button_->GetPreferredSize().width(), kComfortableHostWidth);
  int right = bounds.right() - kTrailingInset;
  right = place(menu_button_, right);
  right = place(shields_button_, right);
  const int copy_width = kButtonSize + kButtonGap;
  const bool show_copy = right - copy_width >= host_width;
  copy_button_->SetVisible(show_copy);
  if (show_copy) {
    right = place(copy_button_, right);
  }
  const bool show_extensions = show_copy && right - copy_width >= host_width;
  extensions_button_->SetVisible(show_extensions);
  if (show_extensions) {
    right = place(extensions_button_, right);
  }
  host_button_->SetBounds(0, 0, std::max(0, right), bounds.height());
}

void OriginAddressPill::OnThemeChanged() {
  views::View::OnThemeChanged();
  UpdateColors();
}

void OriginAddressPill::OnPaintBackground(gfx::Canvas* canvas) {
  const bool dark = origin_chrome_style::IsDark(this);
  const bool hovered =
      host_button_->GetState() == views::Button::STATE_HOVERED ||
      host_button_->GetState() == views::Button::STATE_PRESSED;
  cc::PaintFlags fill;
  fill.setAntiAlias(true);
  fill.setColor(hovered ? origin_chrome_style::SelectedFill(dark)
                        : origin_chrome_style::HoverFill(dark));
  canvas->DrawRoundRect(gfx::RectF(GetLocalBounds()), kPillRadius, fill);
}

void OriginAddressPill::UpdateColors() {
  const bool dark = origin_chrome_style::IsDark(this);
  SetIconButtonImage(extensions_button_, kLeoBrowserExtensionsIcon, dark);
  SetIconButtonImage(copy_button_, kLeoLinkNormalIcon, dark);
  SetIconButtonImage(menu_button_, kLeoMoreVerticalIcon, dark);
  host_button_->UpdateColors();
  SchedulePaint();
}

std::unique_ptr<ui::SimpleMenuModel> OriginAddressPill::BuildCopyMenuModel() {
  auto model = std::make_unique<ui::SimpleMenuModel>(this);
  model->AddItem(kCopyLink, u"Copy Link");
  model->AddItem(kCopyLinkAsMarkdown, u"Copy Link as Markdown");
  model->AddItem(kCopyLinkToHighlight, u"Copy Link to Highlight");
  return model;
}

std::unique_ptr<ui::SimpleMenuModel> OriginAddressPill::BuildMenuModel() {
  auto model = BuildCopyMenuModel();
  model->AddSeparator(ui::NORMAL_SEPARATOR);
  model->AddItem(kSiteSettings, u"Site Settings…");
  model->AddItem(kExtensions, u"Extensions…");
  model->AddSeparator(ui::NORMAL_SEPARATOR);
  TabStripModel* tabs = browser_->tab_strip_model();
  const int active = tabs->active_index();
  if (active == TabStripModel::kNoTab || !tabs->IsTabPinned(active)) {
    model->AddItem(kPinPage, u"Pin Page");
    return model;
  }
  model->AddItem(kBackToPinnedAddress, u"Back to Pinned Address");
  model->AddItem(kReplacePinnedAddress, u"Make This the Pinned Address");
  model->AddItem(kEditPinnedAddress, u"Edit Pinned Address…");
  model->AddSeparator(ui::NORMAL_SEPARATOR);
  model->AddItem(kSeparateFromPin, u"Separate From Pinned Page");
  model->AddItem(kUnloadKeepPinned, u"Unload and Keep Pinned");
  return model;
}

void OriginAddressPill::RunMenu(std::unique_ptr<ui::SimpleMenuModel> model,
                                views::View* anchor,
                                ui::mojom::MenuSourceType source_type) {
  if (!GetWidget()) {
    return;
  }
  // The runner refers to the model, so it goes first.
  menu_runner_.reset();
  menu_model_ = std::move(model);
  menu_runner_ = std::make_unique<views::MenuRunner>(
      menu_model_.get(), views::MenuRunner::HAS_MNEMONICS);
  menu_runner_->RunMenuAt(GetWidget(), nullptr,
                          anchor->GetAnchorBoundsInScreen(),
                          views::MenuAnchorPosition::kTopRight, source_type);
}

void OriginAddressPill::OnMenuButtonPressed() {
  RunMenu(BuildMenuModel(), menu_button_, ui::mojom::MenuSourceType::kMouse);
}

void OriginAddressPill::ShowContextMenuForViewImpl(
    views::View* source,
    const gfx::Point& point,
    ui::mojom::MenuSourceType source_type) {
  RunMenu(BuildCopyMenuModel(), source, source_type);
}

bool OriginAddressPill::IsCommandIdEnabled(int command_id) const {
  switch (command_id) {
    case kSiteSettings:
      return has_page_;
    case kExtensions:
      return true;
    default:
      return callbacks_.is_command_enabled.Run(
          static_cast<Command>(command_id));
  }
}

bool OriginAddressPill::GetAcceleratorForCommandId(
    int command_id,
    ui::Accelerator* accelerator) const {
  // The navigation-mode keys, shown so the menu teaches them.
  switch (command_id) {
    case kCopyLink:
      *accelerator = ui::Accelerator(ui::VKEY_Y, ui::EF_NONE);
      return true;
    case kCopyLinkAsMarkdown:
      *accelerator = ui::Accelerator(ui::VKEY_Y, ui::EF_SHIFT_DOWN);
      return true;
    case kCopyLinkToHighlight:
      *accelerator = ui::Accelerator(ui::VKEY_Y, ui::EF_ALT_DOWN);
      return true;
    case kPinPage:
      *accelerator = ui::Accelerator(ui::VKEY_P, ui::EF_NONE);
      return true;
    case kBackToPinnedAddress:
      *accelerator = ui::Accelerator(ui::VKEY_H, ui::EF_NONE);
      return true;
    case kReplacePinnedAddress:
      *accelerator = ui::Accelerator(ui::VKEY_H, ui::EF_SHIFT_DOWN);
      return true;
    case kEditPinnedAddress:
      *accelerator = ui::Accelerator(ui::VKEY_P, ui::EF_SHIFT_DOWN);
      return true;
    case kSeparateFromPin:
      *accelerator = ui::Accelerator(ui::VKEY_X, ui::EF_NONE);
      return true;
    case kUnloadKeepPinned:
      *accelerator = ui::Accelerator(ui::VKEY_U, ui::EF_NONE);
      return true;
    default:
      return false;
  }
}

void OriginAddressPill::ExecuteCommand(int command_id, int event_flags) {
  switch (command_id) {
    case kSiteSettings:
      ShowSiteSettings();
      return;
    case kExtensions:
      ShowExtensions();
      return;
    default:
      callbacks_.run_command.Run(static_cast<Command>(command_id));
      return;
  }
}

BEGIN_METADATA(OriginAddressPill)
END_METADATA
