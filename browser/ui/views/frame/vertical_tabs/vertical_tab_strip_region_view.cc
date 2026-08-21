/* Copyright (c) 2022 The Brave Authors. All rights reserved.
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at https://mozilla.org/MPL/2.0/. */

#include "brave/browser/ui/views/frame/vertical_tabs/vertical_tab_strip_region_view.h"

#include <algorithm>
#include <array>
#include <memory>
#include <optional>
#include <string_view>
#include <utility>
#include <vector>

#include "base/check.h"
#include "base/check_op.h"
#include "base/command_line.h"
#include "base/dcheck_is_on.h"
#include "base/feature_list.h"
#include "base/functional/bind.h"
#include "base/location.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/string_split.h"
#include "base/strings/string_util.h"
#include "base/strings/utf_string_conversions.h"
#include "base/task/sequenced_task_runner.h"
#include "base/time/time.h"
#include "brave/app/vector_icons/vector_icons.h"
#include "brave/browser/ui/color/brave_color_id.h"
#include "brave/browser/ui/focus_mode/focus_mode_controller.h"
#include "brave/browser/ui/focus_mode/focus_mode_utils.h"
#include "brave/browser/ui/tabs/brave_tab_prefs.h"
#include "brave/browser/ui/tabs/public/switches.h"
#include "brave/browser/ui/tabs/public/vertical_tab_controller.h"
#include "brave/browser/ui/views/frame/brave_browser_view.h"
#include "brave/browser/ui/views/frame/brave_non_client_hit_test_helper.h"
#include "brave/browser/ui/views/frame/tab_strip_placement_coordinator.h"
#include "brave/browser/ui/views/tabs/brave_new_tab_button.h"
#include "brave/browser/ui/views/tabs/brave_tab_container.h"
#include "brave/browser/ui/views/tabs/brave_tab_strip_layout_helper.h"
#include "brave/browser/workspaces/workspace_service_factory.h"
#include "brave/components/brave_origin/buildflags/buildflags.h"
#include "brave/components/constants/pref_names.h"
#include "brave/components/vector_icons/vector_icons.h"
#include "cc/paint/paint_flags.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/themes/theme_properties.h"
#include "chrome/browser/themes/theme_service.h"
#include "chrome/browser/themes/theme_service_factory.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/browser_commands.h"
#include "chrome/browser/ui/browser_window/public/browser_window_features.h"
#include "chrome/browser/ui/color/chrome_color_id.h"
#include "chrome/browser/ui/exclusive_access/exclusive_access_manager.h"
#include "chrome/browser/ui/exclusive_access/fullscreen_controller.h"
#include "chrome/browser/ui/frame/window_frame_util.h"
#include "chrome/browser/ui/tabs/features.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/browser/ui/tabs/tab_style.h"
#include "chrome/browser/ui/ui_features.h"
#include "chrome/browser/ui/views/chrome_layout_provider.h"
#include "chrome/browser/ui/views/frame/browser_view.h"
#include "chrome/browser/ui/views/toolbar/toolbar_ink_drop_util.h"
#include "chrome/browser/ui/views/toolbar/toolbar_view.h"
#include "chrome/common/pref_names.h"
#include "chrome/grit/generated_resources.h"
#include "components/prefs/pref_service.h"
#include "components/vector_icons/vector_icons.h"
#include "ui/base/cursor/cursor.h"
#include "ui/base/hit_test.h"
#include "ui/base/l10n/l10n_util.h"
#include "ui/base/metadata/metadata_impl_macros.h"
#include "ui/base/mojom/dialog_button.mojom.h"
#include "ui/compositor/compositor.h"
#include "ui/compositor/layer.h"
#include "ui/display/screen.h"
#include "ui/events/event_observer.h"
#include "ui/events/keycodes/keyboard_codes.h"
#include "ui/gfx/canvas.h"
#include "ui/gfx/color_utils.h"
#include "ui/gfx/paint_vector_icon.h"
#include "ui/gfx/scoped_canvas.h"
#include "ui/views/accessibility/accessibility_paint_checks.h"
#include "ui/views/animation/ink_drop.h"
#include "ui/views/bubble/bubble_border.h"
#include "ui/views/bubble/bubble_dialog_delegate_view.h"
#include "ui/views/bubble/bubble_frame_view.h"
#include "ui/views/controls/button/image_button.h"
#include "ui/views/controls/button/label_button.h"
#include "ui/views/controls/focus_ring.h"
#include "ui/views/controls/highlight_path_generator.h"
#include "ui/views/controls/image_view.h"
#include "ui/views/controls/label.h"
#include "ui/views/controls/resize_area.h"
#include "ui/views/controls/textfield/textfield.h"
#include "ui/views/layout/box_layout.h"
#include "ui/views/layout/fill_layout.h"
#include "ui/views/layout/flex_layout.h"
#include "ui/views/layout/layout_types.h"
#include "ui/views/view_class_properties.h"
#include "ui/views/view_utils.h"
#include "ui/views/window/hit_test_utils.h"

#if BUILDFLAG(IS_WIN)
#include "ui/views/win/hwnd_util.h"
#endif

#if !BUILDFLAG(IS_MAC)
#include "chrome/app/chrome_command_ids.h"
#endif

namespace {

#if !BUILDFLAG(IS_BRAVE_ORIGIN_BRANDED)
constexpr int kSeparatorHeight = 1;
constexpr int kBorderThickness = 1;
#endif

#if BUILDFLAG(IS_BRAVE_ORIGIN_BRANDED)
constexpr int kOriginWorkspaceRailWidth = 56;
constexpr int kOriginSidebarMinimumWidth =
    kOriginWorkspaceRailWidth + 180;
constexpr int kOriginSidebarMaximumWidth =
    kOriginWorkspaceRailWidth + 360;
constexpr int kOriginWorkspaceHeaderHeight = 58;
constexpr int kOriginSearchFieldHeight = 30;
constexpr int kOriginPageListTop = 66;
constexpr int kOriginNewPageRowHeight = 32;
constexpr int kOriginStatusRowHeight = 26;
constexpr int kOriginPageListFooterHeight = 70;
constexpr int kOriginWorkspaceGap = 6;
constexpr SkColor kOriginPickerSurface = SkColorSetRGB(0x1C, 0x1F, 0x25);
constexpr SkColor kOriginPickerField = SkColorSetARGB(0x0D, 0xFF, 0xFF, 0xFF);
constexpr SkColor kOriginPickerSecondaryText = SkColorSetRGB(0x94, 0x96, 0x9C);
constexpr SkColor kOriginActiveAccent = SkColorSetRGB(0xFB, 0x54, 0x2B);

struct OriginWorkspaceIconChoice {
  std::string_view key;
  std::u16string_view name;
  std::string_view keywords;
};

constexpr std::array<OriginWorkspaceIconChoice, 10>
    kOriginWorkspaceIconChoices = {{
        {kOriginSpaceIconHome, u"Home", "home personal house"},
        {kOriginSpaceIconWork, u"Work", "work briefcase business"},
        {kOriginSpaceIconPlayground, u"Playground", "play lab experiment"},
        {kOriginSpaceIconReading, u"Reading", "reading book research"},
        {kOriginSpaceIconTerminal, u"Development", "terminal code developer"},
        {kOriginSpaceIconIdeas, u"Ideas", "ideas project spark"},
        {kOriginSpaceIconMessages, u"Messages", "messages chat social"},
        {kOriginSpaceIconSchool, u"School", "school study learning"},
        {kOriginSpaceIconShopping, u"Shopping", "shopping store bag"},
        {kOriginSpaceIconTravel, u"Travel", "travel trip compass"},
    }};

const gfx::VectorIcon& GetOriginWorkspaceIcon(std::string_view key) {
  if (key == kOriginSpaceIconHome) {
    return kLeoContainerPersonalIcon;
  }
  if (key == kOriginSpaceIconWork) {
    return kLeoContainerWorkIcon;
  }
  if (key == kOriginSpaceIconPlayground) {
    return kLeoRocketIcon;
  }
  if (key == kOriginSpaceIconReading) {
    return kLeoReadingListIcon;
  }
  if (key == kOriginSpaceIconTerminal) {
    return kLeoCodeIcon;
  }
  if (key == kOriginSpaceIconMessages) {
    return kLeoContainerMessagingIcon;
  }
  if (key == kOriginSpaceIconSchool) {
    return kLeoContainerSchoolIcon;
  }
  if (key == kOriginSpaceIconShopping) {
    return kLeoContainerShoppingIcon;
  }
  if (key == kOriginSpaceIconTravel) {
    return kLeoContainerTravelIcon;
  }
  if (key == "add") {
    return kLeoPlusAddIcon;
  }
  if (key == "sync") {
    return kLeoProductSyncIcon;
  }
  if (key == "account") {
    return kLeoUserCircleIcon;
  }
  return kLeoSpacesIcon;
}

gfx::FontList OriginChromeFont(int size, gfx::Font::Weight weight) {
#if BUILDFLAG(IS_MAC)
  constexpr char kFamily[] = "SF Pro Text";
#elif BUILDFLAG(IS_WIN)
  constexpr char kFamily[] = "Segoe UI";
#else
  constexpr char kFamily[] = "Inter";
#endif
  return gfx::FontList({kFamily}, gfx::Font::NORMAL, size, weight);
}

class OriginWorkspaceButton : public views::LabelButton {
  METADATA_HEADER(OriginWorkspaceButton, views::LabelButton)
 public:
  OriginWorkspaceButton(PressedCallback callback,
                        std::string icon,
                        const std::u16string& accessible_name,
                        bool selected)
      : LabelButton(std::move(callback), std::u16string()),
        icon_(std::move(icon)),
        selected_(selected) {
    SetPreferredSize(gfx::Size(36, 36));
    SetBorder(views::CreateEmptyBorder(gfx::Insets()));
    SetHorizontalAlignment(gfx::HorizontalAlignment::ALIGN_CENTER);
    SetAccessibleName(accessible_name);
    SetTooltipText(accessible_name);
    views::InkDrop::Get(this)->SetMode(views::InkDropHost::InkDropMode::OFF);
    UpdateWorkspaceIcon();
  }

  void SetOriginIcon(std::string icon) {
    icon_ = std::move(icon);
    UpdateWorkspaceIcon();
  }

  void SetWorkspaceSelected(bool selected) {
    selected_ = selected;
    UpdateWorkspaceIcon();
    SchedulePaint();
  }

  void StateChanged(ButtonState old_state) override {
    LabelButton::StateChanged(old_state);
    UpdateWorkspaceIcon();
    SchedulePaint();
  }

  void OnThemeChanged() override {
    LabelButton::OnThemeChanged();
    UpdateWorkspaceIcon();
  }

  void OnPaintBackground(gfx::Canvas* canvas) override {
    const bool dark =
        !GetColorProvider() ||
        color_utils::IsDark(GetColorProvider()->GetColor(kColorToolbar));
    const bool highlighted =
        selected_ || GetState() == STATE_HOVERED || GetState() == STATE_PRESSED;
    if (highlighted) {
      cc::PaintFlags fill;
      fill.setAntiAlias(true);
      fill.setStyle(cc::PaintFlags::kFill_Style);
      fill.setColor(selected_ ? (dark ? SkColorSetARGB(0x12, 0xFF, 0xFF, 0xFF)
                                      : SkColorSetRGB(0xF5, 0xF5, 0xF5))
                              : (dark ? SkColorSetARGB(0x0F, 0xFF, 0xFF, 0xFF)
                                      : SkColorSetRGB(0xF5, 0xF5, 0xF5)));
      canvas->DrawRoundRect(gfx::RectF(GetLocalBounds()), 10, fill);
    }
    if (!selected_) {
      return;
    }

    cc::PaintFlags ring;
    ring.setAntiAlias(true);
    ring.setStyle(cc::PaintFlags::kStroke_Style);
    ring.setStrokeWidth(1);
    ring.setColor(dark ? SkColorSetARGB(0x21, 0xFF, 0xFF, 0xFF)
                       : SkColorSetRGB(0xE9, 0xEA, 0xEB));
    gfx::RectF ring_bounds(GetLocalBounds());
    ring_bounds.Inset(0.5f);
    canvas->DrawRoundRect(ring_bounds, 10, ring);

    cc::PaintFlags accent;
    accent.setAntiAlias(true);
    accent.setStyle(cc::PaintFlags::kFill_Style);
    accent.setColor(kOriginActiveAccent);
    canvas->DrawRoundRect(gfx::RectF(6, 11, 3, 14), 2, accent);
  }

 private:
  void UpdateWorkspaceIcon() {
    const bool dark =
        !GetColorProvider() ||
        color_utils::IsDark(GetColorProvider()->GetColor(kColorToolbar));
    const bool highlighted =
        selected_ || GetState() == STATE_HOVERED || GetState() == STATE_PRESSED;
    const SkColor icon_color = highlighted
                                   ? (dark ? SkColorSetRGB(0xF5, 0xF5, 0xF6)
                                           : SkColorSetRGB(0x18, 0x1D, 0x27))
                                   : (dark ? SkColorSetRGB(0x6B, 0x6E, 0x75)
                                           : SkColorSetRGB(0x71, 0x76, 0x80));
    SetImageModel(ButtonState::STATE_NORMAL,
                  ui::ImageModel::FromVectorIcon(GetOriginWorkspaceIcon(icon_),
                                                 icon_color, 18));
  }

  std::string icon_;
  bool selected_;
};

BEGIN_METADATA(OriginWorkspaceButton)
END_METADATA

class OriginWorkspaceTitleButton : public views::LabelButton {
  METADATA_HEADER(OriginWorkspaceTitleButton, views::LabelButton)
 public:
  OriginWorkspaceTitleButton(PressedCallback callback,
                             const std::u16string& text)
      : LabelButton(std::move(callback), text) {
    label()->SetFontList(OriginChromeFont(16, gfx::Font::Weight::SEMIBOLD));
    views::InkDrop::Get(this)->SetMode(views::InkDropHost::InkDropMode::OFF);
  }
};

BEGIN_METADATA(OriginWorkspaceTitleButton)
END_METADATA

class OriginPageListActionButton : public views::Button {
  METADATA_HEADER(OriginPageListActionButton, views::Button)

 public:
  OriginPageListActionButton(PressedCallback callback,
                             const gfx::VectorIcon& icon,
                             std::u16string text,
                             std::u16string key_hint,
                             bool field)
      : Button(std::move(callback)), field_(field) {
    SetPreferredSize(gfx::Size(
        0, field ? kOriginSearchFieldHeight : kOriginNewPageRowHeight));
    SetFocusBehavior(FocusBehavior::ALWAYS);
    SetAccessibleName(text);
    views::InkDrop::Get(this)->SetMode(views::InkDropHost::InkDropMode::OFF);

    auto* layout = SetLayoutManager(std::make_unique<views::BoxLayout>(
        views::BoxLayout::Orientation::kHorizontal,
        gfx::Insets::TLBR(0, 8, 0, 8), 8));
    layout->set_cross_axis_alignment(
        views::BoxLayout::CrossAxisAlignment::kCenter);

    icon_ = AddChildView(std::make_unique<views::ImageView>());
    icon_->SetPreferredSize(gfx::Size(16, 16));
    icon_->SetImageSize(gfx::Size(16, 16));
    vector_icon_ = &icon;

    label_ = AddChildView(std::make_unique<views::Label>(std::move(text)));
    label_->SetHorizontalAlignment(gfx::HorizontalAlignment::ALIGN_LEFT);
    label_->SetElideBehavior(gfx::ELIDE_TAIL);
    label_->SetFontList(OriginChromeFont(13, gfx::Font::Weight::NORMAL));
    layout->SetFlexForView(label_, 1);

    key_hint_ =
        AddChildView(std::make_unique<views::Label>(std::move(key_hint)));
    key_hint_->SetFontList(OriginChromeFont(10, gfx::Font::Weight::SEMIBOLD));
    RefreshStyle();
  }

  void StateChanged(ButtonState old_state) override {
    Button::StateChanged(old_state);
    RefreshStyle();
  }

  void OnThemeChanged() override {
    Button::OnThemeChanged();
    RefreshStyle();
  }

 private:
  void RefreshStyle() {
    const bool dark =
        !GetColorProvider() ||
        color_utils::IsDark(GetColorProvider()->GetColor(kColorToolbar));
    const bool hovered =
        GetState() == STATE_HOVERED || GetState() == STATE_PRESSED;
    const SkColor primary = dark ? SkColorSetRGB(0xF5, 0xF5, 0xF6)
                                 : SkColorSetRGB(0x18, 0x1D, 0x27);
    const SkColor secondary = dark ? SkColorSetRGB(0x94, 0x96, 0x9C)
                                   : SkColorSetRGB(0x53, 0x58, 0x62);
    const SkColor muted = dark ? SkColorSetRGB(0x6B, 0x6E, 0x75)
                               : SkColorSetRGB(0x71, 0x76, 0x80);
    const SkColor background =
        hovered ? (dark ? SkColorSetARGB(0x0F, 0xFF, 0xFF, 0xFF)
                        : SkColorSetRGB(0xF5, 0xF5, 0xF5))
                : (field_ ? (dark ? SkColorSetARGB(0x0D, 0xFF, 0xFF, 0xFF)
                                  : SkColorSetRGB(0xF5, 0xF5, 0xF5))
                          : SK_ColorTRANSPARENT);
    SetBackground(views::CreateRoundedRectBackground(background, 8));
    // The bottom New page affordance should sit quietly in the shell until
    // the user aims at it; Sigma does not give it the same visual weight as a
    // selected page title.
    label_->SetEnabledColor(field_ ? secondary : (hovered ? primary : muted));
    key_hint_->SetEnabledColor(muted);
    icon_->SetImage(ui::ImageModel::FromVectorIcon(
        *vector_icon_, field_ ? secondary : muted, 16));
  }

  const bool field_;
  raw_ptr<const gfx::VectorIcon> vector_icon_ = nullptr;
  raw_ptr<views::ImageView> icon_ = nullptr;
  raw_ptr<views::Label> label_ = nullptr;
  raw_ptr<views::Label> key_hint_ = nullptr;
};

BEGIN_METADATA(OriginPageListActionButton)
END_METADATA

class OriginWorkspaceIconPickerBubble : public views::View,
                                        public views::TextfieldController {
  METADATA_HEADER(OriginWorkspaceIconPickerBubble, views::View)

 public:
  using IconSelectedCallback = base::RepeatingCallback<void(std::string)>;

  static base::WeakPtr<views::Widget> Show(
      views::View* anchor,
      std::string selected_icon,
      IconSelectedCallback icon_selected_callback) {
    auto picker = std::make_unique<OriginWorkspaceIconPickerBubble>(
        anchor, std::move(selected_icon), std::move(icon_selected_callback));
    auto* picker_ptr = picker.get();
    auto bubble_delegate = std::make_unique<views::BubbleDialogDelegate>(
        anchor, views::BubbleBorder::LEFT_TOP,
        views::BubbleBorder::STANDARD_SHADOW, /*autosize=*/true);
    auto* bubble_delegate_ptr = bubble_delegate.get();
    bubble_delegate->SetButtons(
        static_cast<int>(ui::mojom::DialogButton::kNone));
    bubble_delegate->SetShowTitle(false);
    bubble_delegate->SetShowCloseButton(false);
    bubble_delegate->SetAccessibleTitle(u"Choose a Space icon");
    bubble_delegate->set_adjust_if_offscreen(true);
    bubble_delegate->set_close_on_deactivate(true);
    bubble_delegate->set_fixed_width(300);
    bubble_delegate->set_margins(gfx::Insets::TLBR(12, 12, 14, 12));
    bubble_delegate->SetBackgroundColor(kOriginPickerSurface);
    bubble_delegate->SetContentsView(std::move(picker));
    auto* widget = views::BubbleDialogDelegate::CreateBubbleDeprecated(
        std::move(bubble_delegate),
        views::Widget::InitParams::NATIVE_WIDGET_OWNS_WIDGET);
    auto* frame = bubble_delegate_ptr->GetBubbleFrameView();
    frame->SetRoundedCorners(gfx::RoundedCornersF(20));
    frame->SetDisplayVisibleArrow(true);
    frame->bubble_border()->set_draw_border_stroke(true);
    widget->Show();
    picker_ptr->search_field_->RequestFocus();
    return widget->GetWeakPtr();
  }

  OriginWorkspaceIconPickerBubble(views::View* anchor,
                                  std::string selected_icon,
                                  IconSelectedCallback icon_selected_callback)
      : selected_icon_(std::move(selected_icon)),
        icon_selected_callback_(std::move(icon_selected_callback)) {
    SetBackground(views::CreateSolidBackground(kOriginPickerSurface));
    SetLayoutManager(std::make_unique<views::BoxLayout>(
        views::BoxLayout::Orientation::kVertical, gfx::Insets(), 8));

    search_container_ = AddChildView(std::make_unique<views::View>());
    search_container_->SetBackground(
        views::CreateRoundedRectBackground(kOriginPickerField, 10));
    auto* search_layout =
        search_container_->SetLayoutManager(std::make_unique<views::BoxLayout>(
            views::BoxLayout::Orientation::kHorizontal,
            gfx::Insets::TLBR(6, 10, 6, 10), 6));
    auto* search_icon =
        search_container_->AddChildView(std::make_unique<views::ImageView>());
    search_icon->SetImage(ui::ImageModel::FromVectorIcon(
        vector_icons::kSearchIcon, kOriginPickerSecondaryText, 16));
    search_field_ =
        search_container_->AddChildView(std::make_unique<views::Textfield>());
    search_field_->SetPlaceholderText(u"Search Space icons");
    search_field_->SetAccessibleName(u"Search Space icons");
    search_field_->SetFontList(OriginChromeFont(14, gfx::Font::Weight::NORMAL));
    search_field_->SetBackgroundEnabled(false);
    search_field_->SetBorder(views::CreateEmptyBorder(gfx::Insets()));
    search_field_->set_controller(this);
    views::FocusRing::Remove(search_field_);
    search_layout->SetFlexForView(search_field_, 1);

    content_container_ = AddChildView(std::make_unique<views::View>());
    content_container_->SetLayoutManager(std::make_unique<views::BoxLayout>(
        views::BoxLayout::Orientation::kVertical, gfx::Insets(), 6));
    RebuildChoices();
  }

  OriginWorkspaceIconPickerBubble(const OriginWorkspaceIconPickerBubble&) =
      delete;
  OriginWorkspaceIconPickerBubble& operator=(
      const OriginWorkspaceIconPickerBubble&) = delete;
  ~OriginWorkspaceIconPickerBubble() override = default;

  void ContentsChanged(views::Textfield* sender,
                       const std::u16string& new_contents) override {
    if (sender == search_field_) {
      RebuildChoices();
    }
  }

 private:
  bool MatchesSearch(const OriginWorkspaceIconChoice& choice) const {
    const std::string query = base::ToLowerASCII(base::UTF16ToUTF8(
        base::CollapseWhitespace(search_field_->GetText(), false)));
    if (query.empty()) {
      return true;
    }
    std::string searchable(choice.keywords);
    searchable.append(" ");
    searchable.append(base::UTF16ToUTF8(choice.name));
    searchable.append(" ");
    searchable.append(choice.key);
    return base::ToLowerASCII(searchable).find(query) != std::string::npos;
  }

  template <size_t N>
  void AddChoices(const std::array<OriginWorkspaceIconChoice, N>& choices) {
    views::View* row = nullptr;
    size_t matched_count = 0;
    for (const auto& choice : choices) {
      if (!MatchesSearch(choice)) {
        continue;
      }
      if (matched_count % 5 == 0) {
        row = content_container_->AddChildView(std::make_unique<views::View>());
        row->SetLayoutManager(std::make_unique<views::BoxLayout>(
            views::BoxLayout::Orientation::kHorizontal, gfx::Insets(), 12));
      }
      std::u16string accessible_name = u"Use ";
      accessible_name.append(choice.name);
      accessible_name.append(u" icon");
      row->AddChildView(std::make_unique<OriginWorkspaceButton>(
          base::BindRepeating(&OriginWorkspaceIconPickerBubble::SelectIcon,
                              base::Unretained(this), std::string(choice.key)),
          std::string(choice.key), accessible_name,
          choice.key == selected_icon_));
      ++matched_count;
    }
    if (matched_count == 0) {
      auto* empty_label = content_container_->AddChildView(
          std::make_unique<views::Label>(u"No matching icons"));
      empty_label->SetHorizontalAlignment(gfx::HorizontalAlignment::ALIGN_LEFT);
    }
  }

  void RebuildChoices() {
    content_container_->RemoveAllChildViews();
    auto* section_label = content_container_->AddChildView(
        std::make_unique<views::Label>(u"Line icons"));
    section_label->SetHorizontalAlignment(gfx::HorizontalAlignment::ALIGN_LEFT);
    section_label->SetFontList(
        OriginChromeFont(13, gfx::Font::Weight::SEMIBOLD));
    section_label->SetEnabledColor(kOriginPickerSecondaryText);
    AddChoices(kOriginWorkspaceIconChoices);

    content_container_->InvalidateLayout();
    PreferredSizeChanged();
    if (GetWidget()) {
      GetWidget()
          ->widget_delegate()
          ->AsBubbleDialogDelegate()
          ->SizeToContents();
    }
  }

  void SelectIcon(std::string icon) {
    icon_selected_callback_.Run(std::move(icon));
    GetWidget()->Close();
  }

  const std::string selected_icon_;
  const IconSelectedCallback icon_selected_callback_;
  raw_ptr<views::View> search_container_ = nullptr;
  raw_ptr<views::Textfield> search_field_ = nullptr;
  raw_ptr<views::View> content_container_ = nullptr;
};

BEGIN_METADATA(OriginWorkspaceIconPickerBubble)
END_METADATA

#endif

class ShortcutBox : public views::View {
  METADATA_HEADER(ShortcutBox, views::View)
 public:
  explicit ShortcutBox(const std::u16string& shortcut_text) {
    constexpr int kChildSpacing = 4;
    SetLayoutManager(std::make_unique<views::BoxLayout>(
        views::BoxLayout::Orientation::kHorizontal, gfx::Insets(),
        kChildSpacing));

    std::vector<std::u16string> tokens = base::SplitString(
        shortcut_text, u"+", base::KEEP_WHITESPACE, base::SPLIT_WANT_ALL);
    for (const auto& token : tokens) {
      AddShortcutPart(token);
    }
  }

 private:
  void AddShortcutPart(const std::u16string& text) {
    constexpr int kFontSize = 12;
    auto* shortcut_part = AddChildView(std::make_unique<views::Label>(text));
    shortcut_part->SetHorizontalAlignment(
        gfx::HorizontalAlignment::ALIGN_CENTER);
    shortcut_part->SetVerticalAlignment(gfx::VerticalAlignment::ALIGN_MIDDLE);
    const auto shortcut_font = shortcut_part->font_list();
    shortcut_part->SetFontList(shortcut_font.DeriveWithSizeDelta(
        kFontSize - shortcut_font.GetFontSize()));
    shortcut_part->SetEnabledColor(kColorBraveVerticalTabNTBShortcutTextColor);
    shortcut_part->SetBorder(views::CreateRoundedRectBorder(
        /*thickness*/ 1, /*radius*/ 4, kColorBraveVerticalTabSeparator));

    // Give padding and set minimum to width.
    auto preferred_size = shortcut_part->GetPreferredSize();
    preferred_size.Enlarge(4, 0);
    constexpr int kMinWidth = 18;
    preferred_size.set_width(std::max(kMinWidth, preferred_size.width()));
    shortcut_part->SetPreferredSize(preferred_size);
  }
};

BEGIN_METADATA(ShortcutBox)
END_METADATA

class VerticalTabNewTabButton : public BraveNewTabButton {
  METADATA_HEADER(VerticalTabNewTabButton, BraveNewTabButton)
 public:
  VerticalTabNewTabButton(PressedCallback callback,
                          const std::u16string& shortcut_text,
                          BrowserWindowInterface* browser_window_interface)
      : BraveNewTabButton(std::move(callback),
                          kLeoPlusAddIcon,
                          browser_window_interface) {
    // Turn off inkdrop to have same bg color with tab's.
    views::InkDrop::Get(this)->SetMode(views::InkDropHost::InkDropMode::OFF);

    // We're going to use flex layout for children of this class. Other children
    // from base classes should be handled out of flex layout.
    for (views::View* child : children()) {
      child->SetProperty(views::kViewIgnoredByLayoutKey, true);
    }

    SetNotifyEnterExitOnChild(true);

    constexpr int kNewTabVerticalPadding = 8;
    constexpr int kNewTabHorizontalPadding = 7;
    SetLayoutManager(std::make_unique<views::FlexLayout>())
        ->SetOrientation(views::LayoutOrientation::kHorizontal)
        .SetCrossAxisAlignment(views::LayoutAlignment::kStretch)
        .SetInteriorMargin(
            gfx::Insets::VH(kNewTabHorizontalPadding, kNewTabVerticalPadding));

    plus_icon_ = AddChildView(std::make_unique<views::ImageView>());
    plus_icon_->SetHorizontalAlignment(views::ImageView::Alignment::kCenter);
    plus_icon_->SetVerticalAlignment(views::ImageView::Alignment::kCenter);
    plus_icon_->SetImage(ui::ImageModel::FromVectorIcon(
        kLeoPlusAddIcon, kColorBraveVerticalTabNTBIconColor,
        /* icon_size= */ 16));
    plus_icon_->SetProperty(
        views::kFlexBehaviorKey,
        views::FlexSpecification(views::MinimumFlexSizeRule::kPreferred,
                                 views::MaximumFlexSizeRule::kPreferred)
            .WithOrder(1));

#if BUILDFLAG(IS_BRAVE_ORIGIN_BRANDED)
    text_ = AddChildView(std::make_unique<views::Label>(u"New Page"));
#else
    text_ = AddChildView(std::make_unique<views::Label>(
        l10n_util::GetStringUTF16(IDS_ACCNAME_NEWTAB)));
#endif
    text_->SetHorizontalAlignment(gfx::HorizontalAlignment::ALIGN_LEFT);
    text_->SetVerticalAlignment(gfx::VerticalAlignment::ALIGN_MIDDLE);
    constexpr int kGapBetweenIconAndText = 16;
    text_->SetProperty(views::kMarginsKey,
                       gfx::Insets::TLBR(0, kGapBetweenIconAndText, 0, 0));
    text_->SetProperty(views::kFlexBehaviorKey,
                       views::FlexSpecification(
                           views::MinimumFlexSizeRule::kPreferredSnapToZero,
                           views::MaximumFlexSizeRule::kPreferred)
                           .WithOrder(3));

    constexpr int kFontSize =
#if BUILDFLAG(IS_BRAVE_ORIGIN_BRANDED)
        14;
#else
        12;
#endif
#if BUILDFLAG(IS_BRAVE_ORIGIN_BRANDED)
    text_->SetFontList(OriginChromeFont(kFontSize, gfx::Font::Weight::NORMAL));
#else
    const auto text_font = text_->font_list();
    text_->SetFontList(
        text_font.DeriveWithSizeDelta(kFontSize - text_font.GetFontSize()));
#endif
    text_->SetEnabledColor(kColorBraveVerticalTabNTBTextColor);

    auto* spacer = AddChildView(std::make_unique<views::View>());
    spacer->SetProperty(
        views::kFlexBehaviorKey,
        views::FlexSpecification(views::MinimumFlexSizeRule::kScaleToZero,
                                 views::MaximumFlexSizeRule::kUnbounded)
            .WithOrder(4));

    auto* shortcut_box =
        AddChildView(std::make_unique<ShortcutBox>(shortcut_text));
    shortcut_box->SetProperty(
        views::kMarginsKey, gfx::Insets::TLBR(0, kGapBetweenIconAndText, 0, 0));
    shortcut_box->SetProperty(
        views::kFlexBehaviorKey,
        views::FlexSpecification(views::MinimumFlexSizeRule::kScaleToZero,
                                 views::MaximumFlexSizeRule::kPreferred)
            .WithOrder(2));

    SetTooltipText(l10n_util::GetStringUTF16(IDS_TOOLTIP_NEW_TAB));
    SetAccessibleName(l10n_util::GetStringUTF16(IDS_ACCNAME_NEWTAB));
  }

  ~VerticalTabNewTabButton() override = default;

  gfx::Insets GetInsets() const override {
    // This button doesn't need any insets. Invalidate parent's one.
    return gfx::Insets();
  }

  void StateChanged(ButtonState old_state) override {
    BraveNewTabButton::StateChanged(old_state);
    UpdateColors();
  }

 private:
  void UpdateColors() override {
    auto* widget = GetWidget();
    if (!widget || widget->IsClosed()) {
      // Don't update colors if the widget is closed. Otherwise, it may cause
      // crash.
      return;
    }

    BraveNewTabButton::UpdateColors();

    int bg_color_id = kColorToolbar;
    if (GetState() == views::Button::STATE_PRESSED) {
      bg_color_id = kColorBraveVerticalTabActiveBackground;
    } else if (GetState() == views::Button::STATE_HOVERED) {
      bg_color_id = kColorBraveVerticalTabHoveredBackground;
    }

    SetBackground(
        views::CreateRoundedRectBackground(bg_color_id, GetCornerRadius()));
  }

  raw_ptr<views::ImageView> plus_icon_ = nullptr;
  raw_ptr<views::Label> text_ = nullptr;
};

BEGIN_METADATA(VerticalTabNewTabButton)
END_METADATA

class ResettableResizeArea : public views::ResizeArea {
  METADATA_HEADER(ResettableResizeArea, views::ResizeArea)
 public:
  explicit ResettableResizeArea(BraveVerticalTabStripRegionView* region_view)
      : ResizeArea(region_view), region_view_(region_view) {}
  ~ResettableResizeArea() override = default;

  // views::ResizeArea
  void OnMouseReleased(const ui::MouseEvent& event) override {
    ResizeArea::OnMouseReleased(event);

    if (event.IsOnlyLeftMouseButton() && event.GetClickCount() > 1) {
      region_view_->ResetExpandedWidth();
    }
  }

 private:
  raw_ptr<BraveVerticalTabStripRegionView> region_view_;
};

BEGIN_METADATA(ResettableResizeArea)
END_METADATA

TabStripPlacementCoordinator* GetPlacementCoordinator(
    BrowserView* browser_view) {
  CHECK(browser_view);
  return BraveBrowserView::From(browser_view)
      ->tab_strip_placement_coordinator();
}

}  // namespace

BraveVerticalTabStripRegionView::BraveVerticalTabStripRegionView(
    BrowserView* browser_view,
    HorizontalTabStripRegionView* region_view)
    : views::AnimationDelegateViews(this),
      browser_view_(browser_view),
      browser_(browser_view->browser()),
      original_region_view_(region_view),
      tab_style_(TabStyle::Get()) {
  // Register this view to handle caption area hit test, so that users can drag
  // the window by dragging the vertical tab strip region.
  browser()
      ->browser_window_features()
      ->brave_non_client_hit_test_helper()
      ->RegisterCaptionArea(this);

  // As we follow user's choice for vertical tab alignment,
  // we don't need to mirror this view.
  SetMirrored(false);
  SetNotifyEnterExitOnChild(true);

  auto* container =
      views::AsViewClass<BraveTabContainer>(tab_strip()->tab_container_);
  CHECK(container);
  container->SetVerticalTabStripRegionView(this);

  // The default state is kExpanded, so reset animation state to 1.0.
  width_animation_.Reset(1.0);
#if BUILDFLAG(IS_BRAVE_ORIGIN_BRANDED)
  width_animation_.SetSlideDuration(base::Milliseconds(200));
#endif

#if BUILDFLAG(IS_BRAVE_ORIGIN_BRANDED)
  origin_page_column_ = AddChildView(std::make_unique<views::View>());
  origin_page_column_->SetBackground(
      views::CreateSolidBackground(kColorBraveVerticalTabInactiveBackground));
  region_view_container_ =
      origin_page_column_->AddChildView(std::make_unique<views::View>());
#else
  region_view_container_ = AddChildView(std::make_unique<views::View>());
#endif
  region_view_container_->SetLayoutManager(
      std::make_unique<views::FillLayout>());

#if BUILDFLAG(IS_BRAVE_ORIGIN_BRANDED)
  origin_workspace_service_ =
      WorkspaceServiceFactory::GetForProfile(browser_->GetProfile());
  CHECK(origin_workspace_service_);
  origin_workspace_service_->AddObserver(this);
  origin_space_controller_ = browser_->GetFeatures().origin_space_controller();
  CHECK(origin_space_controller_);
  origin_space_controller_->AddObserver(this);
  origin_active_workspace_id_ = origin_space_controller_->active_space_id();

  origin_workspace_rail_ = AddChildView(std::make_unique<views::View>());
  origin_workspace_rail_->SetLayoutManager(std::make_unique<views::BoxLayout>(
      views::BoxLayout::Orientation::kVertical, gfx::Insets::TLBR(8, 10, 8, 10),
      kOriginWorkspaceGap));
  origin_workspace_rail_->SetBackground(
      views::CreateSolidBackground(kColorBraveVerticalTabInactiveBackground));

  origin_workspace_header_ =
      origin_page_column_->AddChildView(std::make_unique<views::View>());
  origin_workspace_header_->SetBackground(
      views::CreateSolidBackground(kColorBraveVerticalTabInactiveBackground));
  auto* workspace_header_layout = origin_workspace_header_->SetLayoutManager(
      std::make_unique<views::BoxLayout>(
          views::BoxLayout::Orientation::kHorizontal,
          gfx::Insets::TLBR(7, 8, 5, 8), 7));
  workspace_header_layout->set_cross_axis_alignment(
      views::BoxLayout::CrossAxisAlignment::kCenter);

  origin_workspace_icon_button_ = origin_workspace_header_->AddChildView(
      std::make_unique<OriginWorkspaceButton>(
          base::BindRepeating(
              &BraveVerticalTabStripRegionView::ShowOriginWorkspaceIconPicker,
              base::Unretained(this)),
          kOriginSpaceIconIdeas, u"Change Space icon", false));
  origin_workspace_icon_button_->SetPreferredSize(gfx::Size(26, 26));

  auto* workspace_text =
      origin_workspace_header_->AddChildView(std::make_unique<views::View>());
  workspace_text->SetLayoutManager(std::make_unique<views::BoxLayout>(
      views::BoxLayout::Orientation::kVertical, gfx::Insets(), 0));
  workspace_header_layout->SetFlexForView(workspace_text, 1);

  origin_workspace_title_ =
      workspace_text->AddChildView(std::make_unique<OriginWorkspaceTitleButton>(
          base::BindRepeating(
              &BraveVerticalTabStripRegionView::BeginOriginWorkspaceRename,
              base::Unretained(this)),
          std::u16string()));
  origin_workspace_title_->SetHorizontalAlignment(
      gfx::HorizontalAlignment::ALIGN_LEFT);
  origin_workspace_title_->SetBorder(views::CreateEmptyBorder(gfx::Insets()));

  origin_workspace_name_editor_ =
      workspace_text->AddChildView(std::make_unique<views::Textfield>());
  origin_workspace_name_editor_->SetVisible(false);
  origin_workspace_name_editor_->set_controller(this);
  origin_workspace_name_editor_->SetFontList(
      OriginChromeFont(16, gfx::Font::Weight::SEMIBOLD));
  origin_workspace_name_editor_->SetBackgroundEnabled(false);
  origin_workspace_name_editor_->SetBorder(
      views::CreateEmptyBorder(gfx::Insets::TLBR(0, 0, 1, 0)));
  views::FocusRing::Remove(origin_workspace_name_editor_);

  origin_workspace_meta_ =
      workspace_text->AddChildView(std::make_unique<views::Label>());
  origin_workspace_meta_->SetHorizontalAlignment(
      gfx::HorizontalAlignment::ALIGN_LEFT);
  origin_workspace_meta_->SetFontList(
      OriginChromeFont(11, gfx::Font::Weight::NORMAL));
  origin_workspace_meta_->SetEnabledColor(kColorBraveVerticalTabNTBTextColor);

  origin_workspace_save_button_ = origin_workspace_header_->AddChildView(
      std::make_unique<views::LabelButton>(
          base::BindRepeating(
              &BraveVerticalTabStripRegionView::CommitOriginWorkspaceRename,
              base::Unretained(this)),
          u"Save"));
  origin_workspace_save_button_->SetVisible(false);
  origin_workspace_save_button_->SetIsDefault(true);
  origin_workspace_save_button_->SetBorder(
      views::CreateEmptyBorder(gfx::Insets::VH(6, 12)));
  origin_workspace_save_button_->SetBackground(
      views::CreateRoundedRectBackground(kColorBraveVerticalTabActiveBackground,
                                         8));
  views::InkDrop::Get(origin_workspace_save_button_)
      ->SetMode(views::InkDropHost::InkDropMode::OFF);

  origin_workspace_more_button_ = origin_workspace_header_->AddChildView(
      std::make_unique<views::LabelButton>(
          base::BindRepeating(
              &BraveVerticalTabStripRegionView::BeginOriginWorkspaceRename,
              base::Unretained(this)),
          std::u16string()));
  origin_workspace_more_button_->SetPreferredSize(gfx::Size(26, 26));
  origin_workspace_more_button_->SetBorder(
      views::CreateEmptyBorder(gfx::Insets()));
  origin_workspace_more_button_->SetImageModel(
      views::Button::STATE_NORMAL,
      ui::ImageModel::FromVectorIcon(kLeoEditBoxIcon,
                                     kColorBraveVerticalTabNTBIconColor, 16));
  origin_workspace_more_button_->SetTooltipText(u"Edit Space");
  origin_workspace_more_button_->SetAccessibleName(u"Edit Space");
  views::InkDrop::Get(origin_workspace_more_button_)
      ->SetMode(views::InkDropHost::InkDropMode::OFF);

  origin_pages_header_ =
      origin_page_column_->AddChildView(std::make_unique<views::View>());
  origin_pages_header_->SetVisible(false);

  origin_search_button_ = origin_page_column_->AddChildView(
      std::make_unique<OriginPageListActionButton>(
          base::BindRepeating(
              &BraveVerticalTabStripRegionView::ShowOriginQuickOpen,
              base::Unretained(this)),
          kLeoSearchIcon, u"Find or open a page", u"O   Ctrl T",
          /*field=*/true));
  // Quick Open already has first-class titlebar and keyboard entry points.
  // Keeping a second search field inside the page list wastes the most useful
  // part of a compact sidebar and diverges from Sigma's hierarchy.
  origin_search_button_->SetVisible(false);
  origin_new_page_button_ = container->SetOriginNewPageButton(
      std::make_unique<OriginPageListActionButton>(
          base::BindRepeating(
              &BraveVerticalTabStripRegionView::ShowOriginQuickOpen,
              base::Unretained(this)),
          kLeoPlusAddIcon, u"New page", u"N", /*field=*/false));

  origin_status_row_ =
      origin_page_column_->AddChildView(std::make_unique<views::View>());
  auto* status_layout =
      origin_status_row_->SetLayoutManager(std::make_unique<views::BoxLayout>(
          views::BoxLayout::Orientation::kHorizontal,
          gfx::Insets::TLBR(0, 14, 0, 10), 7));
  status_layout->set_cross_axis_alignment(
      views::BoxLayout::CrossAxisAlignment::kCenter);
  auto* status_dot =
      origin_status_row_->AddChildView(std::make_unique<views::View>());
  status_dot->SetPreferredSize(gfx::Size(6, 6));
  status_dot->SetBackground(
      views::CreateRoundedRectBackground(SkColorSetRGB(0x17, 0xB2, 0x6A), 3));
  auto* status_label = origin_status_row_->AddChildView(
      std::make_unique<views::Label>(u"Synced just now"));
  status_label->SetHorizontalAlignment(gfx::HorizontalAlignment::ALIGN_LEFT);
  status_label->SetFontList(OriginChromeFont(11, gfx::Font::Weight::NORMAL));
  status_label->SetEnabledColor(kColorBraveVerticalTabNTBTextColor);
  status_layout->SetFlexForView(status_label, 1);
  auto shortcut_button = std::make_unique<views::ImageButton>(
      base::BindRepeating(
          &BraveVerticalTabStripRegionView::ShowOriginShortcutHelp,
          base::Unretained(this)));
  shortcut_button->SetImageModel(
      views::Button::STATE_NORMAL,
      ui::ImageModel::FromVectorIcon(vector_icons::kKeyboardIcon,
                                     kColorBraveVerticalTabNTBIconColor, 15));
  shortcut_button->SetPreferredSize(gfx::Size(24, 24));
  shortcut_button->SetAccessibleName(u"Keyboard shortcuts");
  shortcut_button->SetTooltipText(u"Keyboard shortcuts");
  shortcut_button->SetFocusBehavior(views::View::FocusBehavior::ALWAYS);
  views::InkDrop::Get(shortcut_button.get())
      ->SetMode(views::InkDropHost::InkDropMode::OFF);
  origin_shortcut_button_ =
      origin_status_row_->AddChildView(std::move(shortcut_button));

  auto theme_button = std::make_unique<views::ImageButton>(
      base::BindRepeating(
          &BraveVerticalTabStripRegionView::ShowOriginThemePicker,
          base::Unretained(this)));
  theme_button->SetImageModel(
      views::Button::STATE_NORMAL,
      ui::ImageModel::FromVectorIcon(kLeoSettingsIcon,
                                     kColorBraveVerticalTabNTBIconColor, 15));
  theme_button->SetPreferredSize(gfx::Size(24, 24));
  theme_button->SetAccessibleName(u"Appearance");
  theme_button->SetTooltipText(u"Choose System, Light, or Dark appearance");
  theme_button->SetFocusBehavior(views::View::FocusBehavior::ALWAYS);
  views::InkDrop::Get(theme_button.get())
      ->SetMode(views::InkDropHost::InkDropMode::OFF);
  origin_theme_button_ =
      origin_status_row_->AddChildView(std::move(theme_button));

  RebuildOriginWorkspaceUI();
#endif

  auto* placement_coordinator = GetPlacementCoordinator(browser_view);
  CHECK(placement_coordinator);
  placement_coordinator->SetPlacement(TabStripPlacementKind::kVerticalTabStrip,
                                      region_view_container_.get());

  separator_ = AddChildView(std::make_unique<views::View>());
  separator_->SetBackground(
      views::CreateSolidBackground(kColorBraveVerticalTabSeparator));
  new_tab_button_ = AddChildView(std::make_unique<VerticalTabNewTabButton>(
      base::BindRepeating(&TabStrip::NewTabButtonPressed,
                          base::Unretained(original_region_view_->tab_strip_)),
      GetShortcutTextForNewTabButton(browser_view), browser_));

  resize_area_ = AddChildView(std::make_unique<ResettableResizeArea>(this));
#if BUILDFLAG(IS_BRAVE_ORIGIN_BRANDED)
  SetBackground(
      views::CreateSolidBackground(kColorBraveVerticalTabInactiveBackground));
#else
  SetBackground(views::CreateSolidBackground(kColorToolbar));
#endif

  auto* prefs = browser_->GetProfile()->GetPrefs();

  sidebar_side_.Init(prefs::kSidePanelHorizontalAlignment, prefs,
                     base::BindRepeating(
                         &BraveVerticalTabStripRegionView::OnBrowserPanelsMoved,
                         base::Unretained(this)));

  expanded_width_pref_.Init(
      brave_tabs::kVerticalTabsExpandedWidth, prefs,
      base::BindRepeating(
          &BraveVerticalTabStripRegionView::OnExpandedWidthPrefChanged,
          base::Unretained(this)));
  OnExpandedWidthPrefChanged();

  show_vertical_tabs_.Init(
      brave_tabs::kVerticalTabsEnabled, prefs,
      base::BindRepeating(
          &BraveVerticalTabStripRegionView::OnShowVerticalTabsPrefChanged,
          base::Unretained(this)));
  UpdateLayout();

  collapsed_pref_.Init(
      brave_tabs::kVerticalTabsCollapsed, prefs,
      base::BindRepeating(
          &BraveVerticalTabStripRegionView::OnCollapsedPrefChanged,
          base::Unretained(this)));
  OnCollapsedPrefChanged();

  expanded_state_per_window_pref_.Init(
      brave_tabs::kVerticalTabsExpandedStatePerWindow, prefs,
      base::BindRepeating(
          &BraveVerticalTabStripRegionView::OnExpandedStatePerWindowPrefChanged,
          base::Unretained(this)));

  floating_mode_pref_.Init(
      brave_tabs::kVerticalTabsFloatingEnabled, prefs,
      base::BindRepeating(
          &BraveVerticalTabStripRegionView::OnFloatingModePrefChanged,
          base::Unretained(this)));

#if BUILDFLAG(IS_MAC)
  show_toolbar_on_fullscreen_pref_.Init(
      prefs::kShowFullscreenToolbar, prefs,
      base::BindRepeating(
          &BraveVerticalTabStripRegionView::OnFullscreenStateChanged,
          base::Unretained(this)));
#endif

  vertical_tab_on_right_.Init(
      brave_tabs::kVerticalTabsOnRight, prefs,
      base::BindRepeating(
          &BraveVerticalTabStripRegionView::OnBrowserPanelsMoved,
          base::Unretained(this)));

  if (base::FeatureList::IsEnabled(tabs::kBraveVerticalTabHideCompletely)) {
    hide_completely_when_collapsed_pref_.Init(
        brave_tabs::kVerticalTabsHideCompletelyWhenCollapsed, prefs,
        base::BindRepeating(&BraveVerticalTabStripRegionView::
                                OnHideComopletelyWhenCollapsedPrefChanged,
                            base::Unretained(this)));
  }

  show_toggle_button_pref_.Init(
      brave_tabs::kVerticalTabsShowToggleButton, prefs,
      base::BindRepeating(
          &BraveVerticalTabStripRegionView::OnShowToggleButtonPrefChanged,
          base::Unretained(this)));

  widget_observation_.Observe(browser_view->GetWidget());

  if (auto* focus_mode_controller =
          browser_->GetFeatures().focus_mode_controller()) {
    focus_mode_observation_.Observe(focus_mode_controller);
  }

  // Subscribe browser closing event, so we can dispose of the context menu.
  browser_did_close_subscription_ = browser_->RegisterBrowserDidClose(
      base::BindRepeating(&BraveVerticalTabStripRegionView::OnBrowserClosing,
                          base::Unretained(this)));

  // Note: This should happen after all the PrefMembers have been initialized.
  OnShowToggleButtonPrefChanged();

  set_context_menu_controller(this);
}

BraveVerticalTabStripRegionView::~BraveVerticalTabStripRegionView() {
#if BUILDFLAG(IS_BRAVE_ORIGIN_BRANDED)
  if (origin_workspace_service_) {
    origin_workspace_service_->RemoveObserver(this);
  }
  if (origin_space_controller_) {
    origin_space_controller_->RemoveObserver(this);
  }
#endif
  auto* container =
      views::AsViewClass<BraveTabContainer>(tab_strip()->tab_container_);
  CHECK(container);
  // This view can be destroyed before the tab container is destroyed.
  container->SetVerticalTabStripRegionView(nullptr);

  // We need to move tab strip region to its original parent to avoid crash
  // during drag and drop session.
  if (auto* coordinator = GetPlacementCoordinator(browser_view_)) {
    coordinator->ClearPlacement(TabStripPlacementKind::kVerticalTabStrip);
  }
  UpdateLayout();
}

void BraveVerticalTabStripRegionView::OnOriginWorkspaceSelected(
    std::string id) {
#if BUILDFLAG(IS_BRAVE_ORIGIN_BRANDED)
  if (!origin_workspace_service_->GetOriginSpace(id)) {
    return;
  }

  origin_space_controller_->SelectSpace(id);
  origin_active_workspace_id_ = origin_space_controller_->active_space_id();
  RebuildOriginWorkspaceUI();
  ApplyOriginWorkspaceTabs();
  InvalidateLayout();
#endif
}

void BraveVerticalTabStripRegionView::RebuildOriginWorkspaceUI() {
#if BUILDFLAG(IS_BRAVE_ORIGIN_BRANDED)
  // Release the tracked raw_ptr references while their views are still alive.
  // Chromium's dangling-pointer detector intentionally rejects clearing these
  // references after RemoveAllChildViews() has destroyed the buttons.
  origin_workspace_buttons_.clear();
  origin_workspace_rail_->RemoveAllChildViews();

  for (const auto& space : origin_workspace_service_->GetOriginSpaces()) {
    auto* button = origin_workspace_rail_->AddChildView(
        std::make_unique<OriginWorkspaceButton>(
            base::BindRepeating(
                &BraveVerticalTabStripRegionView::OnOriginWorkspaceSelected,
                base::Unretained(this), space.id),
            space.icon, base::UTF8ToUTF16(space.name),
            space.id == origin_active_workspace_id_));
    origin_workspace_buttons_.push_back(button);
  }
  origin_workspace_rail_->AddChildView(std::make_unique<OriginWorkspaceButton>(
      base::BindRepeating(
          &BraveVerticalTabStripRegionView::CreateOriginWorkspace,
          base::Unretained(this)),
      "add", u"New Space", false));

  auto* rail_spacer =
      origin_workspace_rail_->AddChildView(std::make_unique<views::View>());
  auto* rail_layout = static_cast<views::BoxLayout*>(
      origin_workspace_rail_->GetLayoutManager());
  rail_layout->SetFlexForView(rail_spacer, 1);
  origin_workspace_rail_->AddChildView(std::make_unique<OriginWorkspaceButton>(
      views::Button::PressedCallback(), "sync", u"Sync status", false));
  origin_workspace_rail_->AddChildView(std::make_unique<OriginWorkspaceButton>(
      views::Button::PressedCallback(), "account", u"Account", false));

  const auto* active =
      origin_workspace_service_->GetOriginSpace(origin_active_workspace_id_);
  if (active) {
    const std::u16string name = base::UTF8ToUTF16(active->name);
    origin_workspace_title_->SetText(name);
    origin_workspace_title_->SetAccessibleName(u"Edit Space name: " + name);
    origin_workspace_title_->SetTooltipText(u"Rename Space");
    views::AsViewClass<OriginWorkspaceButton>(origin_workspace_icon_button_)
        ->SetOriginIcon(active->icon);
    origin_workspace_icon_button_->SetAccessibleName(u"Change icon for " +
                                                     name);
    origin_workspace_icon_button_->SetTooltipText(u"Change Space icon");
  }
  UpdateOriginWorkspaceMeta();
  origin_workspace_rail_->InvalidateLayout();
  origin_workspace_header_->InvalidateLayout();
#endif
}

void BraveVerticalTabStripRegionView::CreateOriginWorkspace() {
#if BUILDFLAG(IS_BRAVE_ORIGIN_BRANDED)
  std::string icon = kOriginSpaceIconIdeas;
  const auto& spaces = origin_workspace_service_->GetOriginSpaces();
  for (const auto& choice : kOriginWorkspaceIconChoices) {
    if (std::ranges::none_of(spaces, [&](const auto& space) {
          return space.icon == choice.key;
        })) {
      icon = choice.key;
      break;
    }
  }
  const std::string id =
      origin_workspace_service_->CreateOriginSpace("Untitled", icon);
  OnOriginWorkspaceSelected(id);
  // A new Space should feel immediately usable, not expose Chromium's
  // underlying tab from the previous Space through an empty-color overlay.
  // Creating the normal Brave NTP after selecting the Space also assigns that
  // page to the new Space through OriginSpaceController's insertion observer.
  chrome::NewTab(browser_, NewTabTypes::kNewTabCommand);
  BeginOriginWorkspaceRename();
#endif
}

void BraveVerticalTabStripRegionView::BeginOriginWorkspaceRename() {
#if BUILDFLAG(IS_BRAVE_ORIGIN_BRANDED)
  const auto* active =
      origin_workspace_service_->GetOriginSpace(origin_active_workspace_id_);
  if (!active) {
    return;
  }
  origin_workspace_name_editor_->SetText(base::UTF8ToUTF16(active->name));
  if (origin_workspace_icon_picker_widget_) {
    origin_workspace_icon_picker_widget_->Close();
    origin_workspace_icon_picker_widget_.reset();
  }
  SetOriginWorkspaceRenameMode(true);
  origin_workspace_name_editor_->RequestFocus();
  origin_workspace_name_editor_->SelectAll(false);
#endif
}

void BraveVerticalTabStripRegionView::CommitOriginWorkspaceRename() {
#if BUILDFLAG(IS_BRAVE_ORIGIN_BRANDED)
  const auto* active =
      origin_workspace_service_->GetOriginSpace(origin_active_workspace_id_);
  if (!active) {
    return;
  }
  OriginSpaceMetadata updated = *active;
  std::u16string name =
      base::CollapseWhitespace(origin_workspace_name_editor_->GetText(), false);
  if (name.empty()) {
    name = base::UTF8ToUTF16(active->name);
  }
  if (name.size() > 40u) {
    name.resize(40u);
  }
  updated.name = base::UTF16ToUTF8(name);
  origin_workspace_service_->UpdateOriginSpace(updated);
  SetOriginWorkspaceRenameMode(false);
#endif
}

void BraveVerticalTabStripRegionView::CancelOriginWorkspaceRename() {
#if BUILDFLAG(IS_BRAVE_ORIGIN_BRANDED)
  SetOriginWorkspaceRenameMode(false);
#endif
}

void BraveVerticalTabStripRegionView::SetOriginWorkspaceRenameMode(
    bool editing) {
#if BUILDFLAG(IS_BRAVE_ORIGIN_BRANDED)
  origin_workspace_title_->SetVisible(!editing);
  origin_workspace_name_editor_->SetVisible(editing);
  origin_workspace_save_button_->SetVisible(editing);
  origin_workspace_icon_button_->SetVisible(true);
  origin_workspace_meta_->SetVisible(!editing);
  origin_workspace_more_button_->SetVisible(!editing);
  origin_workspace_header_->SetBackground(
      editing ? views::CreateRoundedRectBackground(
                    kColorBraveVerticalTabHoveredBackground, 10)
              : views::CreateSolidBackground(
                    kColorBraveVerticalTabInactiveBackground));
  origin_workspace_header_->InvalidateLayout();
  origin_workspace_header_->SchedulePaint();
#endif
}

void BraveVerticalTabStripRegionView::ShowOriginQuickOpen() {
#if BUILDFLAG(IS_BRAVE_ORIGIN_BRANDED)
  BraveBrowserView::From(browser_view_)->ShowOriginQuickOpen();
#endif
}

void BraveVerticalTabStripRegionView::ShowOriginShortcutHelp() {
#if BUILDFLAG(IS_BRAVE_ORIGIN_BRANDED)
  if (origin_shortcut_help_widget_) {
    const bool was_visible = origin_shortcut_help_widget_->IsVisible();
    origin_shortcut_help_widget_->Close();
    origin_shortcut_help_widget_.reset();
    if (was_visible) {
      return;
    }
  }
  if (origin_theme_picker_widget_) {
    origin_theme_picker_widget_->Close();
    origin_theme_picker_widget_.reset();
  }

  auto content = std::make_unique<views::View>();
  content->SetBackground(views::CreateSolidBackground(kOriginPickerSurface));
  content->SetLayoutManager(std::make_unique<views::BoxLayout>(
      views::BoxLayout::Orientation::kVertical, gfx::Insets(), 6));

  auto* title =
      content->AddChildView(std::make_unique<views::Label>(u"Keyboard shortcuts"));
  title->SetSubpixelRenderingEnabled(false);
  title->SetHorizontalAlignment(gfx::HorizontalAlignment::ALIGN_LEFT);
  title->SetFontList(OriginChromeFont(15, gfx::Font::Weight::SEMIBOLD));
  title->SetEnabledColor(SkColorSetRGB(0xF5, 0xF5, 0xF6));
  title->SetProperty(views::kMarginsKey, gfx::Insets::TLBR(0, 2, 4, 2));

  struct ShortcutEntry {
    std::u16string_view key;
    std::u16string_view description;
  };
  constexpr std::array<ShortcutEntry, 14> kShortcuts = {{
      {u"O / N", u"Search or open a page"},
      {u"J / K", u"Next / previous page"},
      {u"1–5", u"Switch Space"},
      {u"P", u"Pin / unpin page"},
      {u"W", u"Close page"},
      {u"D", u"Close page tree"},
      {u"Z", u"Reopen closed page"},
      {u"[ / ]", u"Back / forward"},
      {u"I", u"Enter insert mode"},
      {u"Esc", u"Leave insert mode"},
      {u"⌘ 1–5", u"Switch Space directly"},
      {u"⌘ ↑ / ↓", u"Previous / next Space"},
      {u"⌘ ←", u"Show / hide sidebar"},
      {u"⌘ →", u"Exit split view"},
  }};

  for (const ShortcutEntry& entry : kShortcuts) {
    auto* row = content->AddChildView(std::make_unique<views::View>());
    auto* row_layout =
        row->SetLayoutManager(std::make_unique<views::BoxLayout>(
            views::BoxLayout::Orientation::kHorizontal,
            gfx::Insets::TLBR(2, 2, 2, 2), 10));
    row_layout->set_cross_axis_alignment(
        views::BoxLayout::CrossAxisAlignment::kCenter);

    auto* description = row->AddChildView(
        std::make_unique<views::Label>(std::u16string(entry.description)));
    description->SetSubpixelRenderingEnabled(false);
    description->SetHorizontalAlignment(gfx::HorizontalAlignment::ALIGN_LEFT);
    description->SetFontList(OriginChromeFont(12, gfx::Font::Weight::NORMAL));
    description->SetEnabledColor(kOriginPickerSecondaryText);
    row_layout->SetFlexForView(description, 1);

    auto* key = row->AddChildView(
        std::make_unique<views::Label>(std::u16string(entry.key)));
    key->SetSubpixelRenderingEnabled(false);
    key->SetHorizontalAlignment(gfx::HorizontalAlignment::ALIGN_CENTER);
    key->SetFontList(OriginChromeFont(10, gfx::Font::Weight::SEMIBOLD));
    key->SetEnabledColor(SkColorSetRGB(0xD7, 0xD8, 0xDB));
    key->SetBackground(views::CreateRoundedRectBackground(
        SkColorSetARGB(0x19, 0xFF, 0xFF, 0xFF), 5));
    key->SetBorder(
        views::CreateEmptyBorder(gfx::Insets::TLBR(2, 7, 2, 7)));
  }

  auto bubble_delegate = std::make_unique<views::BubbleDialogDelegate>(
      origin_shortcut_button_, views::BubbleBorder::BOTTOM_RIGHT,
      views::BubbleBorder::STANDARD_SHADOW, /*autosize=*/true);
  auto* bubble_delegate_ptr = bubble_delegate.get();
  bubble_delegate->SetButtons(
      static_cast<int>(ui::mojom::DialogButton::kNone));
  bubble_delegate->SetShowTitle(false);
  bubble_delegate->SetShowCloseButton(false);
  bubble_delegate->SetAccessibleTitle(u"Keyboard shortcuts");
  bubble_delegate->set_adjust_if_offscreen(true);
  bubble_delegate->set_close_on_deactivate(true);
  bubble_delegate->set_fixed_width(320);
  bubble_delegate->set_margins(gfx::Insets::TLBR(14, 14, 14, 14));
  bubble_delegate->SetBackgroundColor(kOriginPickerSurface);
  bubble_delegate->SetContentsView(std::move(content));
  auto* widget = views::BubbleDialogDelegate::CreateBubbleDeprecated(
      std::move(bubble_delegate),
      views::Widget::InitParams::NATIVE_WIDGET_OWNS_WIDGET);
  auto* frame = bubble_delegate_ptr->GetBubbleFrameView();
  frame->SetRoundedCorners(gfx::RoundedCornersF(16));
  frame->SetDisplayVisibleArrow(true);
  frame->bubble_border()->set_draw_border_stroke(true);
  origin_shortcut_help_widget_ = widget->GetWeakPtr();
  widget->Show();
#endif
}

void BraveVerticalTabStripRegionView::ShowOriginThemePicker() {
#if BUILDFLAG(IS_BRAVE_ORIGIN_BRANDED)
  if (origin_theme_picker_widget_) {
    const bool was_visible = origin_theme_picker_widget_->IsVisible();
    origin_theme_picker_widget_->Close();
    origin_theme_picker_widget_.reset();
    if (was_visible) {
      return;
    }
  }
  if (origin_shortcut_help_widget_) {
    origin_shortcut_help_widget_->Close();
    origin_shortcut_help_widget_.reset();
  }

  ThemeService* theme_service =
      ThemeServiceFactory::GetForProfile(browser_->GetProfile());
  if (!theme_service) {
    return;
  }
  const ThemeService::BrowserColorScheme current_scheme =
      theme_service->GetBrowserColorScheme();

  auto content = std::make_unique<views::View>();
  content->SetBackground(views::CreateSolidBackground(kOriginPickerSurface));
  content->SetLayoutManager(std::make_unique<views::BoxLayout>(
      views::BoxLayout::Orientation::kVertical, gfx::Insets(), 6));

  auto* title =
      content->AddChildView(std::make_unique<views::Label>(u"Appearance"));
  title->SetSubpixelRenderingEnabled(false);
  title->SetHorizontalAlignment(gfx::HorizontalAlignment::ALIGN_LEFT);
  title->SetFontList(OriginChromeFont(15, gfx::Font::Weight::SEMIBOLD));
  title->SetEnabledColor(SkColorSetRGB(0xF5, 0xF5, 0xF6));
  title->SetProperty(views::kMarginsKey, gfx::Insets::TLBR(0, 4, 5, 4));

  struct ThemeOption {
    ThemeService::BrowserColorScheme scheme;
    std::u16string_view label;
    std::u16string_view detail;
  };
  constexpr std::array<ThemeOption, 3> kThemeOptions = {{
      {ThemeService::BrowserColorScheme::kSystem, u"System",
       u"Follow macOS appearance"},
      {ThemeService::BrowserColorScheme::kLight, u"Light",
       u"Always use the light shell"},
      {ThemeService::BrowserColorScheme::kDark, u"Dark",
       u"Always use the dark shell"},
  }};

  for (const ThemeOption& option : kThemeOptions) {
    const bool selected = option.scheme == current_scheme;
    auto button = std::make_unique<views::LabelButton>(
        base::BindRepeating(
            &BraveVerticalTabStripRegionView::SetOriginThemeMode,
            weak_factory_.GetWeakPtr(), static_cast<int>(option.scheme)),
        (selected ? u"✓  " : u"    ") + std::u16string(option.label));
    button->SetAccessibleName(u"Use " + std::u16string(option.label) +
                              u" appearance");
    button->SetTooltipText(std::u16string(option.detail));
    button->SetHorizontalAlignment(gfx::HorizontalAlignment::ALIGN_LEFT);
    button->SetTextSubpixelRenderingEnabled(false);
    button->SetEnabledTextColors(selected
                                     ? SkColorSetRGB(0xB2, 0xCC, 0xFF)
                                     : SkColorSetRGB(0xF5, 0xF5, 0xF6));
    button->SetBorder(
        views::CreateEmptyBorder(gfx::Insets::TLBR(8, 10, 8, 10)));
    button->SetBackground(views::CreateRoundedRectBackground(
        selected ? SkColorSetARGB(0x26, 0x15, 0x70, 0xEF)
                 : SK_ColorTRANSPARENT,
        8));
    views::InkDrop::Get(button.get())
        ->SetMode(views::InkDropHost::InkDropMode::OFF);
    content->AddChildView(std::move(button));
  }

  auto bubble_delegate = std::make_unique<views::BubbleDialogDelegate>(
      origin_theme_button_, views::BubbleBorder::BOTTOM_RIGHT,
      views::BubbleBorder::STANDARD_SHADOW, /*autosize=*/true);
  auto* bubble_delegate_ptr = bubble_delegate.get();
  bubble_delegate->SetButtons(
      static_cast<int>(ui::mojom::DialogButton::kNone));
  bubble_delegate->SetShowTitle(false);
  bubble_delegate->SetShowCloseButton(false);
  bubble_delegate->SetAccessibleTitle(u"Appearance");
  bubble_delegate->set_adjust_if_offscreen(true);
  bubble_delegate->set_close_on_deactivate(true);
  bubble_delegate->set_fixed_width(250);
  bubble_delegate->set_margins(gfx::Insets::TLBR(12, 12, 12, 12));
  bubble_delegate->SetBackgroundColor(kOriginPickerSurface);
  bubble_delegate->SetContentsView(std::move(content));
  auto* widget = views::BubbleDialogDelegate::CreateBubbleDeprecated(
      std::move(bubble_delegate),
      views::Widget::InitParams::NATIVE_WIDGET_OWNS_WIDGET);
  auto* frame = bubble_delegate_ptr->GetBubbleFrameView();
  frame->SetRoundedCorners(gfx::RoundedCornersF(16));
  frame->SetDisplayVisibleArrow(true);
  frame->bubble_border()->set_draw_border_stroke(true);
  origin_theme_picker_widget_ = widget->GetWeakPtr();
  widget->Show();
#endif
}

void BraveVerticalTabStripRegionView::SetOriginThemeMode(int mode) {
#if BUILDFLAG(IS_BRAVE_ORIGIN_BRANDED)
  const auto scheme = static_cast<ThemeService::BrowserColorScheme>(mode);
  switch (scheme) {
    case ThemeService::BrowserColorScheme::kSystem:
    case ThemeService::BrowserColorScheme::kLight:
    case ThemeService::BrowserColorScheme::kDark:
      break;
    default:
      return;
  }
  // Theme changes synchronously visit every browser widget. Widget::Close()
  // only hides a native bubble and schedules its destruction, so merely
  // delaying SetBrowserColorScheme() can still leave a half-closed picker in
  // BrowserWindowThemeObserver's widget walk. Leave the button callback first,
  // destroy the picker synchronously on the next task, and apply the theme on
  // a later task after that destruction has completed.
  base::WeakPtr<views::Widget> picker = origin_theme_picker_widget_;
  origin_theme_picker_widget_.reset();
  base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
      FROM_HERE,
      base::BindOnce(
          [](base::WeakPtr<views::Widget> picker_widget) {
            if (picker_widget) {
              // BubbleDialogDelegate observes its anchor widget and forwards
              // OnWidgetThemeChanged() to the bubble. Detach that observation
              // while both widgets are still fully alive; otherwise a closed
              // native bubble can be reached during the browser's synchronous
              // theme walk with its RootView already torn down.
              if (auto* bubble = picker_widget->widget_delegate()
                                     ->AsBubbleDialogDelegate()) {
                bubble->SetAnchorView(nullptr);
              }
              picker_widget->CloseNow();
            }
          },
          std::move(picker)));
  base::SequencedTaskRunner::GetCurrentDefault()->PostDelayedTask(
      FROM_HERE,
      base::BindOnce(
          [](base::WeakPtr<BraveVerticalTabStripRegionView> region,
             ThemeService::BrowserColorScheme color_scheme) {
            if (!region) {
              return;
            }
            ThemeService* theme_service = ThemeServiceFactory::GetForProfile(
                region->browser_->GetProfile());
            if (theme_service) {
              theme_service->SetBrowserColorScheme(color_scheme);
            }
          },
          weak_factory_.GetWeakPtr(), scheme),
      base::Milliseconds(500));
#endif
}

void BraveVerticalTabStripRegionView::UpdateOriginWorkspaceMeta() {
#if BUILDFLAG(IS_BRAVE_ORIGIN_BRANDED)
  const size_t page_count = origin_space_controller_->GetPageCountForSpace(
      origin_active_workspace_id_);

  std::u16string meta = base::NumberToString16(page_count);
  meta.append(page_count == 1 ? u" page" : u" pages");
  origin_workspace_meta_->SetText(meta);
#endif
}

void BraveVerticalTabStripRegionView::EnsureOriginSpaceHasPage() {
#if BUILDFLAG(IS_BRAVE_ORIGIN_BRANDED)
  origin_new_page_pending_ = false;
  if (!origin_space_controller_->ActiveSpaceHasTabs() &&
      !browser_->tab_strip_model()->closing_all()) {
    chrome::NewTab(browser_, NewTabTypes::kNewTabCommand);
  }
#endif
}

bool BraveVerticalTabStripRegionView::HandleKeyEvent(
    views::Textfield* sender,
    const ui::KeyEvent& key_event) {
#if BUILDFLAG(IS_BRAVE_ORIGIN_BRANDED)
  if (sender != origin_workspace_name_editor_ ||
      key_event.type() != ui::EventType::kKeyPressed) {
    return false;
  }
  if (key_event.key_code() == ui::VKEY_RETURN) {
    CommitOriginWorkspaceRename();
    return true;
  }
  if (key_event.key_code() == ui::VKEY_ESCAPE) {
    CancelOriginWorkspaceRename();
    return true;
  }
#endif
  return false;
}

void BraveVerticalTabStripRegionView::ShowOriginWorkspaceIconPicker() {
#if BUILDFLAG(IS_BRAVE_ORIGIN_BRANDED)
  if (origin_workspace_icon_picker_widget_) {
    const bool was_visible = origin_workspace_icon_picker_widget_->IsVisible();
    origin_workspace_icon_picker_widget_->Close();
    origin_workspace_icon_picker_widget_.reset();
    if (was_visible) {
      return;
    }
  }
  const auto* active =
      origin_workspace_service_->GetOriginSpace(origin_active_workspace_id_);
  if (!active) {
    return;
  }
  origin_workspace_icon_picker_widget_ = OriginWorkspaceIconPickerBubble::Show(
      origin_workspace_icon_button_, active->icon,
      base::BindRepeating(
          &BraveVerticalTabStripRegionView::SetOriginWorkspaceIcon,
          weak_factory_.GetWeakPtr()));
#endif
}

void BraveVerticalTabStripRegionView::SetOriginWorkspaceIcon(std::string icon) {
#if BUILDFLAG(IS_BRAVE_ORIGIN_BRANDED)
  const auto* active =
      origin_workspace_service_->GetOriginSpace(origin_active_workspace_id_);
  if (!active || icon.empty()) {
    return;
  }
  OriginSpaceMetadata updated = *active;
  updated.icon = std::move(icon);
  origin_workspace_service_->UpdateOriginSpace(updated);
#endif
}

void BraveVerticalTabStripRegionView::OnOriginSpacesChanged() {
#if BUILDFLAG(IS_BRAVE_ORIGIN_BRANDED)
  origin_active_workspace_id_ = origin_space_controller_->active_space_id();
  RebuildOriginWorkspaceUI();
  ApplyOriginWorkspaceTabs();
#endif
}

void BraveVerticalTabStripRegionView::OnOriginSpaceControllerChanged() {
#if BUILDFLAG(IS_BRAVE_ORIGIN_BRANDED)
  const std::string active_space_id =
      origin_space_controller_->active_space_id();
  if (origin_active_workspace_id_ != active_space_id) {
    origin_active_workspace_id_ = active_space_id;
    RebuildOriginWorkspaceUI();
  }
  ApplyOriginWorkspaceTabs();
  InvalidateLayout();
#endif
}

void BraveVerticalTabStripRegionView::ApplyOriginWorkspaceTabs() {
#if BUILDFLAG(IS_BRAVE_ORIGIN_BRANDED)
  auto* model = browser_->tab_strip_model();
  auto* tab_container =
      views::AsViewClass<BraveTabContainer>(tab_strip()->tab_container_);
  CHECK(tab_container);

  // Space filtering participates in BraveTabContainer::ShouldTabBeVisible().
  // Rebuild ideal bounds before applying visibility; otherwise rows hidden in
  // the previous Space retain zero-height cached bounds when that Space is
  // selected again.
  tab_container->InvalidateIdealBounds();
  tab_container->CompleteAnimationAndLayout();
  tab_container->SchedulePaint();
  const bool active_space_empty =
      !origin_space_controller_->ActiveSpaceHasTabs();
  BraveBrowserView::From(browser_view_)
      ->SetOriginSpaceEmpty(active_space_empty);
  if (active_space_empty && !model->closing_all() &&
      !origin_new_page_pending_) {
    origin_new_page_pending_ = true;
    base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
        FROM_HERE,
        base::BindOnce(
            &BraveVerticalTabStripRegionView::EnsureOriginSpaceHasPage,
            weak_factory_.GetWeakPtr()));
  }
  UpdateOriginWorkspaceMeta();
#endif
}

void BraveVerticalTabStripRegionView::ToggleState() {
  if (state_ == State::kExpanded) {
    collapsed_pref_.SetValue(true);
    SetState(State::kCollapsed);
  } else {
    collapsed_pref_.SetValue(false);
    SetState(State::kExpanded);
  }
}

void BraveVerticalTabStripRegionView::OnWidgetActivationChanged(
    views::Widget* widget,
    bool active) {
  if (active) {
    if (*floating_mode_pref_ && IsMouseHovered()) {
      ScheduleFloatingModeTimer();
    }
    return;
  }

  // When parent widget is deactivated, we should collapse vertical tab
  mouse_enter_timer_.Stop();
  if (state_ == State::kFloating) {
    SetState(State::kCollapsed);
  }
}

void BraveVerticalTabStripRegionView::OnWidgetDestroying(
    views::Widget* widget) {
  widget_observation_.Reset();
}

void BraveVerticalTabStripRegionView::OnFullscreenStateChanged() {
  UpdateFloatingStateForBrowserMode();
}

void BraveVerticalTabStripRegionView::OnFocusModeToggled(bool enabled) {
  UpdateFloatingStateForBrowserMode();
}

void BraveVerticalTabStripRegionView::ListenFullscreenChanges() {
  auto* fullscreen_controller = GetFullscreenController();
  DCHECK(fullscreen_controller);
  // Observe changes to fullscreen state.
  fullscreen_subscription_ =
      ExclusiveAccessManager::From(browser_view_->browser())
          ->fullscreen_controller()
          ->RegisterOnFullscreenStateChanged(base::BindRepeating(
              &BraveVerticalTabStripRegionView::OnFullscreenStateChanged,
              base::Unretained(this)));
}

void BraveVerticalTabStripRegionView::StopListeningFullscreenChanges() {
  fullscreen_subscription_ = {};
}

FullscreenController* BraveVerticalTabStripRegionView::GetFullscreenController()
    const {
  auto* exclusive_access_manager =
      browser_->GetFeatures().exclusive_access_manager();
  if (!exclusive_access_manager) {
    return nullptr;
  }

  return exclusive_access_manager->fullscreen_controller();
}

bool BraveVerticalTabStripRegionView::IsTabFullscreen() const {
  const auto* fullscreen_controller = GetFullscreenController();
  return fullscreen_controller &&
         fullscreen_controller->IsWindowFullscreenForTabOrPending();
}

bool BraveVerticalTabStripRegionView::IsBrowserFullscren() const {
  const auto* fullscreen_controller = GetFullscreenController();
  return fullscreen_controller &&
         fullscreen_controller->IsFullscreenForBrowser();
}

bool BraveVerticalTabStripRegionView::
    ShouldShowVerticalTabsInBrowserFullscreen() const {
#if BUILDFLAG(IS_MAC)
  // Refer to "Always show toolbar in Fullscreen" pref in the app menu
  return show_toolbar_on_fullscreen_pref_.GetValue();
#else
  return false;
#endif
}

void BraveVerticalTabStripRegionView::SetState(State state) {
  if (state_ == state) {
    return;
  }

  mouse_enter_timer_.Stop();
  mouse_exit_timer_.Stop();

  last_state_ = std::exchange(state_, state);
  // A revealed floating panel is still a real panel. Sigma keeps the resize
  // affordance available whenever the page column is visible; disabling it in
  // kFloating made an open sidebar look resizable while retaining the normal
  // arrow cursor and ignoring drags.
  resize_area_->SetEnabled(state != State::kCollapsed);

  if (!VerticalTabController::FromBrowser(browser_)
           ->ShouldShowBraveVerticalTabs()) {
    // This can happen when "float on mouse hover" is enabled and tab strip
    // orientation has been changed.
    return;
  }

  auto tab_strip = original_region_view_->tab_strip_;
  tab_strip->SetAvailableWidthCallback(base::BindRepeating(
      &BraveVerticalTabStripRegionView::GetAvailableWidthForTabContainer,
      base::Unretained(this)));

  if (gfx::Animation::ShouldRenderRichAnimation()) {
    state_ == State::kCollapsed ? width_animation_.Hide()
                                : width_animation_.Show();
  } else {
    tab_strip->tab_container_->InvalidateIdealBounds();
    tab_strip->tab_container_->CompleteAnimationAndLayout();

    if (state_ == State::kCollapsed) {
      // Call the callback immediately if no animation.
      OnCollapseAnimationEnded();
    }
  }

  if (!GetVisible() && state_ != State::kCollapsed) {
    // This can happen when
    // * vertical tab strip is expanded temporarily in browser fullscreen mode.
    // * vertical tab strip is shown from collapsed state with
    // tabs::kBraveVerticalTabHideCompletely on.
    SetVisible(true);
  }

  PreferredSizeChanged();
  UpdateBorder();

  if (!width_animation_.is_animating()) {
    FinalizeOriginContentsResize();
  }

  if (last_state_ == State::kFloating && state_ == State::kExpanded) {
    // In this case we need to lay out pinned tabs so that they need to hide
    // title and close button.
    for (int i = 0; i < tab_strip->NumPinnedTabsInModel(); ++i) {
      tab_strip->tab_at(i)->InvalidateLayout();
    }
  }
}

void BraveVerticalTabStripRegionView::SetExpandedWidth(int dest_width) {
#if BUILDFLAG(IS_BRAVE_ORIGIN_BRANDED)
  dest_width = std::clamp(dest_width, kOriginSidebarMinimumWidth,
                          kOriginSidebarMaximumWidth);
#endif
  if (expanded_width_ == dest_width) {
    return;
  }

  expanded_width_ = dest_width;

  if (expanded_width_ != *expanded_width_pref_) {
    expanded_width_pref_.SetValue(expanded_width_);
  }

  PreferredSizeChanged();
}

void BraveVerticalTabStripRegionView::FinalizeOriginContentsResize() {
#if BUILDFLAG(IS_BRAVE_ORIGIN_BRANDED)
  BraveBrowserView::From(browser_view_)->FinalizeOriginContentsResize();
#endif
}

void BraveVerticalTabStripRegionView::UpdateStateAfterDragAndDropFinished(
    State original_state) {
  DCHECK_NE(original_state, State::kExpanded)
      << "As per ExpandTabStripForDragging(), this shouldn't happen";

  if (IsFloatingVerticalTabsEnabled() && IsMouseHovered()) {
    SetState(State::kFloating);
    return;
  }

  SetState(State::kCollapsed);
}

BraveVerticalTabStripRegionView::ScopedStateResetter
BraveVerticalTabStripRegionView::ExpandTabStripForDragging() {
  if (state_ == State::kExpanded) {
    return {};
  }

  auto resetter = std::make_unique<base::ScopedClosureRunner>(base::BindOnce(
      &BraveVerticalTabStripRegionView::UpdateStateAfterDragAndDropFinished,
      weak_factory_.GetWeakPtr(), state_));

  SetState(State::kExpanded);
  // In this case, we dont' wait for the widget bounds to be changed so that
  // tab drag controller can layout tabs properly.
  SetSize(GetPreferredSize());

  return resetter;
}

int BraveVerticalTabStripRegionView::GetAvailableWidthForTabContainer() {
  DCHECK(VerticalTabController::FromBrowser(browser_)
             ->ShouldShowBraveVerticalTabs());
  int width = GetPreferredWidthForState(state_, /*include_border=*/false,
                                        /*ignore_animation=*/false);
#if BUILDFLAG(IS_BRAVE_ORIGIN_BRANDED)
  // The tab strip is hosted in the fixed 250 px page column, not across the
  // complete sidebar. During the collapse animation this naturally shrinks to
  // zero while the rail retains its full width.
  width -= std::min(kOriginWorkspaceRailWidth, width);
#endif
  return std::max(0, width);
}

gfx::Size BraveVerticalTabStripRegionView::CalculatePreferredSize(
    const views::SizeBounds& available_size) const {
  return GetPreferredSizeForState(state_, /*include_border=*/true,
                                  /*ignore_animation=*/false);
}

gfx::Size BraveVerticalTabStripRegionView::GetMinimumSize() const {
#if BUILDFLAG(IS_BRAVE_ORIGIN_BRANDED)
  // Focus mode normally treats a revealed vertical tab strip as an overlay and
  // leaves the renderer at full-window width. That makes responsive pages lay
  // themselves out underneath Origin's much wider Space/page panel: once the
  // panel is revealed, the visible page is the right-hand crop of that layout.
  // Keep the hidden state overlay-sized, but reserve the panel's real width
  // while it is visible so the renderer receives the viewport the user sees.
  if (IsFloatingEnabledForBrowserMode() && state_ != State::kFloating) {
    return {};
  }
#else
  if (IsFloatingEnabledForBrowserMode() ||
      ((VerticalTabController::FromBrowser(browser_)
            ->ShouldHideVerticalTabsCompletelyWhenCollapsed() &&
        state_ != State::kExpanded))) {
    // Vertical tab strip always overlaps the contents area.
    return {};
  }
#endif

  auto target_state = state_;

  // Minimum size is used for host view's preferred size.
  // See BraveVerticalTabStripContainerView::ChildPreferredSizeChanged().
  // Regular Brave keeps the host size stable while the strip floats. Origin's
  // wider workspace panel intentionally reserves its expanded width below.
  if (state_ == State::kFloating) {
#if BUILDFLAG(IS_BRAVE_ORIGIN_BRANDED)
    // Origin's floating panel is a full Space/page sidebar. Its host must track
    // that visible width so web contents reflows beside it instead of remaining
    // hidden underneath it.
    target_state = State::kExpanded;
#else
    target_state = State::kCollapsed;
#endif
  }

  // Get size w/o border. Consider border width later.
  auto size = GetPreferredSizeForState(target_state,
                                       /*include_border=*/false,
                                       /*ignore_animation=*/true);
  if (size.IsZero()) {
    return size;
  }

#if BUILDFLAG(IS_BRAVE_ORIGIN_BRANDED)
  // Origin has no panel border or half-margin compensation. The minimum width
  // therefore remains the exact 56/306 px state width.
  return size;
#else
  // In rounded corners, border is changed on floating.
  // If we calculated preferred size with |include_border|,
  // it gives different size because of border change.
  // If minumum size changes during the floating, it could affect
  // contents size. It could cause contents area flickering.
  // Append same border width to |size|.
  if (BraveBrowserView::ShouldUseBraveWebViewRoundedCornersForContents(
          browser_)) {
    size.Enlarge(-(tabs::kMarginForVerticalTabContainers / 2), 0);
  } else {
    size.Enlarge(kBorderThickness, 0);
  }

  return size;
#endif
}

void BraveVerticalTabStripRegionView::Layout(PassKey) {
  if (original_region_view_->parent() != region_view_container_) {
    // When original_region_view_ is not a child of this view, it means that
    // the vertical tab strip is not visible.
    return;
  }

  const auto contents_bounds = GetContentsBounds();

#if BUILDFLAG(IS_BRAVE_ORIGIN_BRANDED)
  const int rail_width =
      std::min(kOriginWorkspaceRailWidth, contents_bounds.width());
  origin_workspace_rail_->SetBounds(contents_bounds.x(), contents_bounds.y(),
                                    rail_width, contents_bounds.height());

  const int workspace_x = 0;
  const int workspace_width =
      std::max(0, contents_bounds.width() - rail_width);
  const int contents_view_width = workspace_width;
  origin_page_column_->SetVisible(workspace_width > 0);
  origin_page_column_->SetBounds(contents_bounds.x() + rail_width,
                                 contents_bounds.y(), workspace_width,
                                 contents_bounds.height());
  origin_workspace_header_->SetBounds(workspace_x, 0, contents_view_width,
                                      kOriginWorkspaceHeaderHeight);
  origin_search_button_->SetBoundsRect(gfx::Rect());

  origin_status_row_->SetBounds(
      0, std::max(0, contents_bounds.height() - kOriginStatusRowHeight - 4),
      workspace_width, kOriginStatusRowHeight);
  origin_pages_header_->SetBoundsRect(gfx::Rect());
#else
  const int workspace_x = contents_bounds.x();
  const int workspace_width = contents_bounds.width();
  const int contents_view_width = workspace_width;
#endif

#if BUILDFLAG(IS_BRAVE_ORIGIN_BRANDED)
  const int contents_view_max_height =
      std::max(0, contents_bounds.height() - kOriginPageListTop -
                      kOriginPageListFooterHeight);
#else
  constexpr int kNewTabButtonHeight = tabs::kVerticalTabHeight;
  const int contents_view_max_height =
      contents_bounds.height() - tabs::kMarginForVerticalTabContainers -
      kNewTabButtonHeight - tabs::kMarginForVerticalTabContainers -
      kSeparatorHeight;
#endif
#if BUILDFLAG(IS_BRAVE_ORIGIN_BRANDED)
  const int contents_view_height = contents_view_max_height;
#else
  // Using tab_container_'s preferred height because tab_strip's preferred
  // height could be 0 in tests.
  const int contents_view_preferred_height =
      tab_strip()->tab_container_->GetPreferredSize().height();
  const int contents_view_height =
      std::min(contents_view_max_height, contents_view_preferred_height);
#endif

  region_view_container_->SetBoundsRect(
      gfx::Rect(gfx::Point(workspace_x,
#if BUILDFLAG(IS_BRAVE_ORIGIN_BRANDED)
                           kOriginPageListTop
#else
                           contents_bounds.y()
#endif
                           ),
                gfx::Size(contents_view_width, contents_view_height)));

#if BUILDFLAG(IS_BRAVE_ORIGIN_BRANDED)
  // Quick Open is Origin's single page-creation surface. Keeping the legacy
  // vertical-tab new-tab button here duplicates that action and leaves a
  // permanent footer which Sigma-style workspaces do not have.
  separator_->SetBoundsRect(gfx::Rect());
  new_tab_button_->SetBoundsRect(gfx::Rect());
#else
  gfx::Rect separator_bounds(region_view_container_->bounds().bottom_left(),
                             gfx::Size(workspace_width, kSeparatorHeight));
  separator_bounds.Inset(
      gfx::Insets::VH(0, tabs::kMarginForVerticalTabContainers));
  separator_->SetBoundsRect(separator_bounds);
  gfx::Rect new_tab_button_bounds(
      separator_->bounds().bottom_left(),
      gfx::Size(separator_bounds.width(), kNewTabButtonHeight));
  new_tab_button_bounds.Offset(0, tabs::kMarginForVerticalTabContainers);
  new_tab_button_->SetBoundsRect(new_tab_button_bounds);
#endif

  // Put the resize area over the inside edge. Lay it out even during the first
  // pass, before the side preference is initialized, so it can never remain
  // at empty bounds for the lifetime of a newly-created window.
  const bool tabs_on_right = !vertical_tab_on_right_.GetPrefName().empty() &&
                             *vertical_tab_on_right_;
  constexpr int kResizeAreaWidth = 12;
  resize_area_->SetBounds(
      tabs_on_right ? 0 : width() - kResizeAreaWidth,
      contents_bounds.y(), kResizeAreaWidth, contents_bounds.height());
}

void BraveVerticalTabStripRegionView::OnShowVerticalTabsPrefChanged() {
  UpdateFloatingStateForBrowserMode();
  UpdateLayout();

  if (!VerticalTabController::FromBrowser(browser_)
           ->ShouldShowBraveVerticalTabs() &&
      state_ == State::kFloating) {
    mouse_enter_timer_.Stop();
    SetState(State::kCollapsed);
  }

  UpdateBorder();
}

void BraveVerticalTabStripRegionView::OnBrowserPanelsMoved() {
  UpdateBorder();
  PreferredSizeChanged();
}

void BraveVerticalTabStripRegionView::UpdateLayout() {
  if (auto* coordinator = GetPlacementCoordinator(browser_view_)) {
    coordinator->UpdatePlacement();
  }

  bool vertical_tabs = VerticalTabController::FromBrowser(browser_)
                           ->ShouldShowBraveVerticalTabs();
  auto layout_orientation = vertical_tabs
                                ? views::LayoutOrientation::kVertical
                                : views::LayoutOrientation::kHorizontal;

  if (vertical_tabs) {
    ReorderChildView(resize_area_, children().size() - 1);
  }

  static_cast<views::FlexLayout*>(original_region_view_->GetLayoutManager())
      ->SetOrientation(layout_orientation);

  UpdateNewTabButtonVisibility();

  PreferredSizeChanged();
  DeprecatedLayoutImmediately();
}

void BraveVerticalTabStripRegionView::OnThemeChanged() {
  View::OnThemeChanged();

  UpdateBorder();
}

void BraveVerticalTabStripRegionView::OnMouseExited(
    const ui::MouseEvent& event) {
  // This can be called when context menu is shown even if mouse is hovered
  // over vertical tab strip area. Don't want to collapse in that case.
  if (IsMouseHovered()) {
    return;
  }

  mouse_enter_timer_.Stop();
  if (state_ == State::kFloating) {
    ScheduleCollapseTimer();
  }
}

void BraveVerticalTabStripRegionView::OnMouseEntered(
    const ui::MouseEvent& event) {
  OnMouseEntered();
}

void BraveVerticalTabStripRegionView::OnMouseEntered() {
  if (!IsFloatingVerticalTabsEnabled()) {
    return;
  }

  // During tab dragging, this could be already expanded.
  if (state_ == State::kExpanded) {
    return;
  }

  mouse_exit_timer_.Stop();
  ScheduleFloatingModeTimer();
}

void BraveVerticalTabStripRegionView::HandleMouseEvent(
    const gfx::PointF& point_in_screen) {
  if (ShowVerticalTabStripOnMouseOver(point_in_screen)) {
    return;
  }

  CollapseVerticalTabStripOnMouseOut(point_in_screen);
}

bool BraveVerticalTabStripRegionView::ShowVerticalTabStripOnMouseOver(
    const gfx::PointF& point_in_screen) {
  if (!IsFloatingVerticalTabsEnabled()) {
    return false;
  }

  // If already expanded, no need to show on mouse over.
  if (state_ == State::kExpanded || state_ == State::kFloating) {
    return false;
  }

  gfx::RectF mouse_event_detect_bounds(
      BraveBrowserView::From(BrowserView::GetBrowserViewForBrowser(browser_))
          ->GetBoundingBoxInScreenForMouseOverHandling());

  constexpr int kHotCornerWidth = 7;
  const int inset = mouse_event_detect_bounds.width() - kHotCornerWidth;
  if (*vertical_tab_on_right_) {
    mouse_event_detect_bounds.Inset(gfx::InsetsF::TLBR(0, inset, 0, 0));
  } else {
    mouse_event_detect_bounds.Inset(gfx::InsetsF::TLBR(0, 0, 0, inset));
  }

  if (mouse_event_detect_bounds.Contains(point_in_screen)) {
    OnMouseEntered();
    return true;
  }

  return false;
}

void BraveVerticalTabStripRegionView::CollapseVerticalTabStripOnMouseOut(
    const gfx::PointF& point_in_screen) {
  if (state_ != State::kFloating) {
    return;
  }

  if (gfx::RectF(GetBoundsInScreen()).Contains(point_in_screen)) {
    return;
  }

  mouse_enter_timer_.Stop();
  ScheduleCollapseTimer();
}

void BraveVerticalTabStripRegionView::OnMousePressedInTree() {
  if (IsFloatingVerticalTabsEnabled()) {
    return;
  }

  if (!mouse_enter_timer_.IsRunning()) {
    return;
  }

  // Restart timer when a user presses something. We consider the mouse press
  // event as the case where the user explicitly knows what they're going to do.
  // In this case, expanding vertical tabs could distract them. So we try
  // resetting the timer.
  mouse_enter_timer_.Stop();
  ScheduleFloatingModeTimer();
}

void BraveVerticalTabStripRegionView::OnBoundsChanged(
    const gfx::Rect& previous_bounds) {
  if (!VerticalTabController::FromBrowser(browser_)
           ->ShouldShowBraveVerticalTabs()) {
    return;
  }

  if (previous_bounds.size() != size()) {
    if (GetAvailableWidthForTabContainer() != tab_strip()->width()) {
      // During/After the drag and drop session, tab strip container might have
      // ignored Layout() request. As the container bounds changed, we should
      // force it to layout.
      // https://github.com/brave/brave-browser/issues/29941
      tab_strip()->tab_container_->InvalidateIdealBounds();
      tab_strip()->tab_container_->CompleteAnimationAndLayout();
    }
  }
}

void BraveVerticalTabStripRegionView::OnResize(int resize_amount,
                                               bool done_resizing) {
  CHECK_NE(state_, State::kCollapsed);

  gfx::Rect bounds_in_screen = GetLocalBounds();
  views::View::ConvertRectToScreen(this, &bounds_in_screen);

  auto cursor_position = display::Screen::Get()->GetCursorScreenPoint().x();
  if (!resize_offset_.has_value()) {
    resize_offset_ =
        (*vertical_tab_on_right_ ? bounds_in_screen.x() - cursor_position
                                 : cursor_position - bounds_in_screen.right());
  }
  // Note that we're not using |resize_amount|. The variable is offset from
  // the initial point, it grows bigger and bigger.
  auto dest_width =
      (*vertical_tab_on_right_ ? bounds_in_screen.right() - cursor_position
                               : cursor_position - bounds_in_screen.x()) -
      *resize_offset_ - GetInsets().width();
  // Passed |true| but it doesn't have any meaning becuase we always use same
  // width.
  dest_width = std::clamp(
      dest_width,
#if BUILDFLAG(IS_BRAVE_ORIGIN_BRANDED)
      kOriginSidebarMinimumWidth, kOriginSidebarMaximumWidth
#else
      tab_style_->GetPinnedWidth(/*is_split*/ true) * 3,
      tab_style_->GetStandardWidth(/*is_split*/ true) * 2
#endif
  );
  if (done_resizing) {
    resize_offset_ = std::nullopt;
  }

  if (expanded_width_ == dest_width) {
    if (done_resizing) {
      FinalizeOriginContentsResize();
    }
    return;
  }

  // When mouse goes toward web contents area, the cursor could have been
  // changed to the normal cursor. Reset it resize cursor.
  GetWidget()->SetCursor(ui::Cursor(ui::mojom::CursorType::kEastWestResize));

  if (width_animation_.is_animating()) {
    width_animation_.Stop();
    width_animation_.Reset(state_ == State::kCollapsed ? 0 : 1);
  }

  SetExpandedWidth(dest_width);
  if (done_resizing) {
    FinalizeOriginContentsResize();
  }
}

void BraveVerticalTabStripRegionView::AnimationProgressed(
    const gfx::Animation* animation) {
  PreferredSizeChanged();
}

void BraveVerticalTabStripRegionView::AnimationEnded(
    const gfx::Animation* animation) {
  PreferredSizeChanged();
  FinalizeOriginContentsResize();

  if (state_ == State::kCollapsed) {
    OnCollapseAnimationEnded();
  }
}

void BraveVerticalTabStripRegionView::UpdateNewTabButtonVisibility() {
  const bool is_vertical_tabs = VerticalTabController::FromBrowser(browser_)
                                    ->ShouldShowBraveVerticalTabs();
  auto* original_ntb = original_region_view_->new_tab_button();
  original_ntb->SetVisible(!is_vertical_tabs);
#if BUILDFLAG(IS_BRAVE_ORIGIN_BRANDED)
  new_tab_button_->SetVisible(false);
  separator_->SetVisible(false);
#else
  new_tab_button_->SetVisible(is_vertical_tabs);
  separator_->SetVisible(is_vertical_tabs);
#endif
}

int BraveVerticalTabStripRegionView::GetTabStripViewportMaxHeight() const {
  // Don't depend on |contents_view_|'s current height. It could be bigger than
  // the actual viewport height.
#if BUILDFLAG(IS_BRAVE_ORIGIN_BRANDED)
  return std::max(0, GetContentsBounds().height() - kOriginPageListTop -
                         kOriginPageListFooterHeight);
#else
  return GetContentsBounds().height() -
         (separator_->height() + tabs::kMarginForVerticalTabContainers) -
         new_tab_button_->height();
#endif
}

void BraveVerticalTabStripRegionView::ResetExpandedWidth() {
  auto* prefs = browser_->GetProfile()->GetPrefs();
  prefs->ClearPref(brave_tabs::kVerticalTabsExpandedWidth);

  PreferredSizeChanged();
}

void BraveVerticalTabStripRegionView::UpdateBorder() {
#if BUILDFLAG(IS_BRAVE_ORIGIN_BRANDED)
  // The Origin panel is one continuous surface. Any border here becomes a
  // visible divider between the 250 px page list and the web canvas.
  SetBorder(nullptr);
  PreferredSizeChanged();
#else
  auto show_visible_border = [&]() {
    // The color provider might not be available during initialization.
    if (!GetColorProvider()) {
      return false;
    }

    if (!BraveBrowserView::ShouldUseBraveWebViewRoundedCornersForContents(
            browser_)) {
      return true;
    }

    // Only show the border if the vertical tabs are enabled and in floating
    // mode, and the tabstrip is hovered.
    return VerticalTabController::FromBrowser(browser_)
               ->ShouldShowBraveVerticalTabs() &&
           state_ == State::kFloating;
  };

  // At this point |sidebar_side_| needs to be initialized.
  CHECK(!sidebar_side_.GetPrefName().empty());

  // If the sidebar is on the same side as the vertical tab strip, we shouldn't
  // take away the margin on the vertical tabs, because the sidebar will be
  // between it and the web_contents.
  bool is_on_right =
      !vertical_tab_on_right_.GetPrefName().empty() && *vertical_tab_on_right_;
  bool sidebar_on_same_side = sidebar_side_.GetValue() == is_on_right;

  gfx::Insets border_insets;
  if (is_on_right) {
    border_insets.set_left(kBorderThickness);
  } else {
    border_insets.set_right(kBorderThickness);
  }

  // When show vertical tab's border line, vertical tab can have its whole
  // padding.
  if (show_visible_border()) {
    SetBorder(views::CreateSolidSidedBorder(
        border_insets,
        GetColorProvider()->GetColor(kColorBraveVerticalTabSeparator)));
  } else {
    // When vertical tab's border line is not shown, vertical tab should have
    // only half of its padding because contents has half or margin to draw its
    // shadow.
    if (!sidebar_on_same_side) {
      if (is_on_right) {
        border_insets.set_left(-(tabs::kMarginForVerticalTabContainers / 2));
      } else {
        border_insets.set_right(-(tabs::kMarginForVerticalTabContainers / 2));
      }
    }
    SetBorder(views::CreateEmptyBorder(border_insets));
  }

  PreferredSizeChanged();
#endif
}

void BraveVerticalTabStripRegionView::OnCollapsedPrefChanged() {
  if (!expanded_state_per_window_pref_.GetPrefName().empty() &&
      *expanded_state_per_window_pref_) {
    // On creation(when expanded_state_per_window_pref_ is empty), we set the
    // default state based on the `collapsed_pref_` even if the
    // `expanded_state_per_window_pref_` is set.
    return;
  }

  SetState(collapsed_pref_.GetValue() ? State::kCollapsed : State::kExpanded);
}

void BraveVerticalTabStripRegionView::OnFloatingModePrefChanged() {
  if (!IsFloatingVerticalTabsEnabled()) {
    if (state_ == State::kFloating) {
      SetState(State::kCollapsed);
    }
    return;
  }

  if (IsMouseHovered()) {
    ScheduleFloatingModeTimer();
  }
}

void BraveVerticalTabStripRegionView::OnExpandedStatePerWindowPrefChanged() {
  OnCollapsedPrefChanged();
  OnExpandedWidthPrefChanged();
}

void BraveVerticalTabStripRegionView::
    OnHideComopletelyWhenCollapsedPrefChanged() {
  OnFloatingModePrefChanged();
  if (state_ == State::kCollapsed) {
    // When setting is turned on/off, we should make sure vertical tab strip is
    // getting hidden/shown.
    SetVisible(!VerticalTabController::FromBrowser(browser_)
                    ->ShouldHideVerticalTabsCompletelyWhenCollapsed());
  }

  // Call after setting visibility as region view's visibility is referred when
  // updating widget bounds at
  // BraveVerticalTabStripContainerView::UpdateVerticalTabBounds().
  PreferredSizeChanged();
}

void BraveVerticalTabStripRegionView::OnShowToggleButtonPrefChanged() {
  if (!show_toggle_button_pref_.GetValue()) {
#if BUILDFLAG(IS_BRAVE_ORIGIN_BRANDED)
    // Origin always exposes its native panel control. Ignore a legacy Brave
    // preference that used to hide that control rather than making a fully
    // collapsed sidebar impossible to restore.
#else
    // There is no other way to expand collapsed vertical tabs when the
    // toggle button is hidden, so force the base/resting state to collapsed.
    collapsed_pref_.SetValue(true);
    SetState(State::kCollapsed);
#endif
  }

  // Floating mode is forced on when the toggle button is hidden; make sure
  // this state change is applied immediately.
  OnFloatingModePrefChanged();
}

void BraveVerticalTabStripRegionView::OnExpandedWidthPrefChanged() {
  if (!expanded_state_per_window_pref_.GetPrefName().empty() &&
      *expanded_state_per_window_pref_) {
    // On creation(when expanded_state_per_window_pref_ is empty), we set the
    // default state based on the `expanded_width_pref_` even if the
    // `expanded_state_per_window_pref_` is set.
    return;
  }

  SetExpandedWidth(*expanded_width_pref_);
}

gfx::Size BraveVerticalTabStripRegionView::GetPreferredSizeForState(
    State state,
    bool include_border,
    bool ignore_animation) const {
  if (!VerticalTabController::FromBrowser(browser_)
           ->ShouldShowBraveVerticalTabs()) {
    return {};
  }

  if (IsTabFullscreen()) {
    return {};
  }

  return {GetPreferredWidthForState(state, include_border, ignore_animation),
          View::CalculatePreferredSize({}).height()};
}

int BraveVerticalTabStripRegionView::GetPreferredWidthForState(
    State state,
    bool include_border,
    bool ignore_animation) const {
  auto calculate_expanded_width = [&]() {
    return *expanded_width_pref_ + (include_border ? GetInsets().width() : 0);
  };

  auto calculate_collapsed_width = [&]() {
#if BUILDFLAG(IS_BRAVE_ORIGIN_BRANDED)
    return kOriginWorkspaceRailWidth +
           (include_border ? GetInsets().width() : 0);
#else
    if (IsFloatingEnabledForBrowserMode()) {
      // In this case, vertical tab strip should be invisible but show up when
      // mouse hovers.
      return 0;
    }

    if (VerticalTabController::FromBrowser(browser_)
            ->ShouldHideVerticalTabsCompletelyWhenCollapsed()) {
      return 0;
    }

    return tabs::kVerticalTabMinWidth +
           tabs::kMarginForVerticalTabContainers * 2 +
           (include_border ? GetInsets().width() : 0);
#endif
  };

  if (!ignore_animation && width_animation_.is_animating()) {
    int width = gfx::Tween::IntValueBetween(width_animation_.GetCurrentValue(),
                                            calculate_collapsed_width(),
                                            calculate_expanded_width());
#if BUILDFLAG(IS_MAC)
    // When region view's width is zero, widget gets hidden by
    // BraveVerticalTabStripContainerView::UpdateVerticalTabBounds().
    // Then, width animation seems not working properly on macOS.
    // To avoid widget getting hidden, we set a minimum width when
    // animation goes for showing.
    constexpr int kMinShowWidth = 3;
    if (width_animation_.IsShowing() && width < kMinShowWidth) {
      width = kMinShowWidth;
    }
#endif
    return width;
  }

  if (state == State::kExpanded || state == State::kFloating) {
    return calculate_expanded_width();
  }

  CHECK_EQ(state, State::kCollapsed) << "If a new state was added, "
                                     << __FUNCTION__ << " should be revisited.";
  return calculate_collapsed_width();
}

bool BraveVerticalTabStripRegionView::IsFloatingVerticalTabsEnabled() const {
#if BUILDFLAG(IS_BRAVE_ORIGIN_BRANDED)
  // A manually hidden Origin sidebar stays hidden until the toolbar control or
  // keyboard shortcut restores it. Focus/fullscreen mode can still request a
  // transient floating state through IsFloatingEnabledForBrowserMode().
  return IsFloatingEnabledForBrowserMode();
#else
  return IsFloatingEnabledForBrowserMode() ||
         VerticalTabController::FromBrowser(browser_)
             ->IsFloatingVerticalTabsEnabled() ||
         VerticalTabController::FromBrowser(browser_)
             ->ShouldHideVerticalTabsCompletelyWhenCollapsed();
#endif
}

bool BraveVerticalTabStripRegionView::IsFloatingEnabledForBrowserFullscreen()
    const {
  return IsBrowserFullscren() && !ShouldShowVerticalTabsInBrowserFullscreen();
}

bool BraveVerticalTabStripRegionView::IsFloatingEnabledForBrowserMode() const {
  return IsFloatingEnabledForBrowserFullscreen() ||
         IsFocusModeEnabled(browser_);
}

void BraveVerticalTabStripRegionView::UpdateFloatingStateForBrowserMode() {
  if (!VerticalTabController::FromBrowser(browser_)
           ->ShouldShowBraveVerticalTabs()) {
    return;
  }

  if (IsFloatingEnabledForBrowserMode()) {
    // When transitioning into floating mode due to the current browser mode,
    // save the current state so that it can be restored when floating is no
    // longer required.
    if (!floating_restore_state_) {
      floating_restore_state_ = state_;
      width_animation_.Stop();
      SetVisible(false);
      SetState(State::kCollapsed);
    }
  } else if (floating_restore_state_) {
    // When exiting floating mode based on the current browser mode, restore the
    // pref-based expanded/collapsed state.
    const State restore_state = floating_restore_state_.value();
    floating_restore_state_.reset();
    SetVisible(true);
    SetState(restore_state);
  }

  // This method is called whenever browser mode(fullscreen or focus mode) is
  // changed. Even floating state is not changed, host view's preferred size
  // should be updated. Notify to VerticalTabStripContainerView to do it.
  PreferredSizeChanged();
}

void BraveVerticalTabStripRegionView::ScheduleFloatingModeTimer() {
  if (mouse_events_for_test_) {
    SetState(State::kFloating);
    return;
  }

  if (mouse_enter_timer_.IsRunning()) {
    return;
  }

  if (state_ == State::kCollapsed) {
    auto get_expand_delay = []() {
      constexpr int kDefaultDelay = 0;
      auto* cmd_line = base::CommandLine::ForCurrentProcess();
      if (!cmd_line->HasSwitch(tabs::switches::kVerticalTabExpandDelaySwitch)) {
        return kDefaultDelay;
      }

      auto delay_string = cmd_line->GetSwitchValueASCII(
          tabs::switches::kVerticalTabExpandDelaySwitch);

      int override_delay = 0;
      if (delay_string.empty() ||
          !base::StringToInt(delay_string, &override_delay)) {
        return kDefaultDelay;
      }

      return override_delay;
    };

    const auto delay = get_expand_delay();
    if (delay == 0) {
      // If the delay is 0, we should expand immediately.
      SetState(State::kFloating);
      return;
    }

    mouse_enter_timer_.Start(
        FROM_HERE, base::Milliseconds(delay),
        base::BindOnce(&BraveVerticalTabStripRegionView::SetState,
                       base::Unretained(this), State::kFloating));
  }
}

void BraveVerticalTabStripRegionView::ScheduleCollapseTimer() {
  if (state_ != State::kFloating) {
    return;
  }

  if (mouse_exit_timer_.IsRunning()) {
    return;
  }

  auto get_collapse_delay = []() {
    constexpr int kDefaultDelay = 0;
    auto* cmd_line = base::CommandLine::ForCurrentProcess();
    if (!cmd_line->HasSwitch(tabs::switches::kVerticalTabCollapseDelaySwitch)) {
      return kDefaultDelay;
    }

    auto delay_string = cmd_line->GetSwitchValueASCII(
        tabs::switches::kVerticalTabCollapseDelaySwitch);

    int override_delay = 0;
    if (delay_string.empty() ||
        !base::StringToInt(delay_string, &override_delay)) {
      return kDefaultDelay;
    }

    return override_delay;
  };

  const auto delay = get_collapse_delay();
  if (delay == 0) {
    // If the delay is 0, we should collapse immediately.
    SetState(State::kCollapsed);
    return;
  }

  mouse_exit_timer_.Start(
      FROM_HERE, base::Milliseconds(delay),
      base::BindOnce(&BraveVerticalTabStripRegionView::SetState,
                     base::Unretained(this), State::kCollapsed));
}

#if !BUILDFLAG(IS_MAC)
std::u16string BraveVerticalTabStripRegionView::GetShortcutTextForNewTabButton(
    BrowserView* browser_view) {
  if (ui::Accelerator new_tab_accelerator;
      browser_view->GetAcceleratorForCommandId(IDC_NEW_TAB,
                                               &new_tab_accelerator)) {
    return new_tab_accelerator.GetShortcutText();
  }

  return {};
}
#endif

void BraveVerticalTabStripRegionView::OnCollapseAnimationEnded() {
  CHECK_EQ(state_, State::kCollapsed);

  if (IsFloatingEnabledForBrowserMode() ||
      VerticalTabController::FromBrowser(browser_)
          ->ShouldHideVerticalTabsCompletelyWhenCollapsed()) {
    // When the animation ends, we should hide the vertical tab strip as we
    // don't want the tabstrip to be visible partially. This view only takes a
    // little width and watches mouse movement to expand itself.
    SetVisible(false);
  }
}

bool BraveVerticalTabStripRegionView::IsMenuShowing() const {
  return menu_runner_ && menu_runner_->IsRunning();
}

// Show context menu in unobscured area.
void BraveVerticalTabStripRegionView::ShowContextMenuForViewImpl(
    views::View* source,
    const gfx::Point& p,
    ui::mojom::MenuSourceType source_type) {
#if BUILDFLAG(IS_WIN)
  // Use same context menu of horizontal tab's titlebar.
  views::ShowSystemMenuAtScreenPixelLocation(views::HWNDForView(browser_view_),
                                             p);
#else
  if (IsMenuShowing()) {
    return;
  }

  menu_runner_ = std::make_unique<views::MenuRunner>(
      browser_view_->browser_widget()->GetSystemMenuModel(),
      views::MenuRunner::HAS_MNEMONICS | views::MenuRunner::CONTEXT_MENU,
      base::BindRepeating(&BraveVerticalTabStripRegionView::OnMenuClosed,
                          base::Unretained(this)));
  menu_runner_->RunMenuAt(source->GetWidget(), nullptr,
                          gfx::Rect(p, gfx::Size(0, 0)),
                          views::MenuAnchorPosition::kTopLeft, source_type);
#endif
}

void BraveVerticalTabStripRegionView::OnMenuClosed() {
  menu_runner_.reset();
}

void BraveVerticalTabStripRegionView::OnBrowserClosing(
    BrowserWindowInterface* browser) {
  // Resetting the context menu at this stage, as `SystemMenuModelBuilder` is
  // destroyed during `browser_widget_.reset()`, so we want to tear down our
  // menu runner before that happens.
  menu_runner_.reset();
}

BEGIN_METADATA(BraveVerticalTabStripRegionView)
END_METADATA
