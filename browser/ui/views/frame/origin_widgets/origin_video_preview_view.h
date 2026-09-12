// Copyright (c) 2026 The Brave Authors. All rights reserved.
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this file,
// You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef BRAVE_BROWSER_UI_VIEWS_FRAME_ORIGIN_WIDGETS_ORIGIN_VIDEO_PREVIEW_VIEW_H_
#define BRAVE_BROWSER_UI_VIEWS_FRAME_ORIGIN_WIDGETS_ORIGIN_VIDEO_PREVIEW_VIEW_H_

#include <memory>

#include "base/functional/callback_helpers.h"
#include "base/memory/weak_ptr.h"
#include "base/timer/timer.h"
#include "content/public/browser/web_contents_observer.h"
#include "services/viz/privileged/mojom/compositing/frame_sink_video_capture.mojom.h"
#include "third_party/skia/include/core/SkBitmap.h"
#include "ui/base/metadata/metadata_header_macros.h"
#include "ui/gfx/geometry/rect_f.h"
#include "ui/views/view.h"

namespace base {
class Value;
}
namespace viz {
class ClientFrameSinkVideoCapturer;
}

// Streams the video region of a background page into the widget. Frames stay
// local; the original player remains the sole owner of playback and audio.
class OriginVideoPreviewView : public views::View,
                               public content::WebContentsObserver,
                               public viz::mojom::FrameSinkVideoConsumer {
  METADATA_HEADER(OriginVideoPreviewView, views::View)

 public:
  OriginVideoPreviewView();
  ~OriginVideoPreviewView() override;

  void SetWebContents(content::WebContents* contents);

  // views::View:
  gfx::Size CalculatePreferredSize(
      const views::SizeBounds& available_size) const override;
  void OnPaint(gfx::Canvas* canvas) override;
  void AddedToWidget() override;
  void VisibilityChanged(views::View* starting_from, bool is_visible) override;

 private:
  void UpdateCapture();
  void StopCapture();
  void ReadVideoBounds();
  void OnVideoBounds(base::Value result);

  // content::WebContentsObserver:
  void DidFinishNavigation(content::NavigationHandle* navigation) override;
  void WebContentsDestroyed() override;

  // viz::mojom::FrameSinkVideoConsumer:
  void OnFrameCaptured(
      media::mojom::VideoBufferHandlePtr data,
      media::mojom::VideoFrameInfoPtr info,
      const gfx::Rect& content_rect,
      mojo::PendingRemote<viz::mojom::FrameSinkVideoConsumerFrameCallbacks>
          callbacks) override;
  void OnNewCaptureVersion(const media::CaptureVersion&) override;
  void OnFrameWithEmptyRegionCapture() override;
  void OnStopped() override;
  void OnLog(const std::string&) override;

  std::unique_ptr<viz::ClientFrameSinkVideoCapturer> capturer_;
  base::ScopedClosureRunner capture_request_;
  base::RepeatingTimer bounds_timer_;
  gfx::RectF video_bounds_;
  SkBitmap frame_;
  bool reading_bounds_ = false;
  base::WeakPtrFactory<OriginVideoPreviewView> weak_factory_{this};
};

#endif  // BRAVE_BROWSER_UI_VIEWS_FRAME_ORIGIN_WIDGETS_ORIGIN_VIDEO_PREVIEW_VIEW_H_
