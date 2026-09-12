/* Copyright (c) 2026 The Brave Authors. All rights reserved.
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at https://mozilla.org/MPL/2.0/. */

#ifndef BRAVE_BROWSER_UI_VIEWS_FRAME_ORIGIN_WIDGETS_ORIGIN_MEDIA_WIDGET_H_
#define BRAVE_BROWSER_UI_VIEWS_FRAME_ORIGIN_WIDGETS_ORIGIN_MEDIA_WIDGET_H_

#include <optional>
#include <string>

#include "base/scoped_observation.h"
#include "brave/browser/ui/tabs/origin_media_monitor.h"
#include "brave/browser/ui/views/frame/origin_widgets/origin_widget.h"
#include "ui/base/metadata/metadata_header_macros.h"
#include "url/gurl.h"

// Shared behaviour for widgets backed by a page's media session.
//
// Media widgets are deliberately global: the monitor searches every Space in
// the window, so a video started in one Space keeps its card while the user
// works in another. Subclasses only build their own layout and read `item()`.
class OriginMediaWidgetView : public OriginWidgetView,
                              public OriginMediaMonitor::Observer {
  METADATA_HEADER(OriginMediaWidgetView, OriginWidgetView)

 public:
  OriginMediaWidgetView(std::string widget_id,
                        const OriginWidgetContext& context,
                        OriginMediaMonitor::MediaSource source,
                        GURL service_url);
  OriginMediaWidgetView(const OriginMediaWidgetView&) = delete;
  OriginMediaWidgetView& operator=(const OriginMediaWidgetView&) = delete;
  ~OriginMediaWidgetView() override;

  // OriginWidgetView:
  void Refresh() override;

  // OriginMediaMonitor::Observer:
  void OnOriginMediaChanged() override;

 protected:
  const std::optional<OriginMediaMonitor::MediaItem>& item() const {
    return item_;
  }

  // Called after the tracked media changes, including when it disappears.
  virtual void OnMediaUpdated() = 0;

  void TogglePlayPause();
  void ToggleMuted();
  void SkipToNextTrack();
  void SkipToPreviousTrack();
  // Selects the page the media belongs to, switching Space if it lives in a
  // different one.
  void OpenMediaPage();
  // Opens the service itself. Used by the empty state, where there is no page
  // to return to yet.
  void OpenService();
  content::WebContents* GetMediaContents() const;

  // "Lofi Girl - from Playground". The Space attribution is what makes the
  // card legible when it is showing media from a Space you are not in.
  std::u16string BuildSubtitle() const;
  // Elapsed fraction, or 0 when the page reports no position (a live stream).
  double GetProgressFraction() const;

 private:
  OriginWidgetContext context_;
  OriginMediaMonitor::MediaSource source_;
  GURL service_url_;
  std::optional<OriginMediaMonitor::MediaItem> item_;
  base::ScopedObservation<OriginMediaMonitor, OriginMediaMonitor::Observer>
      monitor_observation_{this};
};

#endif  // BRAVE_BROWSER_UI_VIEWS_FRAME_ORIGIN_WIDGETS_ORIGIN_MEDIA_WIDGET_H_
