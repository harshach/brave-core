// Copyright (c) 2026 The Brave Authors. All rights reserved.
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this file,
// You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef BRAVE_BROWSER_UI_VIEWS_FRAME_VERTICAL_TABS_ORIGIN_SIDEBAR_STRIPS_H_
#define BRAVE_BROWSER_UI_VIEWS_FRAME_VERTICAL_TABS_ORIGIN_SIDEBAR_STRIPS_H_

#include <map>
#include <memory>
#include <optional>
#include <string>

#include "base/functional/callback.h"
#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "base/scoped_observation.h"
#include "base/time/time.h"
#include "base/timer/timer.h"
#include "brave/browser/ui/tabs/origin_download_summary.h"
#include "brave/browser/ui/tabs/origin_later_service.h"
#include "brave/browser/ui/tabs/origin_media_monitor.h"
#include "components/sessions/core/session_id.h"
#include "ui/base/metadata/metadata_header_macros.h"
#include "ui/views/view.h"

class BrowserWindowInterface;

// Short rows stacked just above the Spaces row, for things SigmaOS surfaces
// beside its page list: what is playing, a download in progress, a reminder
// that came due, and brief notices such as "Snoozed until Tomorrow 9:00 AM".
// Each row shows only while it has something to say, so with nothing going
// on the stack takes no room and the page list runs down to the Spaces row.
class OriginSidebarStrips : public views::View,
                            public OriginMediaMonitor::Observer,
                            public OriginDownloadSummary::Observer,
                            public OriginLaterService::Observer {
  METADATA_HEADER(OriginSidebarStrips, views::View)

 public:
  struct Callbacks {
    base::RepeatingClosure show_downloads;
    base::RepeatingClosure show_reminders;
  };

  // `media_monitor` and `later_service` may be null.
  OriginSidebarStrips(BrowserWindowInterface* browser,
                      OriginMediaMonitor* media_monitor,
                      OriginLaterService* later_service,
                      Callbacks callbacks);
  OriginSidebarStrips(const OriginSidebarStrips&) = delete;
  OriginSidebarStrips& operator=(const OriginSidebarStrips&) = delete;
  ~OriginSidebarStrips() override;

  // A one-line notice that fades after a few seconds. A click runs
  // `on_click` when one is given.
  void ShowNotice(std::u16string text,
                  base::RepeatingClosure on_click = base::RepeatingClosure());

  views::View* notice_row_for_testing();
  views::View* reminder_row_for_testing();
  views::View* download_row_for_testing();
  views::View* media_row_for_testing();

  // views::View:
  void OnThemeChanged() override;

 private:
  class NoticeRow;
  class ReminderRow;
  class DownloadRow;
  class MediaRow;

  // OriginMediaMonitor::Observer:
  void OnOriginMediaChanged() override;
  // OriginDownloadSummary::Observer:
  void OnOriginDownloadSummaryChanged() override;
  // OriginLaterService::Observer:
  void OnOriginLaterChanged() override;
  void OnSnoozedPageReturned(content::WebContents* contents) override;

  void UpdateMedia();
  void UpdateDownload();
  void UpdateReminder();
  void SetRowVisible(views::View* row, bool visible);

  raw_ptr<BrowserWindowInterface> browser_ = nullptr;
  raw_ptr<OriginMediaMonitor> media_monitor_ = nullptr;
  raw_ptr<OriginLaterService> later_service_ = nullptr;
  std::unique_ptr<OriginDownloadSummary> download_summary_;
  Callbacks callbacks_;

  raw_ptr<NoticeRow> notice_row_ = nullptr;
  raw_ptr<ReminderRow> reminder_row_ = nullptr;
  raw_ptr<DownloadRow> download_row_ = nullptr;
  raw_ptr<MediaRow> media_row_ = nullptr;

  base::OneShotTimer notice_timer_;
  // Paused media stays reachable for a while after it stops.
  std::map<SessionID, base::TimeTicks> last_heard_playing_;
  base::OneShotTimer media_linger_timer_;

  base::ScopedObservation<OriginMediaMonitor, OriginMediaMonitor::Observer>
      media_observation_{this};
  base::ScopedObservation<OriginDownloadSummary,
                          OriginDownloadSummary::Observer>
      download_observation_{this};
  base::ScopedObservation<OriginLaterService, OriginLaterService::Observer>
      later_observation_{this};
  base::WeakPtrFactory<OriginSidebarStrips> weak_factory_{this};
};

#endif  // BRAVE_BROWSER_UI_VIEWS_FRAME_VERTICAL_TABS_ORIGIN_SIDEBAR_STRIPS_H_
