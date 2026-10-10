/* Copyright (c) 2025 The Brave Authors. All rights reserved.
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at https://mozilla.org/MPL/2.0/. */

#include "brave/browser/ui/views/frame/split_view/brave_multi_contents_view.h"

#include <algorithm>
#include <utility>

#include "base/check.h"
#include "base/i18n/rtl.h"
#include "brave/browser/ui/views/frame/split_view/brave_contents_container_view.h"
#include "chrome/browser/devtools/devtools_ui_controller.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/exclusive_access/exclusive_access_manager.h"
#include "chrome/browser/ui/exclusive_access/fullscreen_controller.h"
#include "chrome/browser/ui/layout_constants.h"
#include "chrome/browser/ui/views/frame/browser_view.h"
#include "chrome/browser/ui/views/frame/contents_web_view.h"
#include "chrome/browser/ui/views/frame/multi_contents_background_view.h"
#include "chrome/browser/ui/views/frame/multi_contents_resize_area.h"
#include "chrome/browser/ui/views/frame/multi_contents_view_delegate.h"
#include "components/tabs/public/tab_interface.h"
#include "content/public/browser/fullscreen_types.h"
#include "ui/base/metadata/metadata_impl_macros.h"
#include "ui/views/layout/layout_provider.h"
#include "ui/views/widget/widget.h"

namespace {

// Returns |corner_radii| with the corners that meet the split divider replaced
// by the smaller border radius.
gfx::RoundedCornersF GetSplitContentsCornerRadii(
    const gfx::RoundedCornersF& corner_radii,
    bool is_first,
    bool is_last) {
  if (corner_radii.IsEmpty()) {
    return corner_radii;
  }

  // In RTL, the first split view is on the right and the last is on the left.
  // Swap directions before setting the visual right and left radii.
  if (base::i18n::IsRTL()) {
    is_last = std::exchange(is_first, is_last);
  }

  const float adjacent_edge_radius =
      views::LayoutProvider::Get()->GetCornerRadiusMetric(
          views::ShapeContextTokensOverride::kRoundedCornersBorderRadius);

  gfx::RoundedCornersF split_radii = corner_radii;
  if (!is_first) {
    split_radii.set_upper_left(adjacent_edge_radius);
    split_radii.set_lower_left(adjacent_edge_radius);
  }
  if (!is_last) {
    split_radii.set_upper_right(adjacent_edge_radius);
    split_radii.set_lower_right(adjacent_edge_radius);
  }

  return split_radii;
}

}  // namespace

// static
BraveMultiContentsView* BraveMultiContentsView::From(MultiContentsView* view) {
  CHECK(view);
  return static_cast<BraveMultiContentsView*>(view);
}

BraveMultiContentsView::BraveMultiContentsView(
    BrowserView* browser_view,
    std::unique_ptr<MultiContentsViewDelegate> delegate)
    : MultiContentsView(browser_view, std::move(delegate)) {
  // Use rounded corners margin as resize area's width.
  resize_area_->SetPreferredSize(
      gfx::Size(kRoundedCornersContentsViewMargin, 0));
}

BraveMultiContentsView::~BraveMultiContentsView() = default;

void BraveMultiContentsView::UseContentsContainerViewForWebPanel() {
  if (!contents_container_view_for_web_panel_) {
    contents_container_view_for_web_panel_ =
        AddChildView(std::make_unique<BraveContentsContainerView>(
            browser_view_, /*for_web_panel*/ true));
    contents_container_view_for_web_panel_->SetVisible(false);

    auto& view_map =
        container_focusable_map_[contents_container_view_for_web_panel_];
    auto* contents_view =
        contents_container_view_for_web_panel_->contents_view();
    view_map[contents_view->GetClassName()] = contents_view;

    contents_focused_subscriptions_.push_back(
        contents_container_view_for_web_panel_->contents_view()
            ->AddWebContentsFocusedCallback(base::BindRepeating(
                &BraveMultiContentsView::OnWebContentsFocused,
                base::Unretained(this))));
    DevtoolsUIController::From(browser_view_->browser())
        ->MakeSureControllerExists(contents_container_view_for_web_panel_);
  }
}

#if BUILDFLAG(IS_BRAVE_ORIGIN_BRANDED)
void BraveMultiContentsView::SetOriginSideView(
    std::unique_ptr<views::View> side_view,
    const gfx::Insets& web_panel_insets) {
  CHECK(!origin_side_view_);
  origin_side_view_ = AddChildView(std::move(side_view));
  origin_web_panel_insets_ = web_panel_insets;
  // The side page draws over the side view's body. Keep the web panel above it
  // so clicks in the page reach the page rather than the view behind it.
  if (contents_container_view_for_web_panel_) {
    ReorderChildView(contents_container_view_for_web_panel_, children().size());
  }
  InvalidateLayout();
}

void BraveMultiContentsView::SetOriginSideViewWidth(int width) {
  if (origin_side_view_width_ == width) {
    return;
  }
  origin_side_view_width_ = width;
  InvalidateLayout();
}

bool BraveMultiContentsView::IsOriginSideViewVisible() const {
  return origin_side_view_ && origin_side_view_->GetVisible();
}

views::ProposedLayout BraveMultiContentsView::CalculateOriginSideViewLayout(
    const views::SizeBounds& size_bounds) const {
  const int host_width = size_bounds.width().value();
  const int host_height = size_bounds.height().value();
  const gfx::Rect host_rect(host_width, host_height);

  // Fullscreen video takes the whole window. The side view steps aside, unless
  // the video is playing in the side page, which then takes the whole area.
  auto* exclusive_access_manager =
      ExclusiveAccessManager::From(browser_view_->browser());
  if (exclusive_access_manager &&
      exclusive_access_manager->fullscreen_controller()->IsTabFullscreen()) {
    views::ProposedLayout layouts =
        MultiContentsView::CalculateProposedLayout(size_bounds);
    if (auto* background_view_layout =
            layouts.GetLayoutFor(background_view_.get())) {
      background_view_layout->visible = false;
    }
    layouts.child_layouts.emplace_back(origin_side_view_.get(), false,
                                       gfx::Rect());
    if (contents_container_view_for_web_panel_) {
      const content::WebContents* side_page =
          contents_container_view_for_web_panel_->contents_view()
              ->web_contents();
      const bool side_page_fullscreen =
          side_page &&
          exclusive_access_manager->fullscreen_controller()
                  ->GetFullscreenState(side_page)
                  .target_mode == content::FullscreenMode::kContent;
      layouts.child_layouts.emplace_back(
          contents_container_view_for_web_panel_.get(),
          side_page_fullscreen &&
              contents_container_view_for_web_panel_->GetVisible(),
          side_page_fullscreen ? host_rect : gfx::Rect());
    }
    layouts.host_size = host_rect.size();
    return layouts;
  }

  // Narrow windows keep a usable page; the side view gives way first.
  const int column_width =
      std::clamp(origin_side_view_width_, 0,
                 std::max(0, host_width - kOriginMinimumMainWidth));

  views::SizeBounds main_size_bounds = size_bounds;
  main_size_bounds.Enlarge(-column_width, 0);
  views::ProposedLayout layouts =
      MultiContentsView::CalculateProposedLayout(main_size_bounds);
  // See CalculateProposedLayout() for why the background stays hidden.
  if (auto* background_view_layout =
          layouts.GetLayoutFor(background_view_.get())) {
    background_view_layout->visible = false;
  }

  const gfx::Rect column(host_width - column_width, 0, column_width,
                         host_height);
  layouts.child_layouts.emplace_back(origin_side_view_.get(), true, column);
  if (contents_container_view_for_web_panel_) {
    gfx::Rect web_panel_rect = column;
    web_panel_rect.Inset(origin_web_panel_insets_);
    layouts.child_layouts.emplace_back(
        contents_container_view_for_web_panel_.get(),
        contents_container_view_for_web_panel_->GetVisible(), web_panel_rect);
  }
  layouts.host_size = gfx::Size(host_width, host_height);
  return layouts;
}
#endif

void BraveMultiContentsView::SetWebPanelContents(
    content::WebContents* web_contents) {
  CHECK(contents_container_view_for_web_panel_);
  contents_container_view_for_web_panel_->contents_view()->SetWebContents(
      web_contents);
  contents_container_view_for_web_panel_->SetVisible(web_contents);
  UpdateContentsBorderAndOverlay();
}

bool BraveMultiContentsView::IsWebPanelVisible() const {
  CHECK(contents_container_view_for_web_panel_);
  return contents_container_view_for_web_panel_->GetVisible();
}

views::View*
BraveMultiContentsView::GetWebPanelContentsViewForTesting()  // IN-TEST
    const {
  return contents_container_view_for_web_panel_
             ? contents_container_view_for_web_panel_->contents_view()
             : nullptr;
}

void BraveMultiContentsView::SetWebPanelWidth(int width) {
  web_panel_width_ = width;
  InvalidateLayout();
}

void BraveMultiContentsView::SetWebPanelOnLeft(bool left) {
  web_panel_on_left_ = left;
  InvalidateLayout();
}

void BraveMultiContentsView::OnShowActiveContentsDomainChanged() {
  UpdateContentsBorderAndOverlay();
}

void BraveMultiContentsView::UpdateContentsCornerRadii(
    const gfx::RoundedCornersF& corner_radii) {
  if (contents_container_view_for_web_panel_) {
    contents_container_view_for_web_panel_->SetContentsCornerRadii(
        corner_radii);
  }

  const bool is_in_split = IsInSplitView();
  const size_t view_count = contents_container_views_.size();
  for (size_t i = 0; i < view_count; ++i) {
    auto* container_view =
        BraveContentsContainerView::From(contents_container_views_[i]);
    if (is_in_split) {
      const bool is_first = i == 0;
      const bool is_last = i == view_count - 1;
      container_view->SetContentsCornerRadii(
          GetSplitContentsCornerRadii(corner_radii, is_first, is_last));
    } else {
      container_view->SetContentsCornerRadii(corner_radii);
    }
  }

  UpdateContentsBorderAndOverlay();
}

gfx::Rect BraveMultiContentsView::GetMainContentsBounds() const {
  gfx::Rect bounds;
  for (const auto& container : contents_container_views_) {
    if (container->GetVisible()) {
      bounds.Union(container->bounds());
    }
  }
  return bounds;
}

views::ProposedLayout BraveMultiContentsView::CalculateProposedLayout(
    const views::SizeBounds& size_bounds) const {
  if (!size_bounds.is_fully_bounded()) {
    return MultiContentsView::CalculateProposedLayout(size_bounds);
  }

#if BUILDFLAG(IS_BRAVE_ORIGIN_BRANDED)
  if (IsOriginSideViewVisible()) {
    return CalculateOriginSideViewLayout(size_bounds);
  }
#endif

  views::SizeBounds new_size_bounds = size_bounds;

  // Negative to shrink to make room for web panel.
  const int web_panel_width = GetWebPanelWidth();
  new_size_bounds.Enlarge(-web_panel_width, 0);
  views::ProposedLayout layouts =
      MultiContentsView::CalculateProposedLayout(new_size_bounds);

  // Always hide |background_view_|. Due to layered |background_view_|,
  // our custom border drawn by BraveContentsContainerView is not visible
  // because BraveContentsContainerView doesn't have layer and its parent
  // layer is behind the |background_view_|.
  // We can handle this by having BraveContentsContainerView's own layer but
  // it's resource waste because we don't need |background_view_|. We alreay
  // have CustomBackground that fills contents area with toolbar color.
  auto* background_view_layout = layouts.GetLayoutFor(background_view_.get());
  CHECK(background_view_layout);
  background_view_layout->visible = false;

  if (!contents_container_view_for_web_panel_) {
#if BUILDFLAG(IS_BRAVE_ORIGIN_BRANDED)
    if (origin_side_view_) {
      layouts.child_layouts.emplace_back(origin_side_view_.get(), false,
                                         gfx::Rect());
    }
#endif
    return layouts;
  }

  if (web_panel_on_left_) {
    for (auto& layout : layouts.child_layouts) {
      // Move all other views to right to put web panel on left side.
      layout.bounds.Offset(web_panel_width, 0);
    }
  }

  int host_width = size_bounds.width().value();
  int host_height = size_bounds.height().value();
  const int web_panel_x = web_panel_on_left_ ? 0 : host_width - web_panel_width;
  gfx::Rect web_panel_rect(web_panel_x, 0, web_panel_width, host_height);
  layouts.child_layouts.emplace_back(
      contents_container_view_for_web_panel_.get(),
      contents_container_view_for_web_panel_->GetVisible(), web_panel_rect);
#if BUILDFLAG(IS_BRAVE_ORIGIN_BRANDED)
  if (origin_side_view_) {
    layouts.child_layouts.emplace_back(origin_side_view_.get(), false,
                                       gfx::Rect());
  }
#endif

  layouts.host_size = gfx::Size(host_width, host_height);
  return layouts;
}

void BraveMultiContentsView::ResetResizeArea() {
  // Give same width on both contents view.
  // Pass true to make delegate save ratio in session service like resizing
  // complete.
  delegate_->ResizeWebContents(0.5, /*done_resizing=*/true);
}

void BraveMultiContentsView::UpdateContentsBorderAndOverlay() {
  if (!contents_container_view_for_web_panel_ ||
      !contents_container_view_for_web_panel_->GetVisible()) {
    MultiContentsView::UpdateContentsBorderAndOverlay();
    return;
  }

  if (!contents_container_view_for_web_panel_->IsActive()) {
    // Web panel is visible but inactive. Hide border of web panel.
    contents_container_view_for_web_panel_->UpdateBorderAndOverlay(
        /*is_in_split*/ false, /*is_active*/ false,
        /*is_highlighted*/ false);
    MultiContentsView::UpdateContentsBorderAndOverlay();
    return;
  }

  // When web panel is active, only it should have active border.
  contents_container_view_for_web_panel_->UpdateBorderAndOverlay(
      /*is_in_split*/ false, /*is_active*/ true,
      /*is_highlighted*/ false);

  for (ContentsContainerView* contents_container_view :
       contents_container_views_) {
    contents_container_view->UpdateBorderAndOverlay(IsInSplitView(),
                                                    /*is_active*/ false,
                                                    /*is_highlighted*/ false);
  }
}

void BraveMultiContentsView::OnWebContentsFocused(views::WebView* web_view) {
  // Early return if web panel is not used.
  if (!contents_container_view_for_web_panel_ ||
      !contents_container_view_for_web_panel_->GetVisible()) {
    MultiContentsView::OnWebContentsFocused(web_view);
    return;
  }

  // When a tab is detached, focus manager could make another web contents
  // focused. We don't need to make web contents' tab activated here. Next
  // active tab could be selected by tab strip model and its web contents will
  // get focused.
  if (!GetActiveContentsView()->web_contents()) {
    return;
  }

  // When tab is activated from Tab UI, we don't need to ask activate
  // |web_view|'s contents to TabStripModel again. It's not activated here when
  // activating tab by clicking contents.
  if (auto* tab =
          tabs::TabInterface::MaybeGetFromContents(web_view->web_contents());
      tab && tab->IsActivated()) {
    return;
  }

  // Base class only gives focus for inactive split tab because that inactive
  // split tab could get focused in upstream.
  // With web panel feature, other tabs also could get focused.
  // When web panel has focus, previously active split tab could get focus.
  // Also, web panel's tab also needs focus.
  // So, notify always.
  delegate_->WebContentsFocused(web_view->web_contents());
}

void BraveMultiContentsView::ExecuteOnEachVisibleContentsView(
    base::RepeatingCallback<void(ContentsWebView*)> callback) {
  if (contents_container_view_for_web_panel_ &&
      contents_container_view_for_web_panel_->GetVisible()) {
    callback.Run(contents_container_view_for_web_panel_->contents_view());
  }

  MultiContentsView::ExecuteOnEachVisibleContentsView(callback);
}

int BraveMultiContentsView::GetWebPanelWidth() const {
  if (!contents_container_view_for_web_panel_ ||
      !contents_container_view_for_web_panel_->GetVisible()) {
    return 0;
  }

  return web_panel_width_;
}

ContentsContainerView* BraveMultiContentsView::GetActiveContentsContainerView()
    const {
  if (is_web_panel_active_) {
    return contents_container_view_for_web_panel_;
  }

  return MultiContentsView::GetActiveContentsContainerView();
}

ContentsWebView* BraveMultiContentsView::GetActiveContentsView() const {
  if (is_web_panel_active_) {
    return contents_container_view_for_web_panel_->contents_view();
  }

  return MultiContentsView::GetActiveContentsView();
}

ContentsContainerView* BraveMultiContentsView::GetContentsContainerViewFor(
    content::WebContents* web_contents) const {
  if (is_web_panel_active_ &&
      contents_container_view_for_web_panel_->contents_view()->web_contents() ==
          web_contents) {
    return contents_container_view_for_web_panel_;
  }

  return MultiContentsView::GetContentsContainerViewFor(web_contents);
}

BEGIN_METADATA(BraveMultiContentsView)
END_METADATA
