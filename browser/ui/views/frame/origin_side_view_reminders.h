// Copyright (c) 2026 The Brave Authors. All rights reserved.
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this file,
// You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef BRAVE_BROWSER_UI_VIEWS_FRAME_ORIGIN_SIDE_VIEW_REMINDERS_H_
#define BRAVE_BROWSER_UI_VIEWS_FRAME_ORIGIN_SIDE_VIEW_REMINDERS_H_

#include <optional>
#include <string>
#include <string_view>
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
#include "url/gurl.h"

namespace views {
class BoxLayoutView;
class ImageView;
class Label;
class ScrollView;
}  // namespace views

// The side view's Reminders tool: what is due, what is coming up, and what
// was done lately. A reminder can be checked off or deleted, and opened when
// it is about a page.
class OriginSideViewReminders : public views::View,
                                public OriginLaterService::Observer {
  METADATA_HEADER(OriginSideViewReminders, views::View)

 public:
  enum class Section {
    // Not done, and due now or earlier.
    kDue,
    kUpcoming,
    kDone,
  };

  // Older done reminders stay with the service, just not in the list.
  static constexpr size_t kMaxDoneRows = 20;

  using OpenUrlCallback = base::RepeatingCallback<void(const GURL& url)>;

  // `service` must outlive this view. `open_url` opens a reminder's page.
  OriginSideViewReminders(OriginLaterService* service,
                          OpenUrlCallback open_url);
  OriginSideViewReminders(const OriginSideViewReminders&) = delete;
  OriginSideViewReminders& operator=(const OriginSideViewReminders&) = delete;
  ~OriginSideViewReminders() override;

  size_t GetRowCountForTesting() const { return rows_.size(); }
  size_t GetSectionRowCountForTesting(Section section) const;
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
  void SetDone(const std::string& id, bool done);
  void OpenPage(const GURL& url);
  void Remove(const std::string& id);

  // The service can notify from inside a row's own button handler, so
  // changes only mark what is stale; rows are touched from a posted task.
  void ScheduleUpdate();
  void ApplyUpdates();
  void Rebuild();
  void AddSection(
      std::u16string_view title,
      Section section,
      const std::vector<const OriginLaterService::Reminder*>& reminders,
      base::Time now);
  void RefreshRows();
  void UpdateRefreshTimer();
  void UpdateColors();
  std::optional<FocusedAction> GetFocusedAction();
  void RestoreFocus(const FocusedAction& focus);

  const raw_ptr<OriginLaterService> service_;
  const OpenUrlCallback open_url_;
  std::optional<base::Time> now_for_testing_;

  raw_ptr<views::ScrollView> scroll_ = nullptr;
  raw_ptr<views::BoxLayoutView> list_ = nullptr;
  std::vector<raw_ptr<views::Label>> section_titles_;
  std::vector<raw_ptr<Row>> rows_;

  raw_ptr<views::View> empty_state_ = nullptr;
  raw_ptr<views::ImageView> empty_icon_ = nullptr;
  raw_ptr<views::Label> empty_title_ = nullptr;
  raw_ptr<views::Label> empty_body_ = nullptr;

  bool needs_rebuild_ = false;
  bool needs_refresh_ = false;
  bool update_scheduled_ = false;
  // Runs exactly while the list is on screen, so "Today" and "Tomorrow" roll
  // over at midnight.
  base::RepeatingTimer refresh_timer_;

  base::ScopedObservation<OriginLaterService, OriginLaterService::Observer>
      later_observation_{this};
  base::WeakPtrFactory<OriginSideViewReminders> weak_factory_{this};
};

#endif  // BRAVE_BROWSER_UI_VIEWS_FRAME_ORIGIN_SIDE_VIEW_REMINDERS_H_
