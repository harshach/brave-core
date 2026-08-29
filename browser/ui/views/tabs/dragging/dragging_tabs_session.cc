/* Copyright (c) 2025 The Brave Authors. All rights reserved.
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at https://mozilla.org/MPL/2.0/. */

#include "brave/browser/ui/views/tabs/dragging/dragging_tabs_session.h"

#include <algorithm>
#include <optional>
#include <vector>

#include "base/check.h"
#include "base/containers/flat_set.h"
#include "brave/browser/ui/tabs/brave_tab_strip_model.h"
#include "brave/browser/ui/views/tabs/brave_tab.h"
#include "brave/components/brave_origin/buildflags/buildflags.h"
#include "brave/components/tabs/public/tree_tab_node_tab_collection.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/browser/ui/views/tabs/dragging/drag_session_data.h"
#include "chrome/browser/ui/views/tabs/dragging/tab_drag_context.h"
#include "chrome/browser/ui/views/tabs/tab.h"
#include "ui/gfx/geometry/insets.h"
#include "ui/gfx/geometry/point.h"
#include "ui/gfx/geometry/rect.h"
#include "ui/views/view_utils.h"

DraggingTabsSession::DraggingTabsSession(
    DragSessionData drag_data,
    TabDragContext& attached_context,
    TabDragPositioningDelegate& drag_position_delegate,
    bool initial_move,
    gfx::Point point_in_screen)
    : DraggingTabsSessionChromium(drag_data,
                                  attached_context,
                                  drag_position_delegate,
                                  initial_move,
                                  point_in_screen) {}

DraggingTabsSession::~DraggingTabsSession() {
  ClearOriginHierarchyDropTarget();
}

gfx::Point DraggingTabsSession::GetAttachedDragPoint(
    gfx::Point point_in_screen) {
  if (!is_showing_vertical_tabs_) {
    return DraggingTabsSessionChromium::GetAttachedDragPoint(point_in_screen);
  }

  gfx::Point tab_loc(point_in_screen);
  views::View::ConvertPointFromScreen(base::to_address(attached_context_),
                                      &tab_loc);
  int x = tab_loc.x() - mouse_offset_;
  if (!drag_data_.tab_drag_data_.front().pinned) {
    // Vertical tree tabs used to force x to zero, making an outdent gesture
    // impossible. Keep the normal position at zero, but expose a bounded
    // negative delta while the pointer is dragged left through the tab's tree
    // indentation. TabStrip uses that delta to select root-only insertion
    // boundaries, and the dragged page visibly follows the pointer into the
    // root lane.
    const std::vector<gfx::Rect> bounds =
        drag_position_delegate_->CalculateBoundsForDraggedViews(
            drag_data_.attached_views());
    const int nesting_offset = bounds.empty() ? 0 : bounds.front().x();
    x = std::clamp(x - nesting_offset, -nesting_offset, 0);
  }
  const int y = tab_loc.y() - mouse_y_offset_;
  return {x, y};
}

void DraggingTabsSession::MoveAttached(gfx::Point point_in_screen) {
  DraggingTabsSessionChromium::MoveAttached(point_in_screen);
  if (!is_showing_vertical_tabs_) {
    return;
  }

  UpdateOriginHierarchyDropTarget(point_in_screen);

  // Unlike upstream, We always update coordinate, as we use y coordinate. Since
  // we don't have threshold there's no any harm for this.
  views::View::ConvertPointFromScreen(base::to_address(attached_context_),
                                      &point_in_screen);
  last_move_attached_context_loc_ = point_in_screen.y();
}

void DraggingTabsSession::UpdateOriginHierarchyDropTarget(
    const gfx::Point& point_in_screen) {
#if BUILDFLAG(IS_BRAVE_ORIGIN_BRANDED)
  auto* model =
      static_cast<BraveTabStripModel*>(attached_context_->GetTabStripModel());
  if (!model || !model->tree_model() ||
      drag_data_.group_header_drag_data_.has_value()) {
    ClearOriginHierarchyDropTarget();
    return;
  }

  content::WebContents* source_contents =
      drag_data_.source_view_drag_data()->contents;
  const int source_index = model->GetIndexOfWebContents(source_contents);
  if (!model->ContainsIndex(source_index) || model->IsTabPinned(source_index)) {
    ClearOriginHierarchyDropTarget();
    return;
  }

  tabs::TabInterface* source_tab = model->GetTabAtIndex(source_index);
  auto* source_node =
      tabs::TreeTabNodeTabCollection::GetNearestTreeTabNodeCollection(
          source_tab);
  if (!source_node) {
    ClearOriginHierarchyDropTarget();
    return;
  }

  base::flat_set<content::WebContents*> dragged_contents;
  for (const TabDragData& drag_data : drag_data_.tab_drag_data_) {
    if (drag_data.contents) {
      dragged_contents.insert(drag_data.contents);
    }
  }

  BraveTab* candidate_view = nullptr;
  content::WebContents* candidate_contents = nullptr;
  for (int index = 0; index < model->count(); ++index) {
    content::WebContents* contents = model->GetWebContentsAt(index);
    if (model->IsTabPinned(index) || dragged_contents.contains(contents)) {
      continue;
    }

    auto* tab_view =
        views::AsViewClass<BraveTab>(drag_position_delegate_->GetTabAt(index));
    if (!tab_view || !tab_view->GetVisible() || tab_view->closing()) {
      continue;
    }

    gfx::Rect nest_zone = tab_view->GetBoundsInScreen();
    nest_zone.Inset(gfx::Insets::VH(std::max(4, nest_zone.height() / 4), 0));
    nest_zone.set_x(nest_zone.x() +
                    std::min(24, std::max(8, nest_zone.width() / 6)));
    if (!nest_zone.Contains(point_in_screen)) {
      continue;
    }

    auto* target_node =
        tabs::TreeTabNodeTabCollection::GetNearestTreeTabNodeCollection(
            model->GetTabAtIndex(index));
    if (!target_node || target_node == source_node ||
        source_node->GetParentCollection() == target_node) {
      continue;
    }
    bool creates_cycle = false;
    for (tabs::TabCollection* ancestor = target_node; ancestor;
         ancestor = ancestor->GetParentCollection()) {
      if (ancestor == source_node) {
        creates_cycle = true;
        break;
      }
    }
    if (creates_cycle) {
      continue;
    }

    candidate_view = tab_view;
    candidate_contents = contents;
    break;
  }

  if (candidate_view == origin_hierarchy_drop_target_view_) {
    origin_hierarchy_drop_target_contents_ = candidate_contents;
    return;
  }
  ClearOriginHierarchyDropTarget();
  origin_hierarchy_drop_target_view_ = candidate_view;
  origin_hierarchy_drop_target_contents_ = candidate_contents;
  if (origin_hierarchy_drop_target_view_) {
    origin_hierarchy_drop_target_view_->SetOriginHierarchyDropTarget(true);
  }
#else
  ClearOriginHierarchyDropTarget();
#endif
}

void DraggingTabsSession::ClearOriginHierarchyDropTarget() {
  if (origin_hierarchy_drop_target_view_) {
    origin_hierarchy_drop_target_view_->SetOriginHierarchyDropTarget(false);
  }
  origin_hierarchy_drop_target_view_ = nullptr;
  origin_hierarchy_drop_target_contents_ = nullptr;
}

std::optional<tab_groups::TabGroupId>
DraggingTabsSession::CalculateGroupForDraggedTabs(int to_index) {
  if (!is_showing_vertical_tabs_) {
    return DraggingTabsSessionChromium::CalculateGroupForDraggedTabs(to_index);
  }

  TabStripModel* attached_model = attached_context_->GetTabStripModel();

  // If a group is moved, the drag cannot be inserted into another group.
  for (const TabDragData& tab_drag_datum : drag_data_.tab_drag_data_) {
    if (tab_drag_datum.view_type == TabSlotView::ViewType::kTabGroupHeader) {
      return std::nullopt;
    }
  }

  // Pinned tabs cannot be grouped, so we only change the group membership of
  // unpinned tabs.
  std::vector<int> selected_unpinned;
  for (size_t selected_index : attached_model->selection_model()
                                   .GetListSelectionModel()
                                   .selected_indices()) {
    if (!attached_model->IsTabPinned(selected_index)) {
      selected_unpinned.push_back(selected_index);
    }
  }

  if (selected_unpinned.empty()) {
    return std::nullopt;
  }

  // Get the proposed tabstrip model assuming the selection has taken place.
  auto [previous_index, next_index] =
      attached_model->GetAdjacentTabsAfterSelectedMove(GetPassKey(), to_index);
  std::optional<tab_groups::TabGroupId> previous_group =
      previous_index.has_value()
          ? attached_model->GetTabGroupForTab(previous_index.value())
          : std::nullopt;
  std::optional<tab_groups::TabGroupId> next_group =
      next_index.has_value()
          ? attached_model->GetTabGroupForTab(next_index.value())
          : std::nullopt;
  std::optional<tab_groups::TabGroupId> current_group =
      attached_model->GetTabGroupForTab(selected_unpinned[0]);

  // We're in the middle of two tabs with the same group membership, or both
  // sides are ungrouped.
  if (previous_group == next_group) {
    return previous_group;
  }

  // If the tabs on the previous and next have different group memberships,
  // including if one is ungrouped or nonexistent, change the group of the
  // dragged tab based on whether it is "leaning" toward the previous or the
  // next of the gap. If the tab is centered in the gap, make the tab
  // ungrouped.

  const TabSlotView* top_most_selected_tab =
      drag_position_delegate_->GetTabAt(selected_unpinned[0]);

  const int buffer = top_most_selected_tab->height() / 4;

  const auto tab_bounds_in_drag_context_coords = [this](int model_index) {
    const TabSlotView* const tab =
        drag_position_delegate_->GetTabAt(model_index);
    return ToEnclosingRect(views::View::ConvertRectToTarget(
        tab->parent(), base::to_address(attached_context_),
        gfx::RectF(tab->bounds())));
  };

  // Use the top edge for a reliable fallback, e.g. if this is the topmost
  // tab or there is a group header to the immediate top.
  int top_edge =
      previous_index.has_value()
          ? tab_bounds_in_drag_context_coords(previous_index.value()).bottom()
          : 0;

  // Extra polish: Prefer staying in an existing group, if any. This prevents
  // tabs at the edge of the group from flickering between grouped and
  // ungrouped. It also gives groups a slightly "sticky" feel while dragging.
  if (previous_group.has_value() && previous_group == current_group) {
    top_edge += buffer;
  }
  if (next_group.has_value() && next_group == current_group && top_edge > 0) {
    top_edge -= buffer;
  }

  const int top_most_selected_y_position = top_most_selected_tab->y();

  if (previous_group.has_value() &&
      !attached_model->IsGroupCollapsed(previous_group.value()) &&
      top_most_selected_y_position <= top_edge - buffer) {
    return previous_group;
  }
  if ((top_most_selected_y_position >= top_edge + buffer) &&
      next_group.has_value() &&
      !attached_model->IsGroupCollapsed(next_group.value())) {
    return next_group;
  }
  return std::nullopt;
}
