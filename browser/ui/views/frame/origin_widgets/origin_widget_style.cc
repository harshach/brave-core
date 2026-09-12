/* Copyright (c) 2026 The Brave Authors. All rights reserved.
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at https://mozilla.org/MPL/2.0/. */

#include "brave/browser/ui/views/frame/origin_widgets/origin_widget_style.h"

#include "brave/browser/ui/color/brave_color_id.h"
#include "build/build_config.h"
#include "cc/paint/paint_flags.h"
#include "third_party/skia/include/core/SkPath.h"
#include "third_party/skia/include/core/SkPathBuilder.h"
#include "ui/color/color_provider.h"
#include "ui/gfx/canvas.h"
#include "ui/gfx/color_utils.h"
#include "ui/views/view.h"

namespace origin_style {

gfx::FontList ChromeFont(int size, gfx::Font::Weight weight) {
#if BUILDFLAG(IS_MAC)
  constexpr char kFamily[] = "SF Pro Text";
#elif BUILDFLAG(IS_WIN)
  constexpr char kFamily[] = "Segoe UI";
#else
  constexpr char kFamily[] = "Inter";
#endif
  return gfx::FontList({kFamily}, gfx::Font::NORMAL, size, weight);
}

bool IsDark(const views::View* view) {
  const auto* provider = view ? view->GetColorProvider() : nullptr;
  return !provider || color_utils::IsDark(provider->GetColor(
                          kColorBraveVerticalTabInactiveBackground));
}

SkColor CardBackground(bool dark) {
  return dark ? SkColorSetARGB(0x0B, 0xFF, 0xFF, 0xFF)
              : SkColorSetRGB(0xFA, 0xFA, 0xFA);
}

SkColor CardBorder(bool dark) {
  return dark ? SkColorSetARGB(0x12, 0xFF, 0xFF, 0xFF)
              : SkColorSetRGB(0xE9, 0xEA, 0xEB);
}

SkColor PrimaryText(bool dark) {
  return dark ? SkColorSetRGB(0xF5, 0xF5, 0xF6) : SkColorSetRGB(0x18, 0x1D, 0x27);
}

SkColor SecondaryText(bool dark) {
  return dark ? SkColorSetRGB(0xCE, 0xCF, 0xD2) : SkColorSetRGB(0x41, 0x46, 0x51);
}

SkColor MutedText(bool dark) {
  return dark ? SkColorSetRGB(0x7C, 0x7F, 0x86) : SkColorSetRGB(0x71, 0x76, 0x80);
}

SkColor HoverFill(bool dark) {
  return dark ? SkColorSetARGB(0x10, 0xFF, 0xFF, 0xFF)
              : SkColorSetRGB(0xF2, 0xF2, 0xF3);
}

void PaintSpeakerGlyph(gfx::Canvas* canvas,
                       const gfx::RectF& bounds,
                       bool muted,
                       SkColor color) {
  // Authored against a 16x16 box and scaled to whatever the caller asks for.
  const float scale = bounds.width() / 16.0f;
  const auto x = [&](float v) { return bounds.x() + v * scale; };
  const auto y = [&](float v) { return bounds.y() + v * scale; };

  cc::PaintFlags flags;
  flags.setAntiAlias(true);
  flags.setColor(color);
  flags.setStrokeWidth(1.2f * scale);
  flags.setStrokeCap(cc::PaintFlags::kRound_Cap);

  const SkPath speaker = SkPathBuilder()
                             .moveTo(x(7.4f), y(4.6f))
                             .lineTo(x(5.4f), y(6.2f))
                             .lineTo(x(3.8f), y(6.2f))
                             .lineTo(x(3.8f), y(9.8f))
                             .lineTo(x(5.4f), y(9.8f))
                             .lineTo(x(7.4f), y(11.4f))
                             .close()
                             .detach();

  cc::PaintFlags fill = flags;
  fill.setStyle(cc::PaintFlags::kFill_Style);
  canvas->DrawPath(speaker, fill);

  cc::PaintFlags stroke = flags;
  stroke.setStyle(cc::PaintFlags::kStroke_Style);
  if (muted) {
    canvas->DrawLine(gfx::PointF(x(9.4f), y(6.2f)),
                     gfx::PointF(x(12.4f), y(9.8f)), stroke);
    canvas->DrawLine(gfx::PointF(x(12.4f), y(6.2f)),
                     gfx::PointF(x(9.4f), y(9.8f)), stroke);
    return;
  }
  // Two sound arcs, drawn as arcs of concentric circles centred on the cone.
  const SkPath waves =
      SkPathBuilder()
          .addArc(SkRect::MakeLTRB(x(4.6f), y(4.4f), x(11.0f), y(11.6f)), -55,
                  110)
          .addArc(SkRect::MakeLTRB(x(3.0f), y(2.4f), x(14.0f), y(13.6f)), -55,
                  110)
          .detach();
  canvas->DrawPath(waves, stroke);
}

void PaintEqualizerGlyph(gfx::Canvas* canvas,
                         const gfx::RectF& bounds,
                         SkColor color) {
  const float scale = bounds.width() / 16.0f;
  cc::PaintFlags flags;
  flags.setAntiAlias(true);
  flags.setStyle(cc::PaintFlags::kFill_Style);
  flags.setColor(color);

  constexpr float kHeights[] = {4.0f, 7.0f, 5.0f};
  float x = bounds.x() + 4.5f * scale;
  const float baseline = bounds.y() + 11.5f * scale;
  for (float height : kHeights) {
    const float h = height * scale;
    canvas->DrawRoundRect(gfx::RectF(x, baseline - h, 1.6f * scale, h),
                          0.8f * scale, flags);
    x += 2.4f * scale;
  }
}

}  // namespace origin_style
