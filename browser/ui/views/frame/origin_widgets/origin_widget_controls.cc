/* Copyright (c) 2026 The Brave Authors. All rights reserved.
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at https://mozilla.org/MPL/2.0/. */

#include "brave/browser/ui/views/frame/origin_widgets/origin_widget_controls.h"

#include <algorithm>
#include <utility>

#include "brave/browser/ui/views/frame/origin_widgets/origin_widget_style.h"
#include "cc/paint/paint_flags.h"
#include "ui/base/metadata/metadata_impl_macros.h"
#include "ui/gfx/canvas.h"
#include "ui/gfx/image/image_skia.h"
#include "ui/gfx/paint_vector_icon.h"
#include "ui/views/animation/ink_drop.h"

namespace {

constexpr int kControlCornerRadius = 7;

}  // namespace

OriginWidgetIconButton::OriginWidgetIconButton(PressedCallback callback,
                                               const gfx::VectorIcon& icon,
                                               const std::u16string& tooltip,
                                               int button_size,
                                               int icon_size)
    : Button(std::move(callback)), icon_(&icon), icon_size_(icon_size) {
  SetPreferredSize(gfx::Size(button_size, button_size));
  SetTooltipAndAccessibleName(tooltip);
  views::InkDrop::Get(this)->SetMode(views::InkDropHost::InkDropMode::OFF);
}

OriginWidgetIconButton::~OriginWidgetIconButton() = default;

void OriginWidgetIconButton::SetVectorIcon(const gfx::VectorIcon& icon) {
  if (icon_ == &icon) {
    return;
  }
  icon_ = &icon;
  SchedulePaint();
}

void OriginWidgetIconButton::SetTooltipAndAccessibleName(
    const std::u16string& text) {
  SetTooltipText(text);
  SetAccessibleName(text);
}

void OriginWidgetIconButton::SetPrimary(bool primary) {
  if (primary_ == primary) {
    return;
  }
  primary_ = primary;
  SchedulePaint();
}

void OriginWidgetIconButton::OnPaintBackground(gfx::Canvas* canvas) {
  const bool dark = origin_style::IsDark(this);
  cc::PaintFlags flags;
  flags.setAntiAlias(true);
  flags.setStyle(cc::PaintFlags::kFill_Style);

  if (primary_) {
    flags.setColor(dark ? SkColorSetRGB(0xF5, 0xF5, 0xF6)
                        : SkColorSetRGB(0x18, 0x1D, 0x27));
    canvas->DrawCircle(gfx::PointF(width() / 2.0f, height() / 2.0f),
                       width() / 2.0f, flags);
    return;
  }
  if (GetState() != STATE_HOVERED && GetState() != STATE_PRESSED) {
    return;
  }
  flags.setColor(origin_style::HoverFill(dark));
  canvas->DrawRoundRect(gfx::RectF(GetLocalBounds()), kControlCornerRadius,
                        flags);
}

void OriginWidgetIconButton::PaintButtonContents(gfx::Canvas* canvas) {
  const bool dark = origin_style::IsDark(this);
  SkColor color;
  if (primary_) {
    // Inverted against the filled disc painted by the background.
    color = dark ? SkColorSetRGB(0x0A, 0x0D, 0x12)
                 : SkColorSetRGB(0xFF, 0xFF, 0xFF);
  } else if (GetState() == STATE_HOVERED || GetState() == STATE_PRESSED) {
    color = origin_style::PrimaryText(dark);
  } else {
    color = origin_style::SecondaryText(dark);
  }
  const gfx::ImageSkia image =
      gfx::CreateVectorIcon(*icon_, icon_size_, color);
  canvas->DrawImageInt(image, (width() - image.width()) / 2,
                       (height() - image.height()) / 2);
}

void OriginWidgetIconButton::OnThemeChanged() {
  Button::OnThemeChanged();
  SchedulePaint();
}

void OriginWidgetIconButton::StateChanged(ButtonState old_state) {
  Button::StateChanged(old_state);
  SchedulePaint();
}

BEGIN_METADATA(OriginWidgetIconButton)
END_METADATA

OriginWidgetSoundButton::OriginWidgetSoundButton(PressedCallback callback,
                                                 int button_size)
    : Button(std::move(callback)) {
  SetPreferredSize(gfx::Size(button_size, button_size));
  views::InkDrop::Get(this)->SetMode(views::InkDropHost::InkDropMode::OFF);
  SetMuted(false);
}

OriginWidgetSoundButton::~OriginWidgetSoundButton() = default;

void OriginWidgetSoundButton::SetMuted(bool muted) {
  muted_ = muted;
  const std::u16string text = muted ? u"Unmute" : u"Mute";
  SetTooltipText(text);
  SetAccessibleName(text);
  SchedulePaint();
}

void OriginWidgetSoundButton::OnPaintBackground(gfx::Canvas* canvas) {
  if (GetState() != STATE_HOVERED && GetState() != STATE_PRESSED) {
    return;
  }
  cc::PaintFlags flags;
  flags.setAntiAlias(true);
  flags.setStyle(cc::PaintFlags::kFill_Style);
  flags.setColor(origin_style::HoverFill(origin_style::IsDark(this)));
  canvas->DrawRoundRect(gfx::RectF(GetLocalBounds()), kControlCornerRadius,
                        flags);
}

void OriginWidgetSoundButton::PaintButtonContents(gfx::Canvas* canvas) {
  const bool dark = origin_style::IsDark(this);
  const SkColor color = muted_ ? origin_style::MutedText(dark)
                               : origin_style::SecondaryText(dark);
  constexpr float kGlyph = 16.0f;
  const gfx::RectF bounds((width() - kGlyph) / 2.0f,
                          (height() - kGlyph) / 2.0f, kGlyph, kGlyph);
  origin_style::PaintSpeakerGlyph(canvas, bounds, muted_, color);
}

BEGIN_METADATA(OriginWidgetSoundButton)
END_METADATA

OriginWidgetProgressBar::OriginWidgetProgressBar() = default;
OriginWidgetProgressBar::~OriginWidgetProgressBar() = default;

void OriginWidgetProgressBar::SetFraction(double fraction) {
  fraction_ = std::clamp(fraction, 0.0, 1.0);
  SchedulePaint();
}

void OriginWidgetProgressBar::SetFillColor(SkColor color) {
  fill_color_ = color;
  SchedulePaint();
}

void OriginWidgetProgressBar::OnPaint(gfx::Canvas* canvas) {
  const bool dark = origin_style::IsDark(this);
  cc::PaintFlags track;
  track.setAntiAlias(true);
  track.setStyle(cc::PaintFlags::kFill_Style);
  track.setColor(dark ? SkColorSetARGB(0x1F, 0xFF, 0xFF, 0xFF)
                      : SkColorSetRGB(0xE9, 0xEA, 0xEB));
  canvas->DrawRoundRect(gfx::RectF(GetLocalBounds()), height() / 2.0f, track);

  if (fraction_ <= 0) {
    return;
  }
  cc::PaintFlags fill = track;
  fill.setColor(fill_color_);
  canvas->DrawRoundRect(
      gfx::RectF(0, 0, static_cast<float>(width()) * fraction_, height()),
      height() / 2.0f, fill);
}

gfx::Size OriginWidgetProgressBar::CalculatePreferredSize(
    const views::SizeBounds& available_size) const {
  return gfx::Size(0, 3);
}

BEGIN_METADATA(OriginWidgetProgressBar)
END_METADATA

namespace {

std::unique_ptr<views::Label> CreateLabel(const std::u16string& text,
                                          int size,
                                          gfx::Font::Weight weight) {
  auto label = std::make_unique<views::Label>(text);
  label->SetFontList(origin_style::ChromeFont(size, weight));
  label->SetHorizontalAlignment(gfx::ALIGN_LEFT);
  label->SetAutoColorReadabilityEnabled(false);
  label->SetElideBehavior(gfx::ELIDE_TAIL);
  return label;
}

}  // namespace

std::unique_ptr<views::Label> CreateOriginWidgetTitleLabel(
    const std::u16string& text) {
  return CreateLabel(text, 12, gfx::Font::Weight::MEDIUM);
}

std::unique_ptr<views::Label> CreateOriginWidgetSubtitleLabel(
    const std::u16string& text) {
  return CreateLabel(text, 11, gfx::Font::Weight::NORMAL);
}

std::unique_ptr<views::Label> CreateOriginWidgetSectionLabel(
    const std::u16string& text) {
  return CreateLabel(text, 11, gfx::Font::Weight::SEMIBOLD);
}
