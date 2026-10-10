// Copyright (c) 2026 The Brave Authors. All rights reserved.
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this file,
// You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef BRAVE_BROWSER_UI_VIEWS_FRAME_ORIGIN_SIDE_VIEW_SNOOZED_H_
#define BRAVE_BROWSER_UI_VIEWS_FRAME_ORIGIN_SIDE_VIEW_SNOOZED_H_

#include <optional>
#include <string>
#include <vector>

#include "base/functional/callback.h"
#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "base/scoped_observation.h"
#include "base/time/time.h"
#include "base/timer/timer.h"
#include "brave/browser/ui/tabs/origin_later_service.h"
#include "ui/base/metadata/metadata_header_macros.h"
#include "ui/views/view.h"

namespace views {
class BoxLayoutView;
class ImageView;
class Label;
class ScrollView;
}  // namespace views

// `time` in local time, as the side view's later tools show it:
// "Today 6:00 PM", "Tomorrow 9:00 AM", "Yesterday 9:00 AM", or
// "Mon, Oct 13, 9:00 AM" with the year added when it isn't `now`'s.
std::u16string FormatOriginLaterTime(base::Time time, base::Time now);

// The side view's Snoozed tool: pages put away until later, soonest first.
// Each can be woken early or let go.
class OriginSideViewSnoozed : public views::View,
                              public OriginLaterService::Observer {
  METADATA_HEADER(OriginSideViewSnoozed, views::View)

 public:
  // A Space's name, or an empty string for a Space that is gone.
  using SpaceNameCallback =
      base::RepeatingCallback<std::u16string(const std::string& space_id)>;

  // `service` must outlive this view.
  OriginSideViewSnoozed(OriginLaterService* service,
                        SpaceNameCallback space_name);
  OriginSideViewSnoozed(const OriginSideViewSnoozed&) = delete;
  OriginSideViewSnoozed& operator=(const OriginSideViewSnoozed&) = delete;
  ~OriginSideViewSnoozed() override;

  size_t GetRowCountForTesting() const { return rows_.size(); }
  views::View* empty_state_for_testing() { return empty_state_; }
  // Takes effect from the next update.
  void SetNowForTesting(base::Time now) { now_for_testing_ = now; }

  // views::View:
  void OnThemeChanged() override;
  void AddedToWidget() override;
  void RemovedFromWidget() override;
  void VisibilityChanged(views::View* starting_from, bool is_visible) override;

 private:
  class Row;

  // Which of a row's buttons had keyboard focus before a rebuild.
  struct FocusedAction {
    std::string id;
    size_t row = 0;
    size_t action = 0;
  };

  // OriginLaterService::Observer:
  void OnOriginLaterChanged() override;

  base::Time Now() const;
  std::u16string GetSpaceName(const std::string& space_id) const;
  void WakeNow(const std::string& id);
  void Remove(const std::string& id);

  // The service can notify from inside a row's own button handler, so
  // changes only mark what is stale; rows are touched from a posted task.
  void ScheduleUpdate();
  void ApplyUpdates();
  void Rebuild();
  void RefreshRows();
  void UpdateRefreshTimer();
  void UpdateColors();
  std::optional<FocusedAction> GetFocusedAction();
  void RestoreFocus(const FocusedAction& focus);

  const raw_ptr<OriginLaterService> service_;
  const SpaceNameCallback space_name_;
  std::optional<base::Time> now_for_testing_;

  raw_ptr<views::ScrollView> scroll_ = nullptr;
  raw_ptr<views::BoxLayoutView> list_ = nullptr;
  std::vector<raw_ptr<Row>> rows_;

  raw_ptr<views::View> empty_state_ = nullptr;
  raw_ptr<views::ImageView> empty_icon_ = nullptr;
  raw_ptr<views::Label> empty_title_ = nullptr;
  raw_ptr<views::Label> empty_body_ = nullptr;

  bool needs_rebuild_ = false;
  bool needs_refresh_ = false;
  bool update_scheduled_ = false;
  // Runs exactly while the list is on screen, so "Today" and "Tomorrow" roll
  // over at midnight and renamed Spaces catch up.
  base::RepeatingTimer refresh_timer_;

  base::ScopedObservation<OriginLaterService, OriginLaterService::Observer>
      later_observation_{this};
  base::WeakPtrFactory<OriginSideViewSnoozed> weak_factory_{this};
};

#endif  // BRAVE_BROWSER_UI_VIEWS_FRAME_ORIGIN_SIDE_VIEW_SNOOZED_H_
