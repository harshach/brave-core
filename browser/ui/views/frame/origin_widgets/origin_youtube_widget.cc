/* Copyright (c) 2026 The Brave Authors. All rights reserved.
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at https://mozilla.org/MPL/2.0/. */

#include "brave/browser/ui/views/frame/origin_widgets/origin_youtube_widget.h"

#include <memory>
#include <utility>

#include "base/functional/bind.h"
#include "brave/browser/ui/views/frame/origin_widgets/origin_video_preview_view.h"
#include "brave/browser/ui/views/frame/origin_widgets/origin_widget_controls.h"
#include "brave/browser/ui/views/frame/origin_widgets/origin_widget_style.h"
#include "brave/components/vector_icons/vector_icons.h"
#include "ui/base/metadata/metadata_impl_macros.h"
#include "ui/views/controls/label.h"
#include "ui/views/layout/box_layout.h"
#include "ui/views/layout/flex_layout.h"
#include "ui/views/view.h"

namespace {

constexpr char kYouTubeWidgetId[] = "youtube";
constexpr SkColor kYouTubeRed = SkColorSetRGB(0xFF, 0x00, 0x33);
}  // namespace

// static
std::unique_ptr<OriginWidgetView> OriginYouTubeWidgetView::Create(
    const OriginWidgetContext& context) {
  return std::make_unique<OriginYouTubeWidgetView>(context);
}

OriginYouTubeWidgetView::OriginYouTubeWidgetView(
    const OriginWidgetContext& context)
    : OriginMediaWidgetView(kYouTubeWidgetId,
                            context,
                            OriginMediaMonitor::MediaSource::kYouTube,
                            GURL("https://www.youtube.com/")) {
  SetLayoutManager(std::make_unique<views::FlexLayout>())
      ->SetOrientation(views::LayoutOrientation::kVertical);

  playing_container_ = AddChildView(std::make_unique<views::View>());
  playing_container_->SetLayoutManager(std::make_unique<views::FlexLayout>())
      ->SetOrientation(views::LayoutOrientation::kVertical);

  video_preview_ = playing_container_->AddChildView(
      std::make_unique<OriginVideoPreviewView>());

  progress_ = playing_container_->AddChildView(
      std::make_unique<OriginWidgetProgressBar>());
  progress_->SetFillColor(kYouTubeRed);

  auto* meta =
      playing_container_->AddChildView(std::make_unique<views::View>());
  auto* meta_layout = meta->SetLayoutManager(std::make_unique<views::BoxLayout>(
      views::BoxLayout::Orientation::kHorizontal,
      gfx::Insets::TLBR(9, 10, 10, 6), 10));
  meta_layout->set_cross_axis_alignment(
      views::BoxLayout::CrossAxisAlignment::kCenter);

  auto* text = meta->AddChildView(std::make_unique<views::View>());
  text->SetLayoutManager(std::make_unique<views::BoxLayout>(
      views::BoxLayout::Orientation::kVertical, gfx::Insets(), 1));
  title_ = text->AddChildView(CreateOriginWidgetTitleLabel(std::u16string()));
  subtitle_ =
      text->AddChildView(CreateOriginWidgetSubtitleLabel(std::u16string()));
  meta_layout->SetFlexForView(text, 1);

  play_button_ = meta->AddChildView(std::make_unique<OriginWidgetIconButton>(
      base::BindRepeating(&OriginYouTubeWidgetView::TogglePlayPause,
                          base::Unretained(this)),
      kLeoPlayFilledIcon, u"Play", /*button_size=*/30, /*icon_size=*/14));
  play_button_->SetPrimary(true);

  sound_button_ = meta->AddChildView(
      std::make_unique<OriginWidgetSoundButton>(base::BindRepeating(
          &OriginYouTubeWidgetView::ToggleMuted, base::Unretained(this))));
  open_page_button_ =
      meta->AddChildView(std::make_unique<OriginWidgetIconButton>(
          base::BindRepeating(&OriginYouTubeWidgetView::OpenMediaPage,
                              base::Unretained(this)),
          kLeoLaunchIcon, u"Return to page"));

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
      base::BindRepeating(&OriginYouTubeWidgetView::OpenService,
                          base::Unretained(this)),
      kOriginWidgetYoutubeIcon, u"Open YouTube"));

  Refresh();
}

OriginYouTubeWidgetView::~OriginYouTubeWidgetView() = default;

void OriginYouTubeWidgetView::OnThemeChanged() {
  OriginMediaWidgetView::OnThemeChanged();
  const bool dark = origin_style::IsDark(this);
  title_->SetEnabledColor(origin_style::PrimaryText(dark));
  subtitle_->SetEnabledColor(origin_style::MutedText(dark));
  empty_label_->SetEnabledColor(origin_style::MutedText(dark));
}

void OriginYouTubeWidgetView::OnMediaUpdated() {
  const bool has_media = item().has_value();
  const bool show_preview = has_media && !item()->page_visible;
  playing_container_->SetVisible(show_preview);
  empty_container_->SetVisible(!has_media);
  // The source page already shows the video, including in split view.
  SetVisible(!has_media || show_preview);
  video_preview_->SetWebContents(show_preview ? GetMediaContents() : nullptr);

  if (has_media) {
    title_->SetText(item()->title);
    subtitle_->SetText(BuildSubtitle());
    play_button_->SetVectorIcon(item()->playing ? kLeoPauseFilledIcon
                                                : kLeoPlayFilledIcon);
    play_button_->SetTooltipAndAccessibleName(item()->playing ? u"Pause"
                                                              : u"Play");
    play_button_->SetEnabled(item()->playing ? item()->can_pause
                                             : item()->can_play);
    sound_button_->SetMuted(item()->muted);
    progress_->SetFraction(GetProgressFraction());
  }
  InvalidateLayout();
}

BEGIN_METADATA(OriginYouTubeWidgetView)
END_METADATA
