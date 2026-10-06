// Copyright (c) 2026 The Brave Authors. All rights reserved.
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this file,
// You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef BRAVE_BROWSER_UI_TABS_ORIGIN_AUDIO_BARS_H_
#define BRAVE_BROWSER_UI_TABS_ORIGIN_AUDIO_BARS_H_

#include <array>

#include "base/time/time.h"
#include "third_party/skia/include/core/SkColor.h"

namespace gfx {
class Canvas;
class RectF;
}  // namespace gfx

// The three-bar indicator a Space shows while it plays sound.
namespace origin_audio_bars {

// Bar heights as a fraction of the tallest bar, left to right.
using Heights = std::array<float, 3>;

// The whole pattern repeats after this long.
inline constexpr base::TimeDelta kCycle = base::Seconds(3.6);

// Where the bars rest when paused, muted, or when motion is reduced.
inline constexpr Heights kRestingHeights = {0.55f, 1.0f, 0.7f};

Heights GetPlayingHeights(base::TimeDelta elapsed);

// Paints the bars on the design's 16-unit grid, scaled to `bounds`. Muted
// bars are dimmed and struck through.
void Paint(gfx::Canvas* canvas,
           const gfx::RectF& bounds,
           const Heights& heights,
           SkColor color,
           bool muted);

}  // namespace origin_audio_bars

#endif  // BRAVE_BROWSER_UI_TABS_ORIGIN_AUDIO_BARS_H_
