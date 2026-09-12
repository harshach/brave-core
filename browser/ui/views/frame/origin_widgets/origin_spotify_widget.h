/* Copyright (c) 2026 The Brave Authors. All rights reserved.
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at https://mozilla.org/MPL/2.0/. */

#ifndef BRAVE_BROWSER_UI_VIEWS_FRAME_ORIGIN_WIDGETS_ORIGIN_SPOTIFY_WIDGET_H_
#define BRAVE_BROWSER_UI_VIEWS_FRAME_ORIGIN_WIDGETS_ORIGIN_SPOTIFY_WIDGET_H_

#include <memory>

#include "base/memory/raw_ptr.h"
#include "brave/browser/ui/views/frame/origin_widgets/origin_media_widget.h"
#include "ui/base/metadata/metadata_header_macros.h"

class OriginWidgetIconButton;
class OriginWidgetProgressBar;

namespace views {
class Label;
class View;
}  // namespace views

// Controls the Spotify web player from the panel. It drives the open player
// tab through its media session rather than the Spotify Web API, so it needs
// no account linking and works wherever the player is already signed in.
class OriginSpotifyWidgetView : public OriginMediaWidgetView {
  METADATA_HEADER(OriginSpotifyWidgetView, OriginMediaWidgetView)

 public:
  static std::unique_ptr<OriginWidgetView> Create(
      const OriginWidgetContext& context);

  explicit OriginSpotifyWidgetView(const OriginWidgetContext& context);
  OriginSpotifyWidgetView(const OriginSpotifyWidgetView&) = delete;
  OriginSpotifyWidgetView& operator=(const OriginSpotifyWidgetView&) = delete;
  ~OriginSpotifyWidgetView() override;

  // views::View:
  void OnThemeChanged() override;

 protected:
  // OriginMediaWidgetView:
  void OnMediaUpdated() override;

 private:
  raw_ptr<views::View> playing_container_ = nullptr;
  raw_ptr<views::View> empty_container_ = nullptr;
  raw_ptr<views::Label> title_ = nullptr;
  raw_ptr<views::Label> subtitle_ = nullptr;
  raw_ptr<views::Label> empty_label_ = nullptr;
  raw_ptr<OriginWidgetProgressBar> progress_ = nullptr;
  raw_ptr<OriginWidgetIconButton> previous_button_ = nullptr;
  raw_ptr<OriginWidgetIconButton> play_button_ = nullptr;
  raw_ptr<OriginWidgetIconButton> next_button_ = nullptr;
};

#endif  // BRAVE_BROWSER_UI_VIEWS_FRAME_ORIGIN_WIDGETS_ORIGIN_SPOTIFY_WIDGET_H_
