// Copyright (c) 2026 The Brave Authors. All rights reserved.
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this file,
// You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef BRAVE_BROWSER_UI_VIEWS_FRAME_ORIGIN_CHROME_STYLE_H_
#define BRAVE_BROWSER_UI_VIEWS_FRAME_ORIGIN_CHROME_STYLE_H_

#include "third_party/skia/include/core/SkColor.h"
#include "ui/gfx/font.h"
#include "ui/gfx/font_list.h"

namespace views {
class View;
}

// Type and colour for Origin's native chrome, matching the sidebar's.
namespace origin_chrome_style {

inline constexpr SkColor kAccent = SkColorSetRGB(0xFB, 0x54, 0x2B);

gfx::FontList Font(int size, gfx::Font::Weight weight);

// Whether `view` is drawn on the dark shell. Defaults to dark before the view
// has a color provider, as the shell does.
bool IsDark(const views::View* view);

SkColor PrimaryText(bool dark);
SkColor SecondaryText(bool dark);
SkColor MutedText(bool dark);
// Behind a hovered row or button.
SkColor HoverFill(bool dark);
// Behind a selected row or button.
SkColor SelectedFill(bool dark);
// Hairlines between groups.
SkColor Separator(bool dark);

// The frosted card the snooze and tag pickers float over the page, as Quick
// Open does.
struct PickerColors {
  SkColor scrim;
  SkColor panel;
  SkColor panel_stroke;
  SkColor field;
  SkColor selected_row;
  SkColor hovered_row;
  SkColor primary_text;
  SkColor secondary_text;
  SkColor tertiary_text;
};
PickerColors GetPickerColors(bool dark);
inline constexpr float kPickerBlurSigma = 32.0f;
inline constexpr float kPickerBackdropQuality = 0.40f;

}  // namespace origin_chrome_style

#endif  // BRAVE_BROWSER_UI_VIEWS_FRAME_ORIGIN_CHROME_STYLE_H_
