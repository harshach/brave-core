/* Copyright (c) 2026 The Brave Authors. All rights reserved.
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at https://mozilla.org/MPL/2.0/. */

#include "brave/browser/ui/views/frame/origin_widgets/origin_media_widget.h"

#include <utility>

#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/singleton_tabs.h"
#include "ui/base/metadata/metadata_impl_macros.h"

OriginMediaWidgetView::OriginMediaWidgetView(
    std::string widget_id,
    const OriginWidgetContext& context,
    OriginMediaMonitor::MediaSource source,
    GURL service_url)
    : OriginWidgetView(std::move(widget_id)),
      context_(context),
      source_(source),
      service_url_(std::move(service_url)) {
  if (context_.media_monitor) {
    monitor_observation_.Observe(context_.media_monitor);
  }
}

OriginMediaWidgetView::~OriginMediaWidgetView() = default;

void OriginMediaWidgetView::Refresh() {
  item_ = context_.media_monitor
              ? context_.media_monitor->GetActiveMedia(source_)
              : std::optional<OriginMediaMonitor::MediaItem>();
  OnMediaUpdated();
}

void OriginMediaWidgetView::OnOriginMediaChanged() {
  Refresh();
}

void OriginMediaWidgetView::TogglePlayPause() {
  if (item_ && context_.media_monitor) {
    context_.media_monitor->TogglePlayPause(item_->tab_id);
  }
}

void OriginMediaWidgetView::ToggleMuted() {
  if (item_ && context_.media_monitor) {
    context_.media_monitor->ToggleMuted(item_->tab_id);
  }
}

void OriginMediaWidgetView::SkipToNextTrack() {
  if (item_ && context_.media_monitor) {
    context_.media_monitor->SkipToNextTrack(item_->tab_id);
  }
}

void OriginMediaWidgetView::SkipToPreviousTrack() {
  if (item_ && context_.media_monitor) {
    context_.media_monitor->SkipToPreviousTrack(item_->tab_id);
  }
}

void OriginMediaWidgetView::OpenMediaPage() {
  if (item_ && context_.media_monitor) {
    context_.media_monitor->ActivateTab(item_->tab_id);
  }
}

void OriginMediaWidgetView::OpenService() {
  if (context_.browser && service_url_.is_valid()) {
    ShowSingletonTab(context_.browser.get(), service_url_);
  }
}

content::WebContents* OriginMediaWidgetView::GetMediaContents() const {
  return item_ && context_.media_monitor
             ? context_.media_monitor->GetMediaContents(item_->tab_id)
             : nullptr;
}

std::u16string OriginMediaWidgetView::BuildSubtitle() const {
  if (!item_) {
    return std::u16string();
  }
  std::u16string subtitle = item_->artist;
  if (!item_->space_name.empty()) {
    if (!subtitle.empty()) {
      subtitle.append(u" · ");
    }
    subtitle.append(u"from ").append(item_->space_name);
  }
  return subtitle;
}

double OriginMediaWidgetView::GetProgressFraction() const {
  if (!item_ || !item_->position || !item_->duration ||
      !item_->duration->is_positive()) {
    return 0;
  }
  return item_->position->InSecondsF() / item_->duration->InSecondsF();
}

BEGIN_METADATA(OriginMediaWidgetView)
END_METADATA
