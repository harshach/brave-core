// Copyright (c) 2026 The Brave Authors. All rights reserved.
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this file,
// You can obtain one at https://mozilla.org/MPL/2.0/.

#include "brave/browser/ui/views/frame/origin_temporary_link_view.h"

#include <algorithm>
#include <array>
#include <memory>
#include <optional>
#include <string_view>
#include <utility>

#include "base/check.h"
#include "base/functional/bind.h"
#include "base/strings/strcat.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/utf_string_conversions.h"
#include "base/task/single_thread_task_runner.h"
#include "brave/browser/ui/startup/origin_external_link_router.h"
#include "brave/browser/workspaces/workspace_metadata.h"
#include "brave/browser/workspaces/workspace_service.h"
#include "brave/browser/workspaces/workspace_service_factory.h"
#include "brave/components/vector_icons/vector_icons.h"
#include "brave/grit/brave_generated_resources.h"
#include "build/build_config.h"
#include "chrome/browser/favicon/favicon_utils.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/color/chrome_color_id.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "content/public/browser/web_contents.h"
#include "ui/accessibility/ax_enums.mojom.h"
#include "ui/base/l10n/l10n_util.h"
#include "ui/base/metadata/metadata_impl_macros.h"
#include "ui/base/models/image_model.h"
#include "ui/compositor/layer.h"
#include "ui/gfx/color_utils.h"
#include "ui/gfx/font_list.h"
#include "ui/gfx/geometry/insets.h"
#include "ui/gfx/geometry/size.h"
#include "ui/gfx/image/image.h"
#include "ui/gfx/text_constants.h"
#include "ui/views/accessibility/view_accessibility.h"
#include "ui/views/background.h"
#include "ui/views/border.h"
#include "ui/views/bubble/bubble_border.h"
#include "ui/views/bubble/bubble_dialog_delegate_view.h"
#include "ui/views/bubble/bubble_frame_view.h"
#include "ui/views/controls/button/image_button.h"
#include "ui/views/controls/button/image_button_factory.h"
#include "ui/views/controls/button/label_button.h"
#include "ui/views/controls/button/toggle_button.h"
#include "ui/views/controls/image_view.h"
#include "ui/views/controls/label.h"
#include "ui/views/layout/box_layout.h"
#include "ui/views/view_class_properties.h"
#include "ui/views/widget/widget.h"
#include "url/gurl.h"

namespace {

constexpr std::array<SkColor, 5> kSpaceAccentColors = {
    SkColorSetRGB(0x4F, 0x8F, 0xFF), SkColorSetRGB(0x43, 0xC5, 0x85),
    SkColorSetRGB(0xF5, 0xA3, 0x42), SkColorSetRGB(0xEF, 0x68, 0x68),
    SkColorSetRGB(0x9B, 0x7B, 0xFF),
};

struct OriginPalette {
  SkColor bar;
  SkColor picker;
  SkColor primary;
  SkColor secondary;
  SkColor muted;
  SkColor chip;
  SkColor hover;
  SkColor divider;
  SkColor keep;
  SkColor keep_hover;
  SkColor keep_text;
};

OriginPalette GetOriginPalette(bool dark) {
  if (dark) {
    return {
        .bar = SkColorSetRGB(0x17, 0x19, 0x1E),
        .picker = SkColorSetRGB(0x22, 0x25, 0x2B),
        .primary = SkColorSetRGB(0xF5, 0xF5, 0xF6),
        .secondary = SkColorSetRGB(0x94, 0x96, 0x9C),
        .muted = SkColorSetRGB(0x6B, 0x6E, 0x75),
        .chip = SkColorSetRGB(0x21, 0x23, 0x27),
        .hover = SkColorSetRGB(0x24, 0x26, 0x29),
        .divider = SkColorSetRGB(0x26, 0x28, 0x2C),
        .keep = SkColorSetARGB(0x2E, 0x17, 0xB2, 0x6A),
        .keep_hover = SkColorSetARGB(0x47, 0x17, 0xB2, 0x6A),
        .keep_text = SkColorSetRGB(0x75, 0xE0, 0xA7),
    };
  }
  return {
      .bar = SK_ColorWHITE,
      .picker = SK_ColorWHITE,
      .primary = SkColorSetRGB(0x18, 0x1D, 0x27),
      .secondary = SkColorSetRGB(0x53, 0x58, 0x62),
      .muted = SkColorSetRGB(0x71, 0x76, 0x80),
      .chip = SkColorSetRGB(0xF5, 0xF5, 0xF5),
      .hover = SkColorSetRGB(0xF5, 0xF5, 0xF5),
      .divider = SkColorSetRGB(0xE9, 0xEA, 0xEB),
      .keep = SkColorSetARGB(0x20, 0x06, 0x76, 0x47),
      .keep_hover = SkColorSetARGB(0x32, 0x06, 0x76, 0x47),
      .keep_text = SkColorSetRGB(0x06, 0x76, 0x47),
  };
}

OriginPalette GetOriginPalette(const views::View* view) {
  const bool dark =
      !view->GetColorProvider() ||
      color_utils::IsDark(view->GetColorProvider()->GetColor(kColorToolbar));
  return GetOriginPalette(dark);
}

gfx::FontList OriginFont(int size, gfx::Font::Weight weight) {
#if BUILDFLAG(IS_MAC)
  constexpr char kFamily[] = "SF Pro Text";
#elif BUILDFLAG(IS_WIN)
  constexpr char kFamily[] = "Segoe UI Variable";
#else
  constexpr char kFamily[] = "Inter";
#endif
  return gfx::FontList({kFamily}, gfx::Font::NORMAL, size, weight);
}

const gfx::VectorIcon& GetSpaceIcon(std::string_view icon) {
  if (icon == kOriginSpaceIconHome) {
    return kLeoContainerPersonalIcon;
  }
  if (icon == kOriginSpaceIconWork) {
    return kLeoContainerWorkIcon;
  }
  if (icon == kOriginSpaceIconPlayground) {
    return kLeoRocketIcon;
  }
  if (icon == kOriginSpaceIconReading) {
    return kLeoReadingListIcon;
  }
  if (icon == kOriginSpaceIconTerminal) {
    return kLeoCodeIcon;
  }
  if (icon == kOriginSpaceIconMessages) {
    return kLeoContainerMessagingIcon;
  }
  if (icon == kOriginSpaceIconSchool) {
    return kLeoContainerSchoolIcon;
  }
  if (icon == kOriginSpaceIconShopping) {
    return kLeoContainerShoppingIcon;
  }
  if (icon == kOriginSpaceIconTravel) {
    return kLeoContainerTravelIcon;
  }
  return kLeoSpacesIcon;
}

std::unique_ptr<views::ImageButton> CreateHeaderIconButton(
    views::Button::PressedCallback callback,
    const gfx::VectorIcon& icon,
    const std::u16string& accessible_name) {
  auto button = views::CreateVectorImageButton(std::move(callback));
  const OriginPalette palette = GetOriginPalette(true);
  button->SetImageModel(
      views::Button::STATE_NORMAL,
      ui::ImageModel::FromVectorIcon(icon, palette.secondary, 16));
  button->SetImageModel(
      views::Button::STATE_HOVERED,
      ui::ImageModel::FromVectorIcon(icon, palette.primary, 16));
  button->SetImageModel(
      views::Button::STATE_PRESSED,
      ui::ImageModel::FromVectorIcon(icon, palette.primary, 16));
  button->SetPreferredSize(gfx::Size(30, 30));
  button->SetAccessibleName(accessible_name);
  button->SetTooltipText(accessible_name);
  button->SetFocusBehavior(views::View::FocusBehavior::ACCESSIBLE_ONLY);
  return button;
}

void StyleHeaderIconButton(views::ImageButton* button,
                           const gfx::VectorIcon& icon,
                           const OriginPalette& palette) {
  button->SetImageModel(
      views::Button::STATE_NORMAL,
      ui::ImageModel::FromVectorIcon(icon, palette.secondary, 16));
  button->SetImageModel(
      views::Button::STATE_HOVERED,
      ui::ImageModel::FromVectorIcon(icon, palette.primary, 16));
  button->SetImageModel(
      views::Button::STATE_PRESSED,
      ui::ImageModel::FromVectorIcon(icon, palette.primary, 16));
}

class OriginHeaderButton : public views::LabelButton {
  METADATA_HEADER(OriginHeaderButton, views::LabelButton)

 public:
  enum class Style { kSpace, kKeep };

  OriginHeaderButton(PressedCallback callback, std::u16string text, Style style)
      : LabelButton(std::move(callback), std::move(text)), style_(style) {
    SetBorder(views::CreateEmptyBorder(gfx::Insets::VH(5, 10)));
    SetFocusBehavior(FocusBehavior::ACCESSIBLE_ONLY);
    label()->SetFontList(OriginFont(13, gfx::Font::Weight::MEDIUM));
    label()->SetSubpixelRenderingEnabled(false);
    RefreshStyle();
  }

 protected:
  void StateChanged(ButtonState old_state) override {
    LabelButton::StateChanged(old_state);
    RefreshStyle();
  }

  void OnThemeChanged() override {
    LabelButton::OnThemeChanged();
    RefreshStyle();
  }

 private:
  void RefreshStyle() {
    const OriginPalette palette = GetOriginPalette(this);
    const bool hovered =
        GetState() == STATE_HOVERED || GetState() == STATE_PRESSED;
    const bool keep = style_ == Style::kKeep;
    const SkColor foreground = keep ? palette.keep_text : palette.primary;
    const SkColor background =
        keep ? (hovered ? palette.keep_hover : palette.keep)
             : (hovered ? palette.hover : palette.chip);
    SetBackground(views::CreateRoundedRectBackground(background, 7));
    SetEnabledTextColors(foreground);
  }

  const Style style_;
};

BEGIN_METADATA(OriginHeaderButton)
END_METADATA

class OriginSpacePickerRow : public views::Button {
  METADATA_HEADER(OriginSpacePickerRow, views::Button)

 public:
  OriginSpacePickerRow(PressedCallback callback,
                       const OriginSpaceMetadata& space,
                       size_t index,
                       bool selected,
                       OriginPalette palette)
      : Button(std::move(callback)), selected_(selected), palette_(palette) {
    SetPreferredSize(gfx::Size(1, 36));
    SetBorder(views::CreateEmptyBorder(gfx::Insets::VH(8, 10)));
    SetFocusBehavior(FocusBehavior::ALWAYS);
    auto* layout = SetLayoutManager(std::make_unique<views::BoxLayout>(
        views::BoxLayout::Orientation::kHorizontal, gfx::Insets(), 9));
    layout->set_cross_axis_alignment(
        views::BoxLayout::CrossAxisAlignment::kCenter);

    auto* icon = AddChildView(std::make_unique<views::ImageView>());
    icon->SetImage(ui::ImageModel::FromVectorIcon(
        GetSpaceIcon(space.icon), kSpaceAccentColors[index % 5], 16));
    icon->SetPreferredSize(gfx::Size(18, 18));

    name_ = AddChildView(
        std::make_unique<views::Label>(base::UTF8ToUTF16(space.name)));
    name_->SetHorizontalAlignment(gfx::ALIGN_LEFT);
    name_->SetFontList(OriginFont(13, gfx::Font::Weight::MEDIUM));
    name_->SetSubpixelRenderingEnabled(false);
    layout->SetFlexForView(name_, 1);

    if (selected_) {
      auto* default_label = AddChildView(std::make_unique<views::Label>(
          l10n_util::GetStringUTF16(IDS_ORIGIN_TEMPORARY_LINK_DEFAULT_SPACE)));
      default_label->SetEnabledColor(palette_.secondary);
      default_label->SetFontList(OriginFont(11, gfx::Font::Weight::NORMAL));
      default_label->SetSubpixelRenderingEnabled(false);
    }

    key_ = AddChildView(
        std::make_unique<views::Label>(base::NumberToString16(index + 1)));
    key_->SetVisible(index < 5);
    key_->SetFontList(OriginFont(11, gfx::Font::Weight::MEDIUM));
    key_->SetBorder(views::CreateEmptyBorder(gfx::Insets::VH(2, 6)));
    SetAccessibleName(base::UTF8ToUTF16(space.name));
    RefreshStyle();
  }

 protected:
  void StateChanged(ButtonState old_state) override {
    Button::StateChanged(old_state);
    RefreshStyle();
  }

 private:
  void RefreshStyle() {
    const bool hovered =
        GetState() == STATE_HOVERED || GetState() == STATE_PRESSED;
    SetBackground(views::CreateRoundedRectBackground(
        hovered || selected_ ? palette_.hover : SK_ColorTRANSPARENT, 8));
    name_->SetEnabledColor(palette_.primary);
    key_->SetEnabledColor(palette_.muted);
    key_->SetBackground(views::CreateRoundedRectBackground(palette_.chip, 4));
  }

  const bool selected_;
  const OriginPalette palette_;
  raw_ptr<views::Label> name_ = nullptr;
  raw_ptr<views::Label> key_ = nullptr;
};

BEGIN_METADATA(OriginSpacePickerRow)
END_METADATA

class OriginRememberRow : public views::Button {
  METADATA_HEADER(OriginRememberRow, views::Button)

 public:
  OriginRememberRow(PressedCallback callback,
                    const std::u16string& text,
                    bool enabled,
                    OriginPalette palette)
      : Button(std::move(callback)), palette_(palette) {
    SetFocusBehavior(FocusBehavior::ALWAYS);
    SetBorder(views::CreateEmptyBorder(gfx::Insets::TLBR(10, 14, 10, 14)));
    SetBackground(views::CreateSolidBackground(palette_.chip));
    auto* layout = SetLayoutManager(std::make_unique<views::BoxLayout>(
        views::BoxLayout::Orientation::kHorizontal, gfx::Insets(), 10));
    layout->set_cross_axis_alignment(
        views::BoxLayout::CrossAxisAlignment::kCenter);

    auto* label = AddChildView(std::make_unique<views::Label>(text));
    label->SetEnabledColor(palette_.secondary);
    label->SetFontList(OriginFont(12, gfx::Font::Weight::NORMAL));
    label->SetHorizontalAlignment(gfx::ALIGN_LEFT);
    label->SetSubpixelRenderingEnabled(false);
    layout->SetFlexForView(label, 1);

    toggle_ = AddChildView(std::make_unique<views::ToggleButton>());
    toggle_->SetAcceptsEvents(false);
    toggle_->SetPreferredSize(gfx::Size(34, 20));
    toggle_->SetInnerBorderEnabled(false);
    toggle_->SetThumbOnColor(SK_ColorWHITE);
    toggle_->SetTrackOnColor(SkColorSetRGB(0x15, 0x70, 0xEF));
    toggle_->SetThumbOffColor(palette_.muted);
    toggle_->SetTrackOffColor(palette_.divider);
    GetViewAccessibility().SetRole(ax::mojom::Role::kSwitch);
    SetIsOn(enabled);
    SetAccessibleName(text);
  }

  void SetIsOn(bool enabled) {
    toggle_->SetIsOn(enabled);
    GetViewAccessibility().SetCheckedState(
        enabled ? ax::mojom::CheckedState::kTrue
                : ax::mojom::CheckedState::kFalse);
  }

 private:
  const OriginPalette palette_;
  raw_ptr<views::ToggleButton> toggle_ = nullptr;
};

BEGIN_METADATA(OriginRememberRow)
END_METADATA

class OriginTemporarySpacePicker : public views::BubbleDialogDelegate {
 public:
  OriginTemporarySpacePicker(
      views::View* anchor,
      base::WeakPtr<OriginTemporaryLinkView> temporary_view,
      WorkspaceService* workspace_service,
      const std::string& domain)
      : BubbleDialogDelegate(anchor, views::BubbleBorder::TOP_LEFT),
        temporary_view_(std::move(temporary_view)) {
    const OriginPalette palette = GetOriginPalette(anchor);
    SetButtons(static_cast<int>(ui::mojom::DialogButton::kNone));
    set_margins(gfx::Insets::VH(6, 6));
    set_fixed_width(300);
    set_corner_radius(14);
    SetBackgroundColor(palette.picker);

    auto contents = std::make_unique<views::View>();
    contents->SetBackground(views::CreateSolidBackground(palette.picker));
    auto* layout =
        contents->SetLayoutManager(std::make_unique<views::BoxLayout>(
            views::BoxLayout::Orientation::kVertical, gfx::Insets(), 2));
    layout->set_cross_axis_alignment(
        views::BoxLayout::CrossAxisAlignment::kStretch);

    const auto& spaces = workspace_service->GetOriginSpaces();
    for (size_t index = 0; index < spaces.size(); ++index) {
      const auto& space = spaces[index];
      contents->AddChildView(std::make_unique<OriginSpacePickerRow>(
          base::BindRepeating(&OriginTemporarySpacePicker::ChooseSpace,
                              base::Unretained(this), space.id),
          space, index,
          temporary_view_ && temporary_view_->selected_space_id() == space.id,
          palette));
    }

    if (!domain.empty()) {
      auto* divider = contents->AddChildView(std::make_unique<views::View>());
      divider->SetPreferredSize(gfx::Size(1, 1));
      divider->SetBackground(views::CreateSolidBackground(palette.divider));
      divider->SetProperty(views::kMarginsKey, gfx::Insets::TLBR(7, 4, 7, 4));

      remember_row_ =
          contents->AddChildView(std::make_unique<OriginRememberRow>(
              base::BindRepeating(&OriginTemporarySpacePicker::ToggleAlwaysOpen,
                                  base::Unretained(this)),
              l10n_util::GetStringFUTF16(
                  IDS_ORIGIN_TEMPORARY_LINK_ALWAYS_OPEN_DOMAIN_HERE,
                  base::UTF8ToUTF16(domain)),
              temporary_view_ && temporary_view_->always_open_domain_here(),
              palette));
    }
    SetContentsView(std::move(contents));
  }

 private:
  void ChooseSpace(std::string space_id) {
    if (temporary_view_) {
      temporary_view_->SelectSpace(std::move(space_id));
    }
    GetWidget()->Close();
  }

  void ToggleAlwaysOpen(const ui::Event&) {
    if (temporary_view_) {
      const bool enabled = !temporary_view_->always_open_domain_here();
      temporary_view_->SetAlwaysOpenDomainHere(enabled);
      remember_row_->SetIsOn(enabled);
    }
  }

  base::WeakPtr<OriginTemporaryLinkView> temporary_view_;
  raw_ptr<OriginRememberRow> remember_row_ = nullptr;
};

}  // namespace

BEGIN_METADATA(OriginTemporaryLinkView)
END_METADATA

OriginTemporaryLinkView::OriginTemporaryLinkView(Browser* browser)
    : browser_(browser) {
  CHECK(browser_);
  SetBackground(views::CreateSolidBackground(GetOriginPalette(true).bar));
  SetPreferredSize(gfx::Size(1, kBarHeight));
  SetPaintToLayer();
  layer()->SetFillsBoundsOpaquely(true);
  auto* layout = SetLayoutManager(std::make_unique<views::BoxLayout>(
      views::BoxLayout::Orientation::kHorizontal,
      gfx::Insets::TLBR(6, 8, 6, 8)));
  layout->set_cross_axis_alignment(
      views::BoxLayout::CrossAxisAlignment::kCenter);

  leading_container_ = AddChildView(std::make_unique<views::View>());
  auto* leading_layout =
      leading_container_->SetLayoutManager(std::make_unique<views::BoxLayout>(
          views::BoxLayout::Orientation::kHorizontal, gfx::Insets(), 7));
  leading_layout->set_cross_axis_alignment(
      views::BoxLayout::CrossAxisAlignment::kCenter);
  layout->SetFlexForView(leading_container_, 1);

  incoming_link_icon_ =
      leading_container_->AddChildView(std::make_unique<views::ImageView>());
  incoming_link_icon_->SetPreferredSize(gfx::Size(24, 24));
  incoming_link_icon_->SetImageSize(gfx::Size(15, 15));

  open_in_label_ =
      leading_container_->AddChildView(std::make_unique<views::Label>(
          l10n_util::GetStringUTF16(IDS_ORIGIN_TEMPORARY_LINK_OPEN_IN)));
  open_in_label_->SetFontList(OriginFont(13, gfx::Font::Weight::NORMAL));
  open_in_label_->SetSubpixelRenderingEnabled(false);

  space_button_ =
      leading_container_->AddChildView(std::make_unique<OriginHeaderButton>(
          base::BindRepeating(&OriginTemporaryLinkView::ShowSpacePicker,
                              weak_factory_.GetWeakPtr()),
          std::u16string(), OriginHeaderButton::Style::kSpace));

  auto* center = AddChildView(std::make_unique<views::View>());
  auto* center_layout =
      center->SetLayoutManager(std::make_unique<views::BoxLayout>(
          views::BoxLayout::Orientation::kHorizontal, gfx::Insets(), 7));
  center_layout->set_cross_axis_alignment(
      views::BoxLayout::CrossAxisAlignment::kCenter);

  favicon_view_ = center->AddChildView(std::make_unique<views::ImageView>());
  favicon_view_->SetPreferredSize(gfx::Size(18, 18));
  favicon_view_->SetImageSize(gfx::Size(16, 16));

  title_label_ = center->AddChildView(std::make_unique<views::Label>());
  title_label_->SetFontList(OriginFont(12, gfx::Font::Weight::MEDIUM));
  title_label_->SetElideBehavior(gfx::ELIDE_TAIL);
  title_label_->SetMaximumWidth(360);
  title_label_->SetSubpixelRenderingEnabled(false);

  count_label_ = center->AddChildView(std::make_unique<views::Label>());
  count_label_->SetFontList(OriginFont(11, gfx::Font::Weight::MEDIUM));
  count_label_->SetBorder(views::CreateEmptyBorder(gfx::Insets::VH(2, 6)));

  trailing_container_ = AddChildView(std::make_unique<views::View>());
  auto* trailing_layout =
      trailing_container_->SetLayoutManager(std::make_unique<views::BoxLayout>(
          views::BoxLayout::Orientation::kHorizontal, gfx::Insets(), 7));
  trailing_layout->set_main_axis_alignment(
      views::BoxLayout::MainAxisAlignment::kEnd);
  trailing_layout->set_cross_axis_alignment(
      views::BoxLayout::CrossAxisAlignment::kCenter);
  layout->SetFlexForView(trailing_container_, 1);

  trailing_container_->AddChildView(std::make_unique<OriginHeaderButton>(
      base::BindRepeating(&OriginTemporaryLinkView::KeepInSelectedSpace,
                          weak_factory_.GetWeakPtr()),
      l10n_util::GetStringUTF16(IDS_ORIGIN_TEMPORARY_LINK_KEEP),
      OriginHeaderButton::Style::kKeep));
  discard_button_ = trailing_container_->AddChildView(CreateHeaderIconButton(
      base::BindRepeating(&OriginTemporaryLinkView::Discard,
                          weak_factory_.GetWeakPtr()),
      kLeoCloseIcon,
      l10n_util::GetStringUTF16(IDS_ORIGIN_TEMPORARY_LINK_DISCARD)));
  RefreshTheme();
  Update();
}

OriginTemporaryLinkView::~OriginTemporaryLinkView() = default;

void OriginTemporaryLinkView::OnThemeChanged() {
  views::View::OnThemeChanged();
  RefreshTheme();
}

void OriginTemporaryLinkView::RefreshTheme() {
  const OriginPalette palette = GetOriginPalette(this);
  SetBackground(views::CreateSolidBackground(palette.bar));
  incoming_link_icon_->SetImage(ui::ImageModel::FromVectorIcon(
      kOriginIncomingLinkIcon, palette.muted, 15));
  StyleHeaderIconButton(discard_button_, kLeoCloseIcon, palette);
  open_in_label_->SetEnabledColor(palette.secondary);
  title_label_->SetEnabledColor(palette.secondary);
  count_label_->SetEnabledColor(palette.muted);
  count_label_->SetBackground(
      views::CreateRoundedRectBackground(palette.chip, 7));
}

void OriginTemporaryLinkView::Update() {
  content::WebContents* contents =
      browser_->tab_strip_model()->GetActiveWebContents();
  if (!contents) {
    return;
  }
  const std::string domain =
      WorkspaceService::GetOriginDomainKey(contents->GetVisibleURL());
  if (selected_space_id_.empty() || domain != selected_domain_) {
    selected_space_id_ = origin_external_link::GetSuggestedSpaceId(
        browser_, contents->GetVisibleURL());
    selected_domain_ = domain;
    always_open_domain_here_ = false;
  }
  RefreshSpaceChip();

  const gfx::Image favicon = favicon::TabFaviconFromWebContents(contents);
  favicon_view_->SetImage(favicon.IsEmpty()
                              ? favicon::GetDefaultFaviconModel()
                              : ui::ImageModel::FromImage(favicon));
  std::u16string title = contents->GetTitle();
  if (title.empty()) {
    title = base::UTF8ToUTF16(contents->GetVisibleURL().host());
  }
  title_label_->SetText(std::move(title));

  const int count = browser_->tab_strip_model()->count();
  count_label_->SetText(base::NumberToString16(count));
  count_label_->SetVisible(count > 1);
}

void OriginTemporaryLinkView::KeepSuggested() {
  KeepInSelectedSpace();
}

void OriginTemporaryLinkView::KeepInSplit() {
  if (selected_space_id_.empty()) {
    return;
  }
  origin_external_link::KeepActivePage(
      browser_, selected_space_id_, always_open_domain_here_,
      origin_external_link::KeepDisposition::kSplit);
}

void OriginTemporaryLinkView::ReplaceCurrentPage() {
  if (selected_space_id_.empty()) {
    return;
  }
  origin_external_link::KeepActivePage(
      browser_, selected_space_id_, always_open_domain_here_,
      origin_external_link::KeepDisposition::kReplace);
}

bool OriginTemporaryLinkView::KeepInSpaceAtIndex(size_t index) {
  WorkspaceService* service =
      WorkspaceServiceFactory::GetForProfile(browser_->GetProfile());
  if (!service || index >= service->GetOriginSpaces().size()) {
    return false;
  }
  selected_space_id_ = service->GetOriginSpaces()[index].id;
  KeepInSelectedSpace();
  return true;
}

void OriginTemporaryLinkView::Discard() {
  origin_external_link::DiscardActivePage(browser_);
}

void OriginTemporaryLinkView::SelectSpace(std::string space_id) {
  selected_space_id_ = std::move(space_id);
  RefreshSpaceChip();
}

void OriginTemporaryLinkView::SetAlwaysOpenDomainHere(bool enabled) {
  always_open_domain_here_ = enabled;
}

base::WeakPtr<OriginTemporaryLinkView> OriginTemporaryLinkView::GetWeakPtr() {
  return weak_factory_.GetWeakPtr();
}

void OriginTemporaryLinkView::ShowSpacePicker() {
  if (space_picker_widget_) {
    return;
  }
  WorkspaceService* service =
      WorkspaceServiceFactory::GetForProfile(browser_->GetProfile());
  if (!service) {
    return;
  }
  content::WebContents* contents =
      browser_->tab_strip_model()->GetActiveWebContents();
  const std::string domain =
      contents ? WorkspaceService::GetOriginDomainKey(contents->GetVisibleURL())
               : std::string();
  space_picker_delegate_ = std::make_unique<OriginTemporarySpacePicker>(
      space_button_, GetWeakPtr(), service, domain);
  space_picker_widget_ = views::BubbleDialogDelegate::CreateBubble(
      space_picker_delegate_.get(),
      base::BindOnce(
          [](base::WeakPtr<OriginTemporaryLinkView> view,
             views::Widget::ClosedReason) {
            if (view) {
              view->OnSpacePickerClosed();
            }
          },
          GetWeakPtr()));
  auto* frame = space_picker_delegate_->GetBubbleFrameView();
  frame->SetRoundedCorners(gfx::RoundedCornersF(14));
  frame->SetDisplayVisibleArrow(false);
  frame->bubble_border()->set_draw_border_stroke(true);
  space_picker_widget_->Show();
}

void OriginTemporaryLinkView::OnSpacePickerClosed() {
  auto task_runner = base::SingleThreadTaskRunner::GetCurrentDefault();
  task_runner->DeleteSoon(FROM_HERE, space_picker_widget_.release());
  task_runner->DeleteSoon(FROM_HERE, space_picker_delegate_.release());
}

void OriginTemporaryLinkView::KeepInSelectedSpace() {
  if (selected_space_id_.empty()) {
    return;
  }
  origin_external_link::KeepActivePage(browser_, selected_space_id_,
                                       always_open_domain_here_);
}

void OriginTemporaryLinkView::RefreshSpaceChip() {
  WorkspaceService* service =
      WorkspaceServiceFactory::GetForProfile(browser_->GetProfile());
  const OriginSpaceMetadata* space =
      service ? service->GetOriginSpace(selected_space_id_) : nullptr;
  if (space) {
    size_t index = 0;
    const auto& spaces = service->GetOriginSpaces();
    for (; index < spaces.size() && spaces[index].id != space->id; ++index) {
    }
    space_button_->SetImageModel(
        views::Button::STATE_NORMAL,
        ui::ImageModel::FromVectorIcon(GetSpaceIcon(space->icon),
                                       kSpaceAccentColors[index % 5], 14));
    space_button_->SetImageLabelSpacing(7);
    const std::u16string text =
        base::StrCat({base::UTF8ToUTF16(space->name), u"  ▾"});
    const bool needs_balance = space_button_->GetText() != text;
    space_button_->SetText(text);
    if (needs_balance) {
      BalanceSideContainers();
    }
    return;
  }
  space_button_->SetImageModel(views::Button::STATE_NORMAL, ui::ImageModel());
  const std::u16string text = base::StrCat(
      {l10n_util::GetStringUTF16(IDS_ORIGIN_TEMPORARY_LINK_CHOOSE_SPACE),
       u"  ▾"});
  const bool needs_balance = space_button_->GetText() != text;
  space_button_->SetText(text);
  if (needs_balance) {
    BalanceSideContainers();
  }
}

void OriginTemporaryLinkView::BalanceSideContainers() {
  leading_container_->SetPreferredSize(std::nullopt);
  trailing_container_->SetPreferredSize(std::nullopt);
  const gfx::Size leading_size = leading_container_->GetPreferredSize();
  const gfx::Size trailing_size = trailing_container_->GetPreferredSize();
  const gfx::Size balanced_size(
      std::max(leading_size.width(), trailing_size.width()),
      std::max(leading_size.height(), trailing_size.height()));
  leading_container_->SetPreferredSize(balanced_size);
  trailing_container_->SetPreferredSize(balanced_size);
}
