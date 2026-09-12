/* Copyright (c) 2026 The Brave Authors. All rights reserved.
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at https://mozilla.org/MPL/2.0/. */

#ifndef BRAVE_BROWSER_UI_VIEWS_FRAME_ORIGIN_WIDGETS_ORIGIN_WIDGET_STYLE_H_
#define BRAVE_BROWSER_UI_VIEWS_FRAME_ORIGIN_WIDGETS_ORIGIN_WIDGET_STYLE_H_

#include "third_party/skia/include/core/SkColor.h"
#include "ui/gfx/font_list.h"
#include "ui/gfx/geometry/rect_f.h"

namespace gfx {
class Canvas;
}

namespace views {
class View;
}

// Shared chrome styling for Origin's own surfaces. The widgets panel is meant
// to read as a mirror of the spaces panel, so both draw from these values
// rather than each re-deriving them.
namespace origin_style {

inline constexpr SkColor kAccent = SkColorSetRGB(0xFB, 0x54, 0x2B);
inline constexpr int kCardCornerRadius = 12;
inline constexpr int kPanelHeaderHeight = 58;
inline constexpr int kPanelContentTop = 66;

// Matches the platform's UI font, which Origin uses in place of the default
// Chromium chrome font.
gfx::FontList ChromeFont(int size, gfx::Font::Weight weight);

// True when the browser chrome is currently painted dark. Views without a
// color provider yet are treated as dark, matching the Origin default.
bool IsDark(const views::View* view);

SkColor CardBackground(bool dark);
SkColor CardBorder(bool dark);
SkColor PrimaryText(bool dark);
SkColor SecondaryText(bool dark);
SkColor MutedText(bool dark);
SkColor HoverFill(bool dark);

// Sound glyphs, drawn rather than themed from a vector icon because they are
// used at 10-14px where a scaled icon loses its shape. Both fit themselves to
// `bounds`, which is expected to be square.
void PaintSpeakerGlyph(gfx::Canvas* canvas,
                       const gfx::RectF& bounds,
                       bool muted,
                       SkColor color);
// Three static bars of uneven height. The design animates these; a static
// silhouette reads the same at this size and costs no compositor frames.
void PaintEqualizerGlyph(gfx::Canvas* canvas,
                         const gfx::RectF& bounds,
                         SkColor color);

}  // namespace origin_style

#endif  // BRAVE_BROWSER_UI_VIEWS_FRAME_ORIGIN_WIDGETS_ORIGIN_WIDGET_STYLE_H_
