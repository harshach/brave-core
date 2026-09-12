// Copyright (c) 2026 The Brave Authors. All rights reserved.
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this file,
// You can obtain one at https://mozilla.org/MPL/2.0/.

#include "brave/browser/ui/views/frame/origin_widgets/origin_video_preview_view.h"

#include <algorithm>
#include <cmath>
#include <utility>

#include "base/functional/bind.h"
#include "base/memory/read_only_shared_memory_region.h"
#include "base/values.h"
#include "chrome/common/chrome_isolated_world_ids.h"
#include "components/viz/host/client_frame_sink_video_capturer.h"
#include "content/public/browser/navigation_handle.h"
#include "content/public/browser/render_frame_host.h"
#include "content/public/browser/render_widget_host_view.h"
#include "content/public/browser/web_contents.h"
#include "media/base/video_frame.h"
#include "media/capture/mojom/video_capture_buffer.mojom.h"
#include "mojo/public/cpp/bindings/remote.h"
#include "third_party/skia/include/core/SkColorSpace.h"
#include "ui/base/metadata/metadata_impl_macros.h"
#include "ui/compositor/layer.h"
#include "ui/gfx/canvas.h"
#include "ui/gfx/geometry/rect_conversions.h"
#include "ui/gfx/image/image_skia.h"

namespace {

// Read geometry in an isolated world; never change the site's player or copy
// page content into the card. Normalized coordinates also account for zoom.
constexpr char16_t kVideoBoundsScript[] =
    uR"JS(
(() => {
  const videos = [...document.querySelectorAll('video')]
      .filter(v => v.readyState >= 2 && v.videoWidth && v.videoHeight)
      .map(v => v.getBoundingClientRect())
      .filter(r => r.width > 0 && r.height > 0 && r.top >= 0 && r.left >= 0 &&
                   r.right <= innerWidth + 1 && r.bottom <= innerHeight + 1)
      .sort((a, b) => b.width * b.height - a.width * a.height);
  if (!videos.length || !innerWidth || !innerHeight) return null;
  const r = videos[0];
  return {x: r.x / innerWidth, y: r.y / innerHeight,
          width: Math.min(r.width / innerWidth, 1 - r.x / innerWidth),
          height: Math.min(r.height / innerHeight, 1 - r.y / innerHeight)};
})()
)JS";

}  // namespace

OriginVideoPreviewView::OriginVideoPreviewView() {
  SetPaintToLayer();
  layer()->SetFillsBoundsOpaquely(false);
  layer()->SetRoundedCornerRadius(gfx::RoundedCornersF(12, 12, 0, 0));
  layer()->SetMasksToBounds(true);
}

OriginVideoPreviewView::~OriginVideoPreviewView() {
  StopCapture();
}

void OriginVideoPreviewView::SetWebContents(content::WebContents* contents) {
  if (contents == web_contents()) {
    return;
  }
  StopCapture();
  Observe(contents);
  UpdateCapture();
}

gfx::Size OriginVideoPreviewView::CalculatePreferredSize(
    const views::SizeBounds& available_size) const {
  const int width = available_size.width().is_bounded()
                        ? available_size.width().value()
                        : 260;
  return gfx::Size(width, width * 9 / 16);
}

void OriginVideoPreviewView::OnPaint(gfx::Canvas* canvas) {
  canvas->FillRect(GetLocalBounds(), SK_ColorBLACK);
  if (frame_.drawsNothing()) {
    return;
  }
  const float scale = std::min(static_cast<float>(width()) / frame_.width(),
                               static_cast<float>(height()) / frame_.height());
  const int draw_width = std::lround(frame_.width() * scale);
  const int draw_height = std::lround(frame_.height() * scale);
  canvas->DrawImageInt(gfx::ImageSkia::CreateFrom1xBitmap(frame_), 0, 0,
                       frame_.width(), frame_.height(),
                       (width() - draw_width) / 2, (height() - draw_height) / 2,
                       draw_width, draw_height, true);
}

void OriginVideoPreviewView::AddedToWidget() {
  UpdateCapture();
}

void OriginVideoPreviewView::VisibilityChanged(views::View* starting_from,
                                               bool is_visible) {
  UpdateCapture();
}

void OriginVideoPreviewView::UpdateCapture() {
  if (!web_contents() || !GetWidget() || !IsDrawn()) {
    StopCapture();
    return;
  }
  if (capturer_) {
    return;
  }
  auto* source_view = web_contents()->GetRenderWidgetHostView();
  if (!source_view) {
    return;
  }
  capturer_ = source_view->CreateVideoCapturer();
  capturer_->SetResolutionConstraints(gfx::Size(1, 1), gfx::Size(960, 960),
                                      false);
  capturer_->SetFormat(media::PIXEL_FORMAT_ARGB);
  capturer_->SetMinCapturePeriod(base::Seconds(1) / 30);
  capturer_->SetMinSizeChangePeriod(base::TimeDelta());
  capturer_->SetAutoThrottlingEnabled(true);
  capture_request_ = web_contents()->IncrementCapturerCount(
      gfx::Size(), /*stay_hidden=*/false, /*stay_awake=*/false,
      /*is_activity=*/false);
  capturer_->Start(this, viz::mojom::BufferFormatPreference::kDefault);
  ReadVideoBounds();
  bounds_timer_.Start(
      FROM_HERE, base::Seconds(1),
      base::BindRepeating(&OriginVideoPreviewView::ReadVideoBounds,
                          base::Unretained(this)));
}

void OriginVideoPreviewView::StopCapture() {
  weak_factory_.InvalidateWeakPtrs();
  bounds_timer_.Stop();
  reading_bounds_ = false;
  if (capturer_) {
    capturer_->StopAndResetConsumer();
    capturer_.reset();
  }
  capture_request_.RunAndReset();
  video_bounds_ = gfx::RectF();
  frame_.reset();
  SchedulePaint();
}

void OriginVideoPreviewView::ReadVideoBounds() {
  if (reading_bounds_ || !web_contents()) {
    return;
  }
  auto* frame = web_contents()->GetPrimaryMainFrame();
  if (!frame || !frame->IsRenderFrameLive()) {
    return;
  }
  reading_bounds_ = true;
  frame->ExecuteJavaScriptInIsolatedWorld(
      kVideoBoundsScript,
      base::BindOnce(&OriginVideoPreviewView::OnVideoBounds,
                     weak_factory_.GetWeakPtr()),
      ISOLATED_WORLD_ID_BRAVE_INTERNAL);
}

void OriginVideoPreviewView::OnVideoBounds(base::Value result) {
  reading_bounds_ = false;
  video_bounds_ = gfx::RectF();
  if (const auto* bounds = result.GetIfDict()) {
    const auto x = bounds->FindDouble("x");
    const auto y = bounds->FindDouble("y");
    const auto width = bounds->FindDouble("width");
    const auto height = bounds->FindDouble("height");
    if (x && y && width && height && std::isfinite(*x) && std::isfinite(*y) &&
        std::isfinite(*width) && std::isfinite(*height) && *x >= 0 && *y >= 0 &&
        *width > 0 && *height > 0 && *x + *width <= 1.001 &&
        *y + *height <= 1.001) {
      video_bounds_ = gfx::RectF(*x, *y, *width, *height);
    }
  }
  if (video_bounds_.IsEmpty()) {
    frame_.reset();
    SchedulePaint();
  } else if (capturer_) {
    capturer_->RequestRefreshFrame();
  }
}

void OriginVideoPreviewView::DidFinishNavigation(
    content::NavigationHandle* navigation) {
  if (navigation->HasCommitted() && navigation->IsInPrimaryMainFrame()) {
    StopCapture();
    UpdateCapture();
  }
}

void OriginVideoPreviewView::WebContentsDestroyed() {
  SetWebContents(nullptr);
}

void OriginVideoPreviewView::OnFrameCaptured(
    media::mojom::VideoBufferHandlePtr data,
    media::mojom::VideoFrameInfoPtr info,
    const gfx::Rect& content_rect,
    mojo::PendingRemote<viz::mojom::FrameSinkVideoConsumerFrameCallbacks>
        callbacks) {
  // Return every buffer, including rejected frames, so capture cannot stall.
  base::ScopedClosureRunner done(base::BindOnce(
      [](mojo::PendingRemote<viz::mojom::FrameSinkVideoConsumerFrameCallbacks>
             pending) {
        mojo::Remote<viz::mojom::FrameSinkVideoConsumerFrameCallbacks> remote(
            std::move(pending));
        remote->Done();
      },
      std::move(callbacks)));
  if (video_bounds_.IsEmpty() || !data->is_read_only_shmem_region() ||
      info->pixel_format != media::PIXEL_FORMAT_ARGB ||
      content_rect.IsEmpty()) {
    return;
  }
  auto mapping = data->get_read_only_shmem_region().Map();
  if (!mapping.IsValid() ||
      mapping.size() < media::VideoFrame::AllocationSize(info->pixel_format,
                                                         info->coded_size)) {
    return;
  }
  gfx::Rect crop = gfx::ToEnclosedRect(
      gfx::RectF(content_rect.x() + video_bounds_.x() * content_rect.width(),
                 content_rect.y() + video_bounds_.y() * content_rect.height(),
                 video_bounds_.width() * content_rect.width(),
                 video_bounds_.height() * content_rect.height()));
  crop.Intersect(content_rect);
  if (crop.IsEmpty() || !gfx::Rect(info->coded_size).Contains(crop)) {
    return;
  }
  // Follow BackgroundThumbnailVideoCapturer's ARGB layout, but retain only
  // our cropped copy so Viz can immediately recycle its shared frame buffer.
  const SkImageInfo image_info = SkImageInfo::MakeN32(
      info->coded_size.width(), info->coded_size.height(), kPremul_SkAlphaType,
      info->color_space.ToSkColorSpace());
  const size_t row_bytes =
      media::VideoFrame::RowBytes(media::VideoFrame::Plane::kARGB,
                                  info->pixel_format, info->coded_size.width());
  SkPixmap pixels(image_info, mapping.memory(), row_bytes);
  SkBitmap next_frame;
  if (next_frame.tryAllocN32Pixels(crop.width(), crop.height()) &&
      pixels.readPixels(next_frame.pixmap(), crop.x(), crop.y())) {
    next_frame.setImmutable();
    frame_ = std::move(next_frame);
    SchedulePaint();
  }
}

void OriginVideoPreviewView::OnNewCaptureVersion(const media::CaptureVersion&) {
}
void OriginVideoPreviewView::OnFrameWithEmptyRegionCapture() {}
void OriginVideoPreviewView::OnStopped() {}
void OriginVideoPreviewView::OnLog(const std::string&) {}

BEGIN_METADATA(OriginVideoPreviewView)
END_METADATA
