// Copyright (c) 2026 The Brave Authors. All rights reserved.
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this file,
// You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef BRAVE_BROWSER_UI_VIEWS_FRAME_ORIGIN_SIDE_VIEW_H_
#define BRAVE_BROWSER_UI_VIEWS_FRAME_ORIGIN_SIDE_VIEW_H_

#include <array>
#include <memory>
#include <optional>
#include <string>

#include "base/memory/raw_ptr.h"
#include "ui/base/metadata/metadata_header_macros.h"
#include "ui/base/models/image_model.h"
#include "ui/gfx/geometry/insets.h"
#include "ui/views/controls/resize_area_delegate.h"
#include "ui/views/view.h"

namespace views {
class ImageButton;
class ImageView;
class Label;
class ResizeArea;
}  // namespace views

// What the side view shows, in header order.
enum class OriginSideViewMode {
  // The active Space's side page: a real tab hosted by the web panel.
  kPage,
  kShortcuts,
  kDownloads,
  kReminders,
  kSnoozed,
  kTags,
};

inline constexpr std::array<OriginSideViewMode, 6> kOriginSideViewModes = {
    OriginSideViewMode::kPage,      OriginSideViewMode::kShortcuts,
    OriginSideViewMode::kDownloads, OriginSideViewMode::kReminders,
    OriginSideViewMode::kSnoozed,   OriginSideViewMode::kTags,
};

std::u16string GetOriginSideViewModeName(OriginSideViewMode mode);

// The window's right-hand panel, after SigmaOS's side view: one column that
// holds either the Space's side page or a tool. The header switches between
// them; the side page itself is drawn by the web panel above this view's body.
class OriginSideView : public views::View, public views::ResizeAreaDelegate {
  METADATA_HEADER(OriginSideView, views::View)

 public:
  static constexpr int kHeaderHeight = 40;
  // Between the page and the panel, the same gap split pages have. Dragging it
  // resizes the panel.
  static constexpr int kGutterWidth = 8;
  static constexpr int kMinimumWidth = 280;
  static constexpr int kMaximumWidth = 760;
  static constexpr int kDefaultWidth = 400;

  class Delegate {
   public:
    virtual void SelectSideViewMode(OriginSideViewMode mode) = 0;
    virtual void CloseSideView() = 0;
    virtual void ReturnSidePageToMain() = 0;
    virtual void CloseSidePage() = 0;
    // `width` excludes nothing: it is the whole column, gutter included.
    virtual void ResizeSideView(int width, bool done) = 0;

   protected:
    virtual ~Delegate() = default;
  };

  explicit OriginSideView(Delegate* delegate);
  OriginSideView(const OriginSideView&) = delete;
  OriginSideView& operator=(const OriginSideView&) = delete;
  ~OriginSideView() override;

  // The delegate goes away a little before this view during window teardown.
  void ClearDelegate() { delegate_ = nullptr; }

  // Where the web panel sits inside this view when it shows the side page.
  static gfx::Insets GetWebPanelInsets();

  // The body for a tool mode. Owned by this view; shown while that mode is.
  void SetToolView(OriginSideViewMode mode, std::unique_ptr<views::View> view);
  views::View* GetToolView(OriginSideViewMode mode) const;

  void SetMode(OriginSideViewMode mode);
  OriginSideViewMode mode() const { return mode_; }

  // The Space's side page, if it has one, for the header.
  void SetSidePage(std::optional<std::u16string> title,
                   const ui::ImageModel& favicon);
  // A count badge on a mode's header button, e.g. overdue reminders.
  void SetModeBadge(OriginSideViewMode mode, int count);

  // views::View:
  void Layout(PassKey) override;
  void OnPaintBackground(gfx::Canvas* canvas) override;
  void OnThemeChanged() override;

  // views::ResizeAreaDelegate:
  void OnResize(int resize_amount, bool done_resizing) override;

  views::View* page_empty_state_for_testing() { return page_empty_state_; }
  views::ImageButton* GetModeButtonForTesting(OriginSideViewMode mode);

 private:
  class ModeButton;

  void UpdateHeader();
  gfx::Rect GetBodyBounds() const;

  raw_ptr<Delegate> delegate_ = nullptr;
  OriginSideViewMode mode_ = OriginSideViewMode::kPage;
  bool has_side_page_ = false;
  std::u16string side_page_title_;
  ui::ImageModel side_page_favicon_;

  raw_ptr<views::ResizeArea> resize_area_ = nullptr;
  std::optional<int> resize_start_width_;

  raw_ptr<views::View> header_ = nullptr;
  std::array<raw_ptr<ModeButton>, kOriginSideViewModes.size()> mode_buttons_{};
  raw_ptr<views::Label> title_ = nullptr;
  raw_ptr<views::ImageButton> return_button_ = nullptr;
  raw_ptr<views::ImageButton> close_page_button_ = nullptr;
  raw_ptr<views::ImageButton> close_button_ = nullptr;

  raw_ptr<views::View> page_empty_state_ = nullptr;
  raw_ptr<views::ImageView> empty_icon_ = nullptr;
  raw_ptr<views::Label> empty_title_ = nullptr;
  raw_ptr<views::Label> empty_body_ = nullptr;
  std::array<raw_ptr<views::View>, kOriginSideViewModes.size()> tool_views_{};
};

#endif  // BRAVE_BROWSER_UI_VIEWS_FRAME_ORIGIN_SIDE_VIEW_H_
