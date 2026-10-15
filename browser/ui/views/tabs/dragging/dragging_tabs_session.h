/* Copyright (c) 2025 The Brave Authors. All rights reserved.
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at https://mozilla.org/MPL/2.0/. */

#ifndef BRAVE_BROWSER_UI_VIEWS_TABS_DRAGGING_DRAGGING_TABS_SESSION_H_
#define BRAVE_BROWSER_UI_VIEWS_TABS_DRAGGING_DRAGGING_TABS_SESSION_H_

#include "base/memory/raw_ptr.h"
#include "chrome/browser/ui/views/tabs/dragging/dragging_tabs_session.h"

class BraveTab;

namespace content {
class WebContents;
}

class DraggingTabsSession : public DraggingTabsSessionChromium {
 public:
  explicit DraggingTabsSession(
      DragSessionData drag_data,
      TabDragContext& attached_context,
      TabDragPositioningDelegate& drag_position_delegate,
      bool initial_move,
      gfx::Point point_in_screen);
  ~DraggingTabsSession() override;

  void set_mouse_y_offset(int offset) { mouse_y_offset_ = offset; }
  void set_is_showing_vertical_tabs(bool show) {
    is_showing_vertical_tabs_ = show;
  }

  // DraggingTabSessionChromium:
  gfx::Point GetAttachedDragPoint(gfx::Point point_in_screen) override;
  void MoveAttached(gfx::Point point_in_screen) override;
  std::optional<tab_groups::TabGroupId> CalculateGroupForDraggedTabs(
      int to_index) override;

  content::WebContents* origin_hierarchy_drop_target_contents() const {
    return origin_hierarchy_drop_target_contents_;
  }

 private:
  void UpdateOriginHierarchyDropTarget(const gfx::Point& point_in_screen);
  void ClearOriginHierarchyDropTarget();

  int mouse_y_offset_ = 0;
  bool is_showing_vertical_tabs_ = false;
  raw_ptr<BraveTab> origin_hierarchy_drop_target_view_ = nullptr;
  raw_ptr<content::WebContents> origin_hierarchy_drop_target_contents_ =
      nullptr;
};

#endif  // BRAVE_BROWSER_UI_VIEWS_TABS_DRAGGING_DRAGGING_TABS_SESSION_H_
