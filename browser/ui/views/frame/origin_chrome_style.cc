// Copyright (c) 2026 The Brave Authors. All rights reserved.
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this file,
// You can obtain one at https://mozilla.org/MPL/2.0/.

#include "brave/browser/ui/views/frame/origin_chrome_style.h"

#include "build/build_config.h"
#include "chrome/browser/ui/color/chrome_color_id.h"
#include "ui/color/color_provider.h"
#include "ui/gfx/color_utils.h"
#include "ui/views/view.h"

namespace origin_chrome_style {

gfx::FontList Font(int size, gfx::Font::Weight weight) {
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
  return !provider || color_utils::IsDark(provider->GetColor(kColorToolbar));
}

SkColor PrimaryText(bool dark) {
  return dark ? SkColorSetRGB(0xF5, 0xF5, 0xF6) : SkColorSetRGB(0x18, 0x1D, 0x27);
}

SkColor SecondaryText(bool dark) {
  return dark ? SkColorSetRGB(0x94, 0x96, 0x9C) : SkColorSetRGB(0x53, 0x58, 0x62);
}

SkColor MutedText(bool dark) {
  return dark ? SkColorSetRGB(0x6B, 0x6E, 0x75) : SkColorSetRGB(0x71, 0x76, 0x80);
}

SkColor HoverFill(bool dark) {
  return dark ? SkColorSetARGB(0x0F, 0xFF, 0xFF, 0xFF)
              : SkColorSetRGB(0xF5, 0xF5, 0xF5);
}

SkColor SelectedFill(bool dark) {
  return dark ? SkColorSetARGB(0x1A, 0xFF, 0xFF, 0xFF)
              : SkColorSetRGB(0xEC, 0xEC, 0xEE);
}

SkColor Separator(bool dark) {
  return dark ? SkColorSetARGB(0x14, 0xFF, 0xFF, 0xFF)
              : SkColorSetRGB(0xE9, 0xEA, 0xEB);
}

}  // namespace origin_chrome_style
