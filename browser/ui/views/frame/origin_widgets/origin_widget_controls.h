/* Copyright (c) 2026 The Brave Authors. All rights reserved.
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at https://mozilla.org/MPL/2.0/. */

#ifndef BRAVE_BROWSER_UI_VIEWS_FRAME_ORIGIN_WIDGETS_ORIGIN_WIDGET_CONTROLS_H_
#define BRAVE_BROWSER_UI_VIEWS_FRAME_ORIGIN_WIDGETS_ORIGIN_WIDGET_CONTROLS_H_

#include <memory>
#include <string>

#include "base/memory/raw_ptr.h"
#include "ui/base/metadata/metadata_header_macros.h"
#include "ui/views/controls/button/button.h"
#include "ui/views/controls/label.h"
#include "ui/views/view.h"

namespace gfx {
struct VectorIcon;
}

// Small square control with the panel's hover treatment. Every inline action
// in a widget card is one of these so they share one hit target size.
class OriginWidgetIconButton : public views::Button {
  METADATA_HEADER(OriginWidgetIconButton, views::Button)

 public:
  OriginWidgetIconButton(PressedCallback callback,
                         const gfx::VectorIcon& icon,
                         const std::u16string& tooltip,
                         int button_size = 26,
                         int icon_size = 14);
  OriginWidgetIconButton(const OriginWidgetIconButton&) = delete;
  OriginWidgetIconButton& operator=(const OriginWidgetIconButton&) = delete;
  ~OriginWidgetIconButton() override;

  void SetVectorIcon(const gfx::VectorIcon& icon);
  void SetTooltipAndAccessibleName(const std::u16string& text);
  // Paints the icon filled on an accent disc instead of flat. Used for the
  // one primary transport control in a card.
  void SetPrimary(bool primary);

  // views::Button:
  void OnPaintBackground(gfx::Canvas* canvas) override;
  void PaintButtonContents(gfx::Canvas* canvas) override;
  void OnThemeChanged() override;
  void StateChanged(ButtonState old_state) override;

 private:
  raw_ptr<const gfx::VectorIcon> icon_;
  int icon_size_;
  bool primary_ = false;
};

// Mute control. Separate from OriginWidgetIconButton because the speaker and
// equalizer glyphs are drawn rather than loaded from the icon set.
class OriginWidgetSoundButton : public views::Button {
  METADATA_HEADER(OriginWidgetSoundButton, views::Button)

 public:
  explicit OriginWidgetSoundButton(PressedCallback callback,
                                   int button_size = 26);
  OriginWidgetSoundButton(const OriginWidgetSoundButton&) = delete;
  OriginWidgetSoundButton& operator=(const OriginWidgetSoundButton&) = delete;
  ~OriginWidgetSoundButton() override;

  void SetMuted(bool muted);

  // views::Button:
  void OnPaintBackground(gfx::Canvas* canvas) override;
  void PaintButtonContents(gfx::Canvas* canvas) override;

 private:
  bool muted_ = false;
};

// Flat 3px progress track. Shows nothing when the fraction is unknown, which
// is the normal state for a live stream.
class OriginWidgetProgressBar : public views::View {
  METADATA_HEADER(OriginWidgetProgressBar, views::View)

 public:
  OriginWidgetProgressBar();
  ~OriginWidgetProgressBar() override;

  // `fraction` is clamped to [0, 1].
  void SetFraction(double fraction);
  void SetFillColor(SkColor color);

  // views::View:
  void OnPaint(gfx::Canvas* canvas) override;
  gfx::Size CalculatePreferredSize(
      const views::SizeBounds& available_size) const override;

 private:
  double fraction_ = 0;
  SkColor fill_color_ = SK_ColorWHITE;
};

// Convenience builders shared by the widgets; they only set typography and
// colour so callers stay focused on layout.
std::unique_ptr<views::Label> CreateOriginWidgetTitleLabel(
    const std::u16string& text);
std::unique_ptr<views::Label> CreateOriginWidgetSubtitleLabel(
    const std::u16string& text);
std::unique_ptr<views::Label> CreateOriginWidgetSectionLabel(
    const std::u16string& text);

#endif  // BRAVE_BROWSER_UI_VIEWS_FRAME_ORIGIN_WIDGETS_ORIGIN_WIDGET_CONTROLS_H_
