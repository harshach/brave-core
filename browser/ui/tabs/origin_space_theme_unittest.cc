// Copyright (c) 2026 The Brave Authors. All rights reserved.
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this file,
// You can obtain one at https://mozilla.org/MPL/2.0/.

#include "brave/browser/ui/tabs/origin_space_theme.h"

#include "brave/browser/workspaces/workspace_metadata.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "ui/gfx/color_utils.h"

namespace origin_space_theme {

namespace {
constexpr auto kRich = OriginSpaceThemeIntensity::kRich;
}  // namespace

TEST(OriginSpaceThemeTest, MatchesDesignTokens) {
  const Colors ember = GetColors(kOriginSpaceThemeEmber, kRich, /*dark=*/true);
  EXPECT_EQ(ember.surface, SkColorSetRGB(0x21, 0x13, 0x10));
  EXPECT_EQ(ember.selection, SkColorSetRGB(0x47, 0x22, 0x18));
  EXPECT_EQ(ember.accent, SkColorSetRGB(0xFA, 0x84, 0x67));
  EXPECT_EQ(ember.swatch, SkColorSetRGB(0xE0, 0x5D, 0x3D));
  EXPECT_EQ(GetColors(kOriginSpaceThemeOcean, OriginSpaceThemeIntensity::kVivid,
                      /*dark=*/true)
                .surface,
            SkColorSetRGB(0x0B, 0x18, 0x27));
}

TEST(OriginSpaceThemeTest, IntensityScalesTintButGraphiteStaysNeutral) {
  const SkColor subtle = GetColors(kOriginSpaceThemeEmber,
                                   OriginSpaceThemeIntensity::kSubtle, true)
                             .surface;
  const SkColor vivid =
      GetColors(kOriginSpaceThemeEmber, OriginSpaceThemeIntensity::kVivid, true)
          .surface;
  EXPECT_NE(subtle, vivid);

  for (auto intensity : {OriginSpaceThemeIntensity::kSubtle,
                         OriginSpaceThemeIntensity::kVivid}) {
    const SkColor graphite =
        GetColors(kOriginSpaceThemeGraphite, intensity, true).surface;
    EXPECT_EQ(graphite, SkColorSetRGB(0x17, 0x17, 0x17));
  }
}

TEST(OriginSpaceThemeTest, LightModeKeepsALightShell) {
  const Colors light = GetColors(kOriginSpaceThemeEmber, kRich, /*dark=*/false);
  EXPECT_EQ(light.surface, SkColorSetRGB(0xFE, 0xF0, 0xEC));
  EXPECT_FALSE(color_utils::IsDark(light.surface));
  EXPECT_FALSE(color_utils::IsDark(light.selection));
  EXPECT_TRUE(color_utils::IsDark(
      GetColors(kOriginSpaceThemeEmber, kRich, /*dark=*/true).surface));
}

TEST(OriginSpaceThemeTest, UnknownThemeFallsBackToTheFirst) {
  EXPECT_EQ(GetTheme("neon").id, GetThemes().front().id);
  EXPECT_EQ(GetColorSupplier("neon", kRich),
            GetColorSupplier(GetThemes().front().id, kRich));
}

// The colour provider cache compares suppliers by address, so each theme and
// intensity must map to one stable supplier.
TEST(OriginSpaceThemeTest, SuppliersAreStablePerThemeAndIntensity) {
  EXPECT_EQ(GetColorSupplier(kOriginSpaceThemeTeal, kRich),
            GetColorSupplier(kOriginSpaceThemeTeal, kRich));
  EXPECT_NE(GetColorSupplier(kOriginSpaceThemeTeal, kRich),
            GetColorSupplier(kOriginSpaceThemeTeal,
                             OriginSpaceThemeIntensity::kSubtle));
  EXPECT_NE(GetColorSupplier(kOriginSpaceThemeTeal, kRich),
            GetColorSupplier(kOriginSpaceThemeRose, kRich));
}

}  // namespace origin_space_theme
