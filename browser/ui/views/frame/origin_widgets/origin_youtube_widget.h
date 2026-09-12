/* Copyright (c) 2026 The Brave Authors. All rights reserved.
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at https://mozilla.org/MPL/2.0/. */

#ifndef BRAVE_BROWSER_UI_VIEWS_FRAME_ORIGIN_WIDGETS_ORIGIN_YOUTUBE_WIDGET_H_
#define BRAVE_BROWSER_UI_VIEWS_FRAME_ORIGIN_WIDGETS_ORIGIN_YOUTUBE_WIDGET_H_

#include <memory>

#include "base/memory/raw_ptr.h"
#include "brave/browser/ui/views/frame/origin_widgets/origin_media_widget.h"
#include "ui/base/metadata/metadata_header_macros.h"

class OriginWidgetIconButton;
class OriginWidgetProgressBar;
class OriginWidgetSoundButton;
class OriginVideoPreviewView;

namespace views {
class Label;
class View;
}  // namespace views

// Shows whatever YouTube page is playing anywhere in the window, in any Space.
class OriginYouTubeWidgetView : public OriginMediaWidgetView {
  METADATA_HEADER(OriginYouTubeWidgetView, OriginMediaWidgetView)

 public:
  static std::unique_ptr<OriginWidgetView> Create(
      const OriginWidgetContext& context);

  explicit OriginYouTubeWidgetView(const OriginWidgetContext& context);
  OriginYouTubeWidgetView(const OriginYouTubeWidgetView&) = delete;
  OriginYouTubeWidgetView& operator=(const OriginYouTubeWidgetView&) = delete;
  ~OriginYouTubeWidgetView() override;

  // views::View:
  void OnThemeChanged() override;

 protected:
  // OriginMediaWidgetView:
  void OnMediaUpdated() override;

 private:
  raw_ptr<OriginVideoPreviewView> video_preview_ = nullptr;
  raw_ptr<views::View> playing_container_ = nullptr;
  raw_ptr<views::View> empty_container_ = nullptr;
  raw_ptr<OriginWidgetIconButton> play_button_ = nullptr;
  raw_ptr<OriginWidgetIconButton> open_page_button_ = nullptr;
  raw_ptr<OriginWidgetSoundButton> sound_button_ = nullptr;
  raw_ptr<OriginWidgetProgressBar> progress_ = nullptr;
  raw_ptr<views::Label> title_ = nullptr;
  raw_ptr<views::Label> subtitle_ = nullptr;
  raw_ptr<views::Label> empty_label_ = nullptr;
};

#endif  // BRAVE_BROWSER_UI_VIEWS_FRAME_ORIGIN_WIDGETS_ORIGIN_YOUTUBE_WIDGET_H_
