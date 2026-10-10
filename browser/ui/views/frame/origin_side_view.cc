// Copyright (c) 2026 The Brave Authors. All rights reserved.
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this file,
// You can obtain one at https://mozilla.org/MPL/2.0/.

#include "brave/browser/ui/views/frame/origin_side_view.h"

#include <algorithm>
#include <utility>

#include "base/check.h"
#include "base/functional/bind.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/utf_string_conversions.h"
#include "brave/browser/ui/views/frame/origin_chrome_style.h"
#include "brave/components/vector_icons/vector_icons.h"
#include "cc/paint/paint_flags.h"
#include "ui/base/metadata/metadata_impl_macros.h"
#include "ui/gfx/canvas.h"
#include "ui/gfx/geometry/rect_f.h"
#include "ui/gfx/paint_vector_icon.h"
#include "ui/views/animation/ink_drop.h"
#include "ui/views/background.h"
#include "ui/views/border.h"
#include "ui/views/controls/button/image_button.h"
#include "ui/views/controls/image_view.h"
#include "ui/views/controls/label.h"
#include "ui/views/controls/resize_area.h"
#include "ui/views/layout/box_layout.h"
#include "ui/views/layout/box_layout_view.h"

namespace {

constexpr int kHeaderButtonSize = 28;
constexpr int kHeaderIconSize = 16;
constexpr int kBodyCornerRadius = 10;

size_t ModeIndex(OriginSideViewMode mode) {
  return static_cast<size_t>(mode);
}

const gfx::VectorIcon& GetModeIcon(OriginSideViewMode mode) {
  switch (mode) {
    case OriginSideViewMode::kPage:
      return kLeoBrowserSplitViewVerticalIcon;
    case OriginSideViewMode::kShortcuts:
      return kLeoKeyboardIcon;
    case OriginSideViewMode::kDownloads:
      return kLeoDownloadIcon;
    case OriginSideViewMode::kReminders:
      return kLeoAlarmIcon;
    case OriginSideViewMode::kSnoozed:
      return kLeoSleepTimerIcon;
    case OriginSideViewMode::kTags:
      return kLeoLabelIcon;
  }
}

SkColor BodyFill(bool dark) {
  return dark ? SkColorSetRGB(0x1C, 0x1E, 0x24) : SK_ColorWHITE;
}

std::unique_ptr<views::ImageButton> MakeHeaderButton(
    views::Button::PressedCallback callback,
    const std::u16string& name) {
  auto button = std::make_unique<views::ImageButton>(std::move(callback));
  button->SetPreferredSize(gfx::Size(kHeaderButtonSize, kHeaderButtonSize));
  button->SetImageHorizontalAlignment(views::ImageButton::ALIGN_CENTER);
  button->SetImageVerticalAlignment(views::ImageButton::ALIGN_MIDDLE);
  button->SetAccessibleName(name);
  button->SetTooltipText(name);
  views::InkDrop::Get(button.get())
      ->SetMode(views::InkDropHost::InkDropMode::OFF);
  return button;
}

void StyleActionButton(views::ImageButton* button,
                       const gfx::VectorIcon& icon,
                       bool dark) {
  button->SetImageModel(
      views::Button::STATE_NORMAL,
      ui::ImageModel::FromVectorIcon(icon, origin_chrome_style::MutedText(dark),
                                     kHeaderIconSize));
  button->SetImageModel(
      views::Button::STATE_HOVERED,
      ui::ImageModel::FromVectorIcon(
          icon, origin_chrome_style::PrimaryText(dark), kHeaderIconSize));
  button->SetBackground(nullptr);
}

}  // namespace

std::u16string GetOriginSideViewModeName(OriginSideViewMode mode) {
  switch (mode) {
    case OriginSideViewMode::kPage:
      return u"Side page";
    case OriginSideViewMode::kShortcuts:
      return u"Shortcuts";
    case OriginSideViewMode::kDownloads:
      return u"Downloads";
    case OriginSideViewMode::kReminders:
      return u"Reminders";
    case OriginSideViewMode::kSnoozed:
      return u"Snoozed";
    case OriginSideViewMode::kTags:
      return u"Tags";
  }
}

// A header button for one mode. The selected mode keeps a filled square; a
// count, when there is one, sits in the corner.
class OriginSideView::ModeButton : public views::ImageButton {
  METADATA_HEADER(ModeButton, views::ImageButton)

 public:
  ModeButton(PressedCallback callback, OriginSideViewMode mode)
      : views::ImageButton(std::move(callback)), mode_(mode) {
    SetPreferredSize(gfx::Size(kHeaderButtonSize, kHeaderButtonSize));
    SetImageHorizontalAlignment(ALIGN_CENTER);
    SetImageVerticalAlignment(ALIGN_MIDDLE);
    SetAccessibleName(GetOriginSideViewModeName(mode));
    SetTooltipText(GetOriginSideViewModeName(mode));
    views::InkDrop::Get(this)->SetMode(views::InkDropHost::InkDropMode::OFF);
  }

  void SetSelected(bool selected) {
    if (selected_ == selected) {
      return;
    }
    selected_ = selected;
    UpdateImage();
    SchedulePaint();
  }

  // The side page's favicon replaces the generic icon when there is one.
  void SetFavicon(const ui::ImageModel& favicon) {
    favicon_ = favicon;
    UpdateImage();
  }

  void SetBadge(int count) {
    if (badge_ == count) {
      return;
    }
    badge_ = count;
    SchedulePaint();
  }

  // views::ImageButton:
  void StateChanged(ButtonState old_state) override {
    views::ImageButton::StateChanged(old_state);
    UpdateImage();
    SchedulePaint();
  }

  void OnThemeChanged() override {
    views::ImageButton::OnThemeChanged();
    UpdateImage();
  }

  void OnPaintBackground(gfx::Canvas* canvas) override {
    const bool dark = origin_chrome_style::IsDark(this);
    const bool hovered =
        GetState() == STATE_HOVERED || GetState() == STATE_PRESSED;
    if (!selected_ && !hovered) {
      return;
    }
    cc::PaintFlags fill;
    fill.setAntiAlias(true);
    fill.setColor(selected_ ? origin_chrome_style::SelectedFill(dark)
                            : origin_chrome_style::HoverFill(dark));
    canvas->DrawRoundRect(gfx::RectF(GetLocalBounds()), 8, fill);
  }

  void PaintButtonContents(gfx::Canvas* canvas) override {
    views::ImageButton::PaintButtonContents(canvas);
    if (badge_ <= 0) {
      return;
    }
    // A small accent dot with the count; nine and up read as "9+".
    const std::u16string text =
        badge_ > 9 ? u"9+" : base::NumberToString16(badge_);
    const gfx::FontList font =
        origin_chrome_style::Font(9, gfx::Font::Weight::BOLD);
    const int text_width = gfx::Canvas::GetStringWidth(text, font);
    const int diameter = std::max(13, text_width + 6);
    const gfx::Rect dot(width() - diameter, 0, diameter, 13);
    cc::PaintFlags fill;
    fill.setAntiAlias(true);
    fill.setColor(origin_chrome_style::kAccent);
    canvas->DrawRoundRect(gfx::RectF(dot), 6.5f, fill);
    canvas->DrawStringRectWithFlags(text, font, SK_ColorWHITE, dot,
                                    gfx::Canvas::TEXT_ALIGN_CENTER);
  }

 private:
  void UpdateImage() {
    if (mode_ == OriginSideViewMode::kPage && !favicon_.IsEmpty()) {
      SetImageModel(STATE_NORMAL, favicon_);
      return;
    }
    const bool dark = origin_chrome_style::IsDark(this);
    const bool hovered =
        GetState() == STATE_HOVERED || GetState() == STATE_PRESSED;
    const SkColor color = selected_ ? origin_chrome_style::PrimaryText(dark)
                          : hovered ? origin_chrome_style::SecondaryText(dark)
                                    : origin_chrome_style::MutedText(dark);
    SetImageModel(STATE_NORMAL,
                  ui::ImageModel::FromVectorIcon(GetModeIcon(mode_), color,
                                                 kHeaderIconSize));
  }

  const OriginSideViewMode mode_;
  bool selected_ = false;
  int badge_ = 0;
  ui::ImageModel favicon_;
};

BEGIN_METADATA(OriginSideView, ModeButton)
END_METADATA

OriginSideView::OriginSideView(Delegate* delegate) : delegate_(delegate) {
  SetNotifyEnterExitOnChild(true);

  resize_area_ = AddChildView(std::make_unique<views::ResizeArea>(this));

  header_ = AddChildView(std::make_unique<views::View>());
  auto* header_layout =
      header_->SetLayoutManager(std::make_unique<views::BoxLayout>(
          views::BoxLayout::Orientation::kHorizontal,
          gfx::Insets::TLBR(6, 4, 6, 4), 2));
  header_layout->set_cross_axis_alignment(
      views::BoxLayout::CrossAxisAlignment::kCenter);

  for (OriginSideViewMode mode : kOriginSideViewModes) {
    auto* button = header_->AddChildView(std::make_unique<ModeButton>(
        base::BindRepeating(
            [](OriginSideView* view, OriginSideViewMode mode) {
              if (view->delegate_) {
                view->delegate_->SelectSideViewMode(mode);
              }
            },
            base::Unretained(this), mode),
        mode));
    // Tools announce themselves through SetToolView(); the page is always
    // there.
    button->SetVisible(mode == OriginSideViewMode::kPage);
    mode_buttons_[ModeIndex(mode)] = button;
  }

  title_ = header_->AddChildView(std::make_unique<views::Label>());
  title_->SetHorizontalAlignment(gfx::ALIGN_LEFT);
  title_->SetElideBehavior(gfx::ELIDE_TAIL);
  title_->SetFontList(
      origin_chrome_style::Font(13, gfx::Font::Weight::SEMIBOLD));
  title_->SetBorder(views::CreateEmptyBorder(gfx::Insets::VH(0, 8)));
  header_layout->SetFlexForView(title_, 1);

  return_button_ = header_->AddChildView(
      MakeHeaderButton(base::BindRepeating(
                           [](OriginSideView* view) {
                             if (view->delegate_) {
                               view->delegate_->ReturnSidePageToMain();
                             }
                           },
                           base::Unretained(this)),
                       u"Move to the page list (.)"));
  close_page_button_ = header_->AddChildView(
      MakeHeaderButton(base::BindRepeating(
                           [](OriginSideView* view) {
                             if (view->delegate_) {
                               view->delegate_->CloseSidePage();
                             }
                           },
                           base::Unretained(this)),
                       u"Close side page"));
  close_button_ = header_->AddChildView(
      MakeHeaderButton(base::BindRepeating(
                           [](OriginSideView* view) {
                             if (view->delegate_) {
                               view->delegate_->CloseSideView();
                             }
                           },
                           base::Unretained(this)),
                       u"Hide panel (\\)"));

  // Shown in Page mode while the Space has no side page.
  auto empty_state = std::make_unique<views::BoxLayoutView>();
  empty_state->SetOrientation(views::BoxLayout::Orientation::kVertical);
  empty_state->SetMainAxisAlignment(
      views::BoxLayout::MainAxisAlignment::kCenter);
  empty_state->SetCrossAxisAlignment(
      views::BoxLayout::CrossAxisAlignment::kCenter);
  empty_state->SetBetweenChildSpacing(8);
  empty_state->SetInsideBorderInsets(gfx::Insets::VH(24, 28));
  empty_icon_ = empty_state->AddChildView(std::make_unique<views::ImageView>());
  empty_title_ = empty_state->AddChildView(
      std::make_unique<views::Label>(u"Keep a page beside this Space"));
  empty_title_->SetFontList(
      origin_chrome_style::Font(14, gfx::Font::Weight::SEMIBOLD));
  empty_body_ = empty_state->AddChildView(std::make_unique<views::Label>(
      u"Press . on a page to move it here. It stays with this Space, and "
      u". again puts it back in the list."));
  empty_body_->SetFontList(
      origin_chrome_style::Font(12, gfx::Font::Weight::NORMAL));
  empty_body_->SetMultiLine(true);
  empty_body_->SetMaximumWidth(260);
  empty_body_->SetHorizontalAlignment(gfx::ALIGN_CENTER);
  page_empty_state_ = AddChildView(std::move(empty_state));

  UpdateHeader();
}

OriginSideView::~OriginSideView() = default;

// static
gfx::Insets OriginSideView::GetWebPanelInsets() {
  return gfx::Insets::TLBR(kHeaderHeight, kGutterWidth, 0, 0);
}

void OriginSideView::SetToolView(OriginSideViewMode mode,
                                 std::unique_ptr<views::View> view) {
  CHECK_NE(mode, OriginSideViewMode::kPage);
  CHECK(!tool_views_[ModeIndex(mode)]);
  views::View* tool = AddChildView(std::move(view));
  tool->SetVisible(mode_ == mode);
  tool_views_[ModeIndex(mode)] = tool;
  mode_buttons_[ModeIndex(mode)]->SetVisible(true);
  InvalidateLayout();
}

views::View* OriginSideView::GetToolView(OriginSideViewMode mode) const {
  return tool_views_[ModeIndex(mode)];
}

void OriginSideView::SetMode(OriginSideViewMode mode) {
  if (mode_ == mode) {
    return;
  }
  mode_ = mode;
  UpdateHeader();
  InvalidateLayout();
  SchedulePaint();
}

void OriginSideView::SetSidePage(std::optional<std::u16string> title,
                                 const ui::ImageModel& favicon) {
  std::u16string new_title = title.value_or(std::u16string());
  if (title.has_value() == has_side_page_ && new_title == side_page_title_ &&
      favicon == side_page_favicon_) {
    return;
  }
  has_side_page_ = title.has_value();
  side_page_title_ = std::move(new_title);
  side_page_favicon_ = favicon;
  mode_buttons_[ModeIndex(OriginSideViewMode::kPage)]->SetFavicon(
      has_side_page_ ? favicon : ui::ImageModel());
  UpdateHeader();
  InvalidateLayout();
}

void OriginSideView::SetModeBadge(OriginSideViewMode mode, int count) {
  mode_buttons_[ModeIndex(mode)]->SetBadge(count);
}

views::ImageButton* OriginSideView::GetModeButtonForTesting(
    OriginSideViewMode mode) {
  return mode_buttons_[ModeIndex(mode)];
}

gfx::Rect OriginSideView::GetBodyBounds() const {
  gfx::Rect body = GetLocalBounds();
  body.Inset(GetWebPanelInsets());
  return body;
}

void OriginSideView::Layout(PassKey) {
  resize_area_->SetBounds(0, 0, kGutterWidth, height());
  header_->SetBounds(kGutterWidth, 0, std::max(0, width() - kGutterWidth),
                     kHeaderHeight);

  const gfx::Rect body = GetBodyBounds();
  const bool page_mode = mode_ == OriginSideViewMode::kPage;
  page_empty_state_->SetVisible(page_mode && !has_side_page_);
  page_empty_state_->SetBoundsRect(body);
  for (OriginSideViewMode mode : kOriginSideViewModes) {
    if (views::View* tool = tool_views_[ModeIndex(mode)]) {
      tool->SetVisible(mode == mode_);
      tool->SetBoundsRect(body);
    }
  }
}

void OriginSideView::OnPaintBackground(gfx::Canvas* canvas) {
  // The side page brings its own surface; tools and the empty state sit on a
  // card shaped like the page beside them.
  if (mode_ == OriginSideViewMode::kPage && has_side_page_) {
    return;
  }
  cc::PaintFlags fill;
  fill.setAntiAlias(true);
  fill.setColor(BodyFill(origin_chrome_style::IsDark(this)));
  canvas->DrawRoundRect(gfx::RectF(GetBodyBounds()), kBodyCornerRadius, fill);
}

void OriginSideView::OnThemeChanged() {
  views::View::OnThemeChanged();
  UpdateHeader();
}

void OriginSideView::OnResize(int resize_amount, bool done_resizing) {
  // The gutter is the panel's leading edge: dragging it toward the page widens
  // the panel.
  if (!resize_start_width_) {
    resize_start_width_ = width();
  }
  const int new_width = std::clamp(*resize_start_width_ - resize_amount,
                                   kMinimumWidth, kMaximumWidth);
  if (delegate_) {
    delegate_->ResizeSideView(new_width, done_resizing);
  }
  if (done_resizing) {
    resize_start_width_.reset();
  }
}

void OriginSideView::UpdateHeader() {
  const bool dark = origin_chrome_style::IsDark(this);
  for (OriginSideViewMode mode : kOriginSideViewModes) {
    mode_buttons_[ModeIndex(mode)]->SetSelected(mode == mode_);
  }

  const bool page_mode = mode_ == OriginSideViewMode::kPage;
  title_->SetText(page_mode && has_side_page_
                      ? side_page_title_
                      : GetOriginSideViewModeName(mode_));
  title_->SetEnabledColor(origin_chrome_style::PrimaryText(dark));
  return_button_->SetVisible(page_mode && has_side_page_);
  close_page_button_->SetVisible(page_mode && has_side_page_);
  StyleActionButton(return_button_, kLeoSwapHorizontalIcon, dark);
  StyleActionButton(close_page_button_, kLeoCloseIcon, dark);
  StyleActionButton(close_button_, kLeoBrowserSidebarRightIcon, dark);

  empty_icon_->SetImage(
      ui::ImageModel::FromVectorIcon(kLeoBrowserSplitViewVerticalIcon,
                                     origin_chrome_style::MutedText(dark), 28));
  empty_title_->SetEnabledColor(origin_chrome_style::PrimaryText(dark));
  empty_body_->SetEnabledColor(origin_chrome_style::SecondaryText(dark));
}

BEGIN_METADATA(OriginSideView)
END_METADATA
