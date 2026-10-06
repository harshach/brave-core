// Copyright (c) 2026 The Brave Authors. All rights reserved.
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this file,
// You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef BRAVE_BROWSER_UI_TABS_ORIGIN_SPACE_THEME_H_
#define BRAVE_BROWSER_UI_TABS_ORIGIN_SPACE_THEME_H_

#include <string_view>

#include "base/containers/span.h"
#include "brave/browser/workspaces/workspace_metadata.h"
#include "third_party/skia/include/core/SkColor.h"
#include "ui/color/color_provider_key.h"

namespace origin_space_theme {

struct Theme {
  std::string_view id;
  std::u16string_view name;
  // OKLCH hue in degrees.
  float hue;
  // Graphite keeps the hue for its swatch only and never tints the window.
  bool mono;
};

// Every theme a Space can pick, in the order the card shows them.
base::span<const Theme> GetThemes();

// Returns the theme for `id`, or the first one if `id` is unknown.
const Theme& GetTheme(std::string_view id);

// One hue, several tokens. Intensity only scales chroma, so text contrast
// stays the same across every theme.
struct Colors {
  // The window shell: frame, top bar and sidebar.
  SkColor surface;
  SkColor hover;
  // Fill behind the active page and the selected Space.
  SkColor selection;
  SkColor border;
  // The selected Space's icon.
  SkColor accent;
  // The theme's chip in the Space card.
  SkColor swatch;
};

Colors GetColors(std::string_view theme_id,
                 OriginSpaceThemeIntensity intensity,
                 bool dark);

// Returns a supplier that tints a window for `theme_id`. Suppliers live for
// the life of the process, so the colour provider cache can key on them.
ui::ColorProviderKey::InitializerSupplier* GetColorSupplier(
    std::string_view theme_id,
    OriginSpaceThemeIntensity intensity);

}  // namespace origin_space_theme

#endif  // BRAVE_BROWSER_UI_TABS_ORIGIN_SPACE_THEME_H_
