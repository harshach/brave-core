// Copyright (c) 2026 The Brave Authors. All rights reserved.
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this file,
// You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef BRAVE_BROWSER_UI_VIEWS_FRAME_VERTICAL_TABS_ORIGIN_SPACE_CARD_H_
#define BRAVE_BROWSER_UI_VIEWS_FRAME_VERTICAL_TABS_ORIGIN_SPACE_CARD_H_

#include <string>
#include <vector>

#include "base/functional/callback.h"
#include "base/memory/raw_ptr.h"
#include "base/memory/raw_ref.h"
#include "brave/browser/workspaces/workspace_metadata.h"
#include "ui/base/metadata/metadata_header_macros.h"
#include "ui/views/view.h"

class OriginOwnedBubble;

namespace gfx {
struct VectorIcon;
}  // namespace gfx

namespace views {
class Label;
class Widget;
}  // namespace views

// Manages one Space from the rail: its window theme and intensity, plus
// rename, change icon and delete.
class OriginSpaceCard : public views::View {
  METADATA_HEADER(OriginSpaceCard, views::View)

 public:
  struct Params {
    Params();
    Params(Params&&);
    Params& operator=(Params&&);
    ~Params();

    OriginSpaceMetadata space;
    raw_ptr<const gfx::VectorIcon> icon = nullptr;
    size_t page_count = 0;
    // 1-based number key that selects the Space; 0 hides the hint.
    int shortcut = 0;
    bool can_delete = true;
    OriginSpaceThemeIntensity intensity = OriginSpaceThemeIntensity::kRich;

    base::RepeatingCallback<void(const std::string& theme)> set_theme;
    base::RepeatingCallback<void(OriginSpaceThemeIntensity)> set_intensity;
    base::RepeatingClosure rename;
    base::RepeatingClosure change_icon;
    base::RepeatingClosure delete_space;
  };

  // Shows the card above `anchor`, owned by `owner`. `on_closed` runs once
  // the card has closed, however it closed.
  static views::Widget* Show(OriginOwnedBubble& owner,
                             views::View* anchor,
                             Params params,
                             base::OnceClosure on_closed);

  explicit OriginSpaceCard(Params params);
  OriginSpaceCard(const OriginSpaceCard&) = delete;
  OriginSpaceCard& operator=(const OriginSpaceCard&) = delete;
  ~OriginSpaceCard() override;

  // For tests.
  void SelectThemeForTesting(const std::string& theme) { SelectTheme(theme); }
  void SelectIntensityForTesting(OriginSpaceThemeIntensity intensity) {
    SelectIntensity(intensity);
  }

 private:
  class IconTile;
  class Swatch;
  class Segment;

  void SelectTheme(const std::string& theme);
  void SelectIntensity(OriginSpaceThemeIntensity intensity);
  void RunAction(base::RepeatingClosure action);
  void Refresh();

  Params params_;
  raw_ptr<IconTile> icon_tile_ = nullptr;
  raw_ptr<views::Label> theme_name_ = nullptr;
  std::vector<raw_ptr<Swatch>> swatches_;
  std::vector<raw_ptr<Segment>> segments_;
};

#endif  // BRAVE_BROWSER_UI_VIEWS_FRAME_VERTICAL_TABS_ORIGIN_SPACE_CARD_H_
