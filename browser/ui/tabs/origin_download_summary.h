// Copyright (c) 2026 The Brave Authors. All rights reserved.
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this file,
// You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef BRAVE_BROWSER_UI_TABS_ORIGIN_DOWNLOAD_SUMMARY_H_
#define BRAVE_BROWSER_UI_TABS_ORIGIN_DOWNLOAD_SUMMARY_H_

#include <stdint.h>

#include <memory>
#include <optional>
#include <set>
#include <string>

#include "base/memory/raw_ptr.h"
#include "base/observer_list.h"
#include "base/observer_list_types.h"
#include "base/time/time.h"
#include "base/timer/timer.h"
#include "components/download/content/public/all_download_item_notifier.h"

class Profile;

// What one profile is downloading right now, condensed for the sidebar's thin
// progress strip. Transient downloads, which the browser makes for itself and
// never lists, don't count.
class OriginDownloadSummary
    : public download::AllDownloadItemNotifier::Observer {
 public:
  // How long "Downloaded <name>" stays up after the last download finishes.
  static constexpr base::TimeDelta kLingerDuration = base::Seconds(4);
  // Bytes arrive far more often than a strip can usefully redraw, so progress
  // notifications are spaced at least this far apart. Starts, completions and
  // cancellations are never delayed.
  static constexpr base::TimeDelta kProgressInterval = base::Milliseconds(250);

  struct Summary {
    // Downloads still running, paused ones included.
    int in_progress_count = 0;
    // Sums over the running downloads. While any of them has an unknown size,
    // `total_bytes` is 0 and `percent` is empty.
    int64_t received_bytes = 0;
    int64_t total_bytes = 0;
    std::optional<int> percent;
    // The newest running download whose name is known; while
    // `just_completed`, the one that just finished. Empty when there is
    // nothing to name.
    std::u16string current_file_name;
    // True for kLingerDuration after the last running download completes.
    // Nothing is running then, so the counts and sums above are zero.
    bool just_completed = false;
  };

  class Observer : public base::CheckedObserver {
   public:
    virtual void OnOriginDownloadSummaryChanged() = 0;
  };

  // Follows `profile`'s own download manager only, so an incognito profile
  // reports its own downloads and not its parent's.
  explicit OriginDownloadSummary(Profile* profile);
  OriginDownloadSummary(const OriginDownloadSummary&) = delete;
  OriginDownloadSummary& operator=(const OriginDownloadSummary&) = delete;
  ~OriginDownloadSummary() override;

  Summary GetSummary() const;

  void AddObserver(Observer* observer);
  void RemoveObserver(Observer* observer);

 private:
  // download::AllDownloadItemNotifier::Observer:
  void OnManagerGoingDown(content::DownloadManager* manager) override;
  void OnDownloadCreated(content::DownloadManager* manager,
                         download::DownloadItem* item) override;
  void OnDownloadUpdated(content::DownloadManager* manager,
                         download::DownloadItem* item) override;
  void OnDownloadRemoved(content::DownloadManager* manager,
                         download::DownloadItem* item) override;
  void OnDownloadDestroyed(content::DownloadManager* manager,
                           download::DownloadItem* item) override;

  void OnItemChanged(download::DownloadItem* item);
  void ForgetItem(download::DownloadItem* item);
  void OnLingerEnded();
  void NotifyNow();
  void NotifyThrottled();

  // Running downloads. An item always leaves before it is destroyed.
  std::set<raw_ptr<download::DownloadItem, SetExperimental>> in_progress_;
  std::u16string lingering_name_;
  base::OneShotTimer linger_timer_;
  base::OneShotTimer throttle_timer_;
  base::TimeTicks last_notified_;
  base::ObserverList<Observer> observers_;
  // Last, so it stops reporting before anything above goes away.
  std::unique_ptr<download::AllDownloadItemNotifier> notifier_;
};

#endif  // BRAVE_BROWSER_UI_TABS_ORIGIN_DOWNLOAD_SUMMARY_H_
