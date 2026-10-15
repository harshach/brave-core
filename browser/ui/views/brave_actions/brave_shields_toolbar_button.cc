// Copyright (c) 2026 The Brave Authors. All rights reserved.
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this file,
// You can obtain one at https://mozilla.org/MPL/2.0/.

#include "brave/browser/ui/views/brave_actions/brave_shields_toolbar_button.h"

#include <utility>

#include "base/functional/bind.h"
#include "base/strings/string_number_conversions.h"
#include "brave/browser/ui/views/brave_actions/brave_shields_action_view.h"
#include "brave/components/brave_origin/buildflags/buildflags.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface.h"
#include "components/grit/brave_components_strings.h"
#include "ui/base/l10n/l10n_util.h"
#include "ui/base/metadata/metadata_impl_macros.h"
#include "ui/gfx/geometry/insets.h"
#include "ui/gfx/geometry/point.h"
#include "ui/gfx/geometry/skia_conversions.h"
#include "ui/views/background.h"
#include "ui/views/border.h"
#include "ui/views/controls/highlight_path_generator.h"
#include "ui/views/view_class_properties.h"

namespace {

class ShieldsToolbarHighlightPathGenerator
    : public views::HighlightPathGenerator {
  SkPath GetHighlightPath(const views::View* view) override {
    return SkPath::Rect(gfx::RectToSkRect(view->GetLocalBounds()));
  }
};

}  // namespace

BraveShieldsToolbarButton::BraveShieldsToolbarButton(
    BrowserWindowInterface* browser_window_interface,
    CreateWebUIBubbleManagerCallback create_bubble_manager_callback)
    : ToolbarButton(views::Button::PressedCallback()),
      controller_(std::make_unique<BraveShieldsActionController>(
          browser_window_interface,
          std::move(create_bubble_manager_callback))) {
  SetText(std::u16string());
  SetAccessibleName(l10n_util::GetStringUTF16(IDS_BRAVE_SHIELDS));
  // Use the same element identifier as BraveShieldsActionView so that we can
  // find either of them in the BrowserElementsViews.
  SetProperty(views::kElementIdentifierKey,
              BraveShieldsActionView::kShieldsActionIcon);
  SetBorder(nullptr);

  // This control lives in the browser toolbar rather than inside the location
  // bar. Keep ToolbarButton's standard press path so native mouse and
  // accessibility activation both dispatch through Button::NotifyClick(). The
  // controller already owns the open/close toggle for its WebUI bubble.
  SetCallback(base::BindRepeating(&BraveShieldsToolbarButton::ButtonPressed,
                                  weak_ptr_factory_.GetWeakPtr()));

  controller_->SetOnStateChanged(
      base::BindRepeating(&BraveShieldsToolbarButton::OnControllerStateChanged,
                          weak_ptr_factory_.GetWeakPtr()));
  controller_->SetIconStyle(
      BraveShieldsActionController::IconStyle::kWebAppTitleBar);
  controller_->SetAnchorView(this);

  views::HighlightPathGenerator::Install(
      this, std::make_unique<ShieldsToolbarHighlightPathGenerator>());

  Update();
}

BraveShieldsToolbarButton::~BraveShieldsToolbarButton() = default;

void BraveShieldsToolbarButton::SetOriginTitleBarStyle() {
#if BUILDFLAG(IS_BRAVE_ORIGIN_BRANDED)
  origin_title_bar_style_ = true;
  controller_->SetIconStyle(
      BraveShieldsActionController::IconStyle::kOriginTitleBar);
  SetImageLabelSpacing(5);
  SetHorizontalAlignment(gfx::ALIGN_CENTER);
  SetProperty(views::kMarginsKey, gfx::Insets());
  Update();
#endif
}

void BraveShieldsToolbarButton::ButtonPressed() {
  controller_->OnButtonPressed();
}

void BraveShieldsToolbarButton::OnControllerStateChanged() {
  Update();
}

void BraveShieldsToolbarButton::Update() {
  if (origin_title_bar_style_) {
    constexpr SkColor kEnabledColor = SkColorSetRGB(0xFB, 0x54, 0x2B);
    constexpr SkColor kDisabledColor = SkColorSetRGB(0x6B, 0x6E, 0x75);
    const bool enabled = controller_->IsShieldsEnabled();
    const int blocked = controller_->GetTotalBlockedCount();
    std::u16string count;
    if (enabled) {
      count = blocked > 999 ? u"999+" : base::NumberToString16(blocked);
    }
    SetText(count);
    SetAccessibleName(l10n_util::GetStringUTF16(IDS_BRAVE_SHIELDS));
    SetEnabledTextColors(enabled ? kEnabledColor : kDisabledColor);
    SetBackground(views::CreateRoundedRectBackground(
        enabled ? SkColorSetARGB(0x2B, 0xFB, 0x54, 0x2B)
                : SkColorSetARGB(0x0D, 0xFF, 0xFF, 0xFF),
        8));
    SetBorder(views::CreateEmptyBorder(gfx::Insets::VH(0, 8)));
    const int digit_width = count.size() > 2u ? 12 : count.size() > 1u ? 6 : 0;
    SetPreferredSize(gfx::Size(enabled ? 44 + digit_width : 30, 28));
  }
  controller_->RefreshButtonImages(this);
  PreferredSizeChanged();
}

views::Widget* BraveShieldsToolbarButton::GetBubbleWidget() {
  return controller_->GetBubbleWidget();
}

std::u16string BraveShieldsToolbarButton::GetRenderedTooltipText(
    const gfx::Point& p) const {
  return controller_->GetTooltipText();
}

void BraveShieldsToolbarButton::OnThemeChanged() {
  ToolbarButton::OnThemeChanged();
  // Bitmap icon already encodes state; re-resolve image from controller for
  // any color provider updates.
  Update();
}

BEGIN_METADATA(BraveShieldsToolbarButton)
END_METADATA
