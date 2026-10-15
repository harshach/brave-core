// Copyright (c) 2026 The Brave Authors. All rights reserved.
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this file,
// You can obtain one at https://mozilla.org/MPL/2.0/.

#include "brave/browser/ui/tabs/origin_audio_bars.h"

#include <array>
#include <cmath>
#include <numbers>

#include "cc/paint/paint_flags.h"
#include "ui/gfx/canvas.h"
#include "ui/gfx/geometry/point_f.h"
#include "ui/gfx/geometry/rect_f.h"

namespace origin_audio_bars {

namespace {

struct Bar {
  // Left edge on the 16-unit grid.
  float x;
  base::TimeDelta period;
  base::TimeDelta phase;
};

constexpr auto kBars = std::to_array<Bar>({
    {4, base::Seconds(1.2), base::Seconds(0)},
    {7, base::Seconds(0.9), base::Seconds(0.3)},
    {10, base::Seconds(1.8), base::Seconds(0.6)},
});

constexpr float kGrid = 16;
constexpr float kBarWidth = 2;
constexpr float kBarTop = 3;
constexpr float kBarBottom = 13;
constexpr float kMinHeight = 0.3f;
constexpr SkAlpha kMutedAlpha = 0x73;  // 45%

}  // namespace

Heights GetPlayingHeights(base::TimeDelta elapsed) {
  Heights heights;
  for (size_t i = 0; i < kBars.size(); ++i) {
    const double cycle = (elapsed + kBars[i].phase) / kBars[i].period;
    const double wave = 0.5 - 0.5 * std::cos(2 * std::numbers::pi * cycle);
    heights[i] = static_cast<float>(kMinHeight + (1 - kMinHeight) * wave);
  }
  return heights;
}

void Paint(gfx::Canvas* canvas,
           const gfx::RectF& bounds,
           const Heights& heights,
           SkColor color,
           bool muted) {
  const float scale = bounds.width() / kGrid;
  cc::PaintFlags flags;
  flags.setAntiAlias(true);
  flags.setStyle(cc::PaintFlags::kFill_Style);
  flags.setColor(muted ? SkColorSetA(color, kMutedAlpha) : color);
  const float full = kBarBottom - kBarTop;
  for (size_t i = 0; i < kBars.size(); ++i) {
    const float height = full * heights[i];
    const gfx::RectF bar(bounds.x() + kBars[i].x * scale,
                         bounds.y() + (kBarBottom - height) * scale,
                         kBarWidth * scale, height * scale);
    canvas->DrawRoundRect(bar, scale, flags);
  }
  if (!muted) {
    return;
  }
  cc::PaintFlags slash;
  slash.setAntiAlias(true);
  slash.setStyle(cc::PaintFlags::kStroke_Style);
  slash.setStrokeCap(cc::PaintFlags::kRound_Cap);
  slash.setStrokeWidth(1.6f * scale);
  slash.setColor(color);
  canvas->DrawLine(
      gfx::PointF(bounds.x() + 3 * scale, bounds.y() + 3 * scale),
      gfx::PointF(bounds.x() + 13 * scale, bounds.y() + 13 * scale), slash);
}

}  // namespace origin_audio_bars
