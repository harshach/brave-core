// Copyright (c) 2026 The Brave Authors. All rights reserved.
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this file,
// You can obtain one at https://mozilla.org/MPL/2.0/.

#include "brave/browser/ui/tabs/origin_audio_bars.h"

#include "testing/gtest/include/gtest/gtest.h"

namespace origin_audio_bars {

// The design's first frame: bars of 3, 8.25 and 8.25 on a 10-unit column.
TEST(OriginAudioBarsTest, StartsOnTheDesignsFirstFrame) {
  const Heights heights = GetPlayingHeights(base::TimeDelta());
  EXPECT_NEAR(heights[0], 0.3f, 1e-4);
  EXPECT_NEAR(heights[1], 0.825f, 1e-4);
  EXPECT_NEAR(heights[2], 0.825f, 1e-4);
}

TEST(OriginAudioBarsTest, RepeatsEveryCycleAndStaysInRange) {
  for (int ms = 0; ms < 3600; ms += 37) {
    const base::TimeDelta t = base::Milliseconds(ms);
    const Heights now = GetPlayingHeights(t);
    const Heights later = GetPlayingHeights(t + kCycle);
    for (size_t i = 0; i < now.size(); ++i) {
      EXPECT_NEAR(now[i], later[i], 1e-4) << "bar " << i << " at " << ms;
      EXPECT_GE(now[i], 0.3f - 1e-4);
      EXPECT_LE(now[i], 1.0f + 1e-4);
    }
  }
}

TEST(OriginAudioBarsTest, BarsMoveIndependently) {
  // Half of the left bar's period later, it is at its tallest while the others
  // are somewhere else, so the bars never move in lockstep.
  const Heights heights = GetPlayingHeights(base::Seconds(0.6));
  EXPECT_NEAR(heights[0], 1.0f, 1e-4);
  EXPECT_NE(heights[0], heights[1]);
  EXPECT_NE(heights[1], heights[2]);
}

}  // namespace origin_audio_bars
