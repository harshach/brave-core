/* Copyright (c) 2026 The Brave Authors. All rights reserved.
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at https://mozilla.org/MPL/2.0/. */

#include "brave/browser/ui/views/frame/origin_widgets/origin_spotify_widget.h"

#include <memory>
#include <utility>

#include "base/functional/bind.h"
#include "brave/browser/ui/views/frame/origin_widgets/origin_widget_controls.h"
#include "brave/browser/ui/views/frame/origin_widgets/origin_widget_style.h"
#include "brave/components/vector_icons/vector_icons.h"
#include "ui/base/metadata/metadata_impl_macros.h"
#include "ui/views/controls/label.h"
#include "ui/views/layout/box_layout.h"
#include "ui/views/layout/flex_layout.h"
#include "ui/views/view.h"

namespace {

constexpr char kSpotifyWidgetId[] = "spotify";
constexpr SkColor kSpotifyGreen = SkColorSetRGB(0x1D, 0xB9, 0x54);

}  // namespace

// static
std::unique_ptr<OriginWidgetView> OriginSpotifyWidgetView::Create(
    const OriginWidgetContext& context) {
  return std::make_unique<OriginSpotifyWidgetView>(context);
}

OriginSpotifyWidgetView::OriginSpotifyWidgetView(
    const OriginWidgetContext& context)
    : OriginMediaWidgetView(kSpotifyWidgetId,
                            context,
                            OriginMediaMonitor::MediaSource::kSpotify,
                            GURL("https://open.spotify.com/")) {
  SetLayoutManager(std::make_unique<views::FlexLayout>())
      ->SetOrientation(views::LayoutOrientation::kVertical);

  playing_container_ = AddChildView(std::make_unique<views::View>());
  playing_container_->SetLayoutManager(std::make_unique<views::BoxLayout>(
      views::BoxLayout::Orientation::kVertical, gfx::Insets(10), 9));

  auto* now_playing =
      playing_container_->AddChildView(std::make_unique<views::View>());
  auto* now_playing_layout =
      now_playing->SetLayoutManager(std::make_unique<views::BoxLayout>(
          views::BoxLayout::Orientation::kHorizontal, gfx::Insets(), 10));
  now_playing_layout->set_cross_axis_alignment(
      views::BoxLayout::CrossAxisAlignment::kCenter);

  auto* text = now_playing->AddChildView(std::make_unique<views::View>());
  text->SetLayoutManager(std::make_unique<views::BoxLayout>(
      views::BoxLayout::Orientation::kVertical, gfx::Insets(), 1));
  title_ = text->AddChildView(CreateOriginWidgetTitleLabel(std::u16string()));
  subtitle_ =
      text->AddChildView(CreateOriginWidgetSubtitleLabel(std::u16string()));
  now_playing_layout->SetFlexForView(text, 1);

  now_playing->AddChildView(std::make_unique<OriginWidgetIconButton>(
      base::BindRepeating(&OriginSpotifyWidgetView::OpenMediaPage,
                          base::Unretained(this)),
      kLeoLaunchIcon, u"Return to player"));

  progress_ = playing_container_->AddChildView(
      std::make_unique<OriginWidgetProgressBar>());
  progress_->SetFillColor(kSpotifyGreen);

  auto* transport =
      playing_container_->AddChildView(std::make_unique<views::View>());
  auto* transport_layout =
      transport->SetLayoutManager(std::make_unique<views::BoxLayout>(
          views::BoxLayout::Orientation::kHorizontal, gfx::Insets(), 14));
  transport_layout->set_main_axis_alignment(
      views::BoxLayout::MainAxisAlignment::kCenter);

  previous_button_ =
      transport->AddChildView(std::make_unique<OriginWidgetIconButton>(
          base::BindRepeating(&OriginSpotifyWidgetView::SkipToPreviousTrack,
                              base::Unretained(this)),
          kLeoPreviousOutlineIcon, u"Previous track"));
  play_button_ =
      transport->AddChildView(std::make_unique<OriginWidgetIconButton>(
          base::BindRepeating(&OriginSpotifyWidgetView::TogglePlayPause,
                              base::Unretained(this)),
          kLeoPlayFilledIcon, u"Play", /*button_size=*/30, /*icon_size=*/13));
  play_button_->SetPrimary(true);
  next_button_ =
      transport->AddChildView(std::make_unique<OriginWidgetIconButton>(
          base::BindRepeating(&OriginSpotifyWidgetView::SkipToNextTrack,
                              base::Unretained(this)),
          kLeoNextOutlineIcon, u"Next track"));

  empty_container_ = AddChildView(std::make_unique<views::View>());
  auto* empty_layout =
      empty_container_->SetLayoutManager(std::make_unique<views::BoxLayout>(
          views::BoxLayout::Orientation::kHorizontal,
          gfx::Insets::TLBR(10, 10, 10, 6), 10));
  empty_layout->set_cross_axis_alignment(
      views::BoxLayout::CrossAxisAlignment::kCenter);
  empty_label_ = empty_container_->AddChildView(
      CreateOriginWidgetSubtitleLabel(u"Nothing playing"));
  empty_layout->SetFlexForView(empty_label_, 1);
  empty_container_->AddChildView(std::make_unique<OriginWidgetIconButton>(
      base::BindRepeating(&OriginSpotifyWidgetView::OpenService,
                          base::Unretained(this)),
      kOriginWidgetSpotifyIcon, u"Open Spotify"));

  Refresh();
}

OriginSpotifyWidgetView::~OriginSpotifyWidgetView() = default;

void OriginSpotifyWidgetView::OnThemeChanged() {
  OriginMediaWidgetView::OnThemeChanged();
  const bool dark = origin_style::IsDark(this);
  title_->SetEnabledColor(origin_style::PrimaryText(dark));
  subtitle_->SetEnabledColor(origin_style::MutedText(dark));
  empty_label_->SetEnabledColor(origin_style::MutedText(dark));
}

void OriginSpotifyWidgetView::OnMediaUpdated() {
  const bool has_media = item().has_value();
  playing_container_->SetVisible(has_media);
  empty_container_->SetVisible(!has_media);

  if (has_media) {
    title_->SetText(item()->title);
    std::u16string subtitle = BuildSubtitle();
    // The web player keeps its session alive while paused, so the state has to
    // be spelled out or a paused track reads as a playing one.
    if (!subtitle.empty()) {
      subtitle.append(u" · ");
    }
    subtitle.append(item()->playing ? u"Playing" : u"Paused");
    subtitle_->SetText(subtitle);

    play_button_->SetVectorIcon(item()->playing ? kLeoPauseFilledIcon
                                                : kLeoPlayFilledIcon);
    play_button_->SetTooltipAndAccessibleName(item()->playing ? u"Pause"
                                                              : u"Play");
    play_button_->SetEnabled(item()->playing ? item()->can_pause
                                             : item()->can_play);
    previous_button_->SetEnabled(item()->can_skip_to_previous);
    next_button_->SetEnabled(item()->can_skip_to_next);
    progress_->SetFraction(GetProgressFraction());
  }
  InvalidateLayout();
}

BEGIN_METADATA(OriginSpotifyWidgetView)
END_METADATA
