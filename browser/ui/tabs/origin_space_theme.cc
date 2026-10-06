// Copyright (c) 2026 The Brave Authors. All rights reserved.
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this file,
// You can obtain one at https://mozilla.org/MPL/2.0/.

#include "brave/browser/ui/tabs/origin_space_theme.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <iterator>
#include <memory>
#include <vector>

#include "base/no_destructor.h"
#include "base/numerics/angle_conversions.h"
#include "base/numerics/safe_conversions.h"
#include "brave/browser/ui/color/brave_color_id.h"
#include "chrome/browser/ui/color/chrome_color_id.h"
#include "ui/color/color_id.h"
#include "ui/color/color_mixer.h"
#include "ui/color/color_provider.h"
#include "ui/color/color_recipe.h"
#include "ui/color/color_transform.h"

namespace origin_space_theme {

namespace {

constexpr auto kThemes = std::to_array<Theme>({
    {kOriginSpaceThemeEmber, u"Ember", 35, false},
    {kOriginSpaceThemeAmber, u"Amber", 75, false},
    {kOriginSpaceThemeForest, u"Forest", 150, false},
    {kOriginSpaceThemeTeal, u"Teal", 195, false},
    {kOriginSpaceThemeOcean, u"Ocean", 255, false},
    {kOriginSpaceThemeViolet, u"Violet", 295, false},
    {kOriginSpaceThemeRose, u"Rose", 355, false},
    {kOriginSpaceThemeGraphite, u"Graphite", 260, true},
});

constexpr int kIntensityCount = 3;

U8CPU EncodeSrgb(double linear) {
  const double value = linear <= 0.0031308
                           ? 12.92 * linear
                           : 1.055 * std::pow(linear, 1.0 / 2.4) - 0.055;
  return base::ClampRound<U8CPU>(std::clamp(value, 0.0, 1.0) * 255);
}

size_t GetThemeIndex(std::string_view id) {
  const auto it = std::ranges::find(kThemes, id, &Theme::id);
  return it == kThemes.end()
             ? 0u
             : static_cast<size_t>(std::distance(kThemes.begin(), it));
}

// https://bottosson.github.io/posts/oklab/
SkColor Oklch(double lightness, double chroma, double hue) {
  const double a = chroma * std::cos(base::DegToRad(hue));
  const double b = chroma * std::sin(base::DegToRad(hue));
  const double l = std::pow(lightness + 0.3963377774 * a + 0.2158037573 * b, 3);
  const double m = std::pow(lightness - 0.1055613458 * a - 0.0638541728 * b, 3);
  const double s = std::pow(lightness - 0.0894841775 * a - 1.2914855480 * b, 3);
  return SkColorSetRGB(
      EncodeSrgb(4.0767416621 * l - 3.3077115913 * m + 0.2309699292 * s),
      EncodeSrgb(-1.2684380046 * l + 2.6097574011 * m - 0.3413193965 * s),
      EncodeSrgb(-0.0041960863 * l - 0.7034186147 * m + 1.7076147010 * s));
}

class ColorSupplier : public ui::ColorProviderKey::InitializerSupplier {
 public:
  ColorSupplier(std::string_view theme_id, OriginSpaceThemeIntensity intensity)
      : theme_id_(theme_id), intensity_(intensity) {}

  void AddColorMixers(ui::ColorProvider* provider,
                      const ui::ColorProviderKey& key) const override {
    const Colors colors =
        GetColors(theme_id_, intensity_,
                  key.color_mode == ui::ColorProviderKey::ColorMode::kDark);
    // Origin's own shell colours are set in post-processing, and suppliers
    // are added after them, so this mixer has the last word.
    ui::ColorMixer& mixer = provider->AddPostprocessingMixer();
    mixer[ui::kColorFrameActive] = {colors.surface};
    mixer[ui::kColorFrameInactive] = {colors.surface};
    mixer[kColorToolbar] = {colors.surface};
    mixer[kColorToolbarTopSeparatorFrameActive] = {colors.surface};
    mixer[kColorToolbarTopSeparatorFrameInactive] = {colors.surface};
    mixer[kColorLocationBarBackground] = {colors.surface};
    mixer[kColorLocationBarBackgroundHovered] = {colors.surface};
    mixer[kColorBraveVerticalTabInactiveBackground] = {colors.surface};
    mixer[kColorBraveVerticalTabSeparator] = {colors.surface};
    mixer[kColorBraveVerticalTabHoveredBackground] = {colors.hover};
    mixer[kColorBraveVerticalTabActiveBackground] = {colors.selection};
  }

 private:
  const std::string_view theme_id_;
  const OriginSpaceThemeIntensity intensity_;
};

}  // namespace

base::span<const Theme> GetThemes() {
  return kThemes;
}

const Theme& GetTheme(std::string_view id) {
  return kThemes[GetThemeIndex(id)];
}

Colors GetColors(std::string_view theme_id,
                 OriginSpaceThemeIntensity intensity,
                 bool dark) {
  const Theme& theme = GetTheme(theme_id);
  const double k = static_cast<double>(intensity);
  const double c = theme.mono ? 0.0 : 1.0;
  const double h = theme.hue;
  const SkColor swatch =
      theme.mono ? SkColorSetRGB(0x6B, 0x6E, 0x75) : Oklch(0.64, 0.17, h);
  if (dark) {
    return {
        .surface = Oklch(0.205, 0.012 * k * c, h),
        .hover = Oklch(0.26, 0.02 * k * c, h),
        .selection = Oklch(0.30, 0.03 * k * c, h),
        .border = theme.mono ? SkColorSetARGB(0x2E, 0xFF, 0xFF, 0xFF)
                             : Oklch(0.50, 0.05 + 0.03 * k, h),
        .accent =
            theme.mono ? SkColorSetRGB(0xE4, 0xE5, 0xE7) : Oklch(0.74, 0.15, h),
        .swatch = swatch,
    };
  }
  // The design is dark only. Light mode keeps the same hue and chroma steps
  // on a near-white shell so dark text stays readable.
  return {
      .surface = Oklch(0.965, 0.008 * k * c, h),
      .hover = Oklch(0.93, 0.014 * k * c, h),
      .selection = Oklch(0.905, 0.028 * k * c, h),
      .border = theme.mono ? SkColorSetARGB(0x24, 0x00, 0x00, 0x00)
                           : Oklch(0.80, 0.04 + 0.02 * k, h),
      .accent =
          theme.mono ? SkColorSetRGB(0x3F, 0x41, 0x46) : Oklch(0.56, 0.15, h),
      .swatch = swatch,
  };
}

ui::ColorProviderKey::InitializerSupplier* GetColorSupplier(
    std::string_view theme_id,
    OriginSpaceThemeIntensity intensity) {
  static base::NoDestructor<std::vector<std::unique_ptr<ColorSupplier>>>
      suppliers([] {
        std::vector<std::unique_ptr<ColorSupplier>> all;
        for (const Theme& theme : kThemes) {
          for (int k = 1; k <= kIntensityCount; ++k) {
            all.push_back(std::make_unique<ColorSupplier>(
                theme.id, static_cast<OriginSpaceThemeIntensity>(k)));
          }
        }
        return all;
      }());
  const int k = std::clamp(static_cast<int>(intensity), 1, kIntensityCount);
  return (*suppliers)[GetThemeIndex(theme_id) * kIntensityCount + (k - 1)]
      .get();
}

}  // namespace origin_space_theme
