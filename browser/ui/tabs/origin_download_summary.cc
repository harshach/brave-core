// Copyright (c) 2026 The Brave Authors. All rights reserved.
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this file,
// You can obtain one at https://mozilla.org/MPL/2.0/.

#include "brave/browser/ui/tabs/origin_download_summary.h"

#include <algorithm>

#include "base/check.h"
#include "base/functional/bind.h"
#include "base/location.h"
#include "chrome/browser/profiles/profile.h"
#include "components/download/public/common/download_item.h"
#include "content/public/browser/download_manager.h"

namespace {

bool IsRunning(const download::DownloadItem* item) {
  return !item->IsTransient() &&
         item->GetState() == download::DownloadItem::IN_PROGRESS;
}

}  // namespace

OriginDownloadSummary::OriginDownloadSummary(Profile* profile) {
  CHECK(profile);
  content::DownloadManager* manager = profile->GetDownloadManager();
  // The notifier only reports changes, so pick up what is already running.
  content::DownloadManager::DownloadVector items;
  manager->GetAllDownloads(&items);
  for (download::DownloadItem* item : items) {
    if (IsRunning(item)) {
      in_progress_.insert(item);
    }
  }
  notifier_ =
      std::make_unique<download::AllDownloadItemNotifier>(manager, this);
}

OriginDownloadSummary::~OriginDownloadSummary() = default;

OriginDownloadSummary::Summary OriginDownloadSummary::GetSummary() const {
  Summary summary;
  if (in_progress_.empty()) {
    if (linger_timer_.IsRunning()) {
      summary.just_completed = true;
      summary.current_file_name = lingering_name_;
    }
    return summary;
  }

  bool sizes_known = true;
  const download::DownloadItem* named = nullptr;
  for (const download::DownloadItem* item : in_progress_) {
    ++summary.in_progress_count;
    summary.received_bytes += item->GetReceivedBytes();
    const int64_t total = item->GetTotalBytes();
    if (total > 0) {
      summary.total_bytes += total;
    } else {
      sizes_known = false;
    }
    // A download is named a moment after it starts; until then the strip
    // keeps naming the one before rather than going blank.
    if (!item->GetFileNameToReportUser().empty() &&
        (!named || item->GetStartTime() > named->GetStartTime())) {
      named = item;
    }
  }
  if (named) {
    summary.current_file_name =
        named->GetFileNameToReportUser().LossyDisplayName();
  }
  if (!sizes_known) {
    summary.total_bytes = 0;
  } else if (summary.total_bytes > 0) {
    // Servers can send more than they announced.
    summary.percent = static_cast<int>(std::clamp<int64_t>(
        summary.received_bytes * 100 / summary.total_bytes, 0, 100));
  }
  return summary;
}

void OriginDownloadSummary::AddObserver(Observer* observer) {
  observers_.AddObserver(observer);
}

void OriginDownloadSummary::RemoveObserver(Observer* observer) {
  observers_.RemoveObserver(observer);
}

void OriginDownloadSummary::OnManagerGoingDown(
    content::DownloadManager* manager) {
  // Whatever is still running is about to be cancelled and destroyed.
  const bool had_anything = !in_progress_.empty() || linger_timer_.IsRunning();
  in_progress_.clear();
  linger_timer_.Stop();
  lingering_name_.clear();
  if (had_anything) {
    NotifyNow();
  }
}

void OriginDownloadSummary::OnDownloadCreated(content::DownloadManager* manager,
                                              download::DownloadItem* item) {
  OnItemChanged(item);
}

void OriginDownloadSummary::OnDownloadUpdated(content::DownloadManager* manager,
                                              download::DownloadItem* item) {
  OnItemChanged(item);
}

void OriginDownloadSummary::OnDownloadRemoved(content::DownloadManager* manager,
                                              download::DownloadItem* item) {
  ForgetItem(item);
}

void OriginDownloadSummary::OnDownloadDestroyed(
    content::DownloadManager* manager,
    download::DownloadItem* item) {
  ForgetItem(item);
}

void OriginDownloadSummary::OnItemChanged(download::DownloadItem* item) {
  // Once the manager is going down, the cancellations that follow are not
  // news to anyone.
  if (!notifier_->GetManager()) {
    return;
  }
  const bool was_running = in_progress_.contains(item);
  const bool running = IsRunning(item);
  if (running && !was_running) {
    // A new or resumed download replaces any "Downloaded" message.
    in_progress_.insert(item);
    linger_timer_.Stop();
    lingering_name_.clear();
    NotifyNow();
  } else if (!running && was_running) {
    in_progress_.erase(item);
    // Only a finished file earns the message; a cancelled or failed one just
    // clears the strip.
    if (in_progress_.empty() &&
        item->GetState() == download::DownloadItem::COMPLETE) {
      lingering_name_ = item->GetFileNameToReportUser().LossyDisplayName();
      linger_timer_.Start(FROM_HERE, kLingerDuration,
                          base::BindOnce(&OriginDownloadSummary::OnLingerEnded,
                                         base::Unretained(this)));
    }
    NotifyNow();
  } else if (running) {
    NotifyThrottled();
  }
}

void OriginDownloadSummary::ForgetItem(download::DownloadItem* item) {
  if (in_progress_.erase(item)) {
    NotifyNow();
  }
}

void OriginDownloadSummary::OnLingerEnded() {
  lingering_name_.clear();
  NotifyNow();
}

void OriginDownloadSummary::NotifyNow() {
  throttle_timer_.Stop();
  last_notified_ = base::TimeTicks::Now();
  observers_.Notify(&Observer::OnOriginDownloadSummaryChanged);
}

void OriginDownloadSummary::NotifyThrottled() {
  if (throttle_timer_.IsRunning()) {
    // Already due, and it will read the latest numbers.
    return;
  }
  const base::TimeDelta since_last = base::TimeTicks::Now() - last_notified_;
  if (since_last >= kProgressInterval) {
    NotifyNow();
    return;
  }
  throttle_timer_.Start(FROM_HERE, kProgressInterval - since_last,
                        base::BindOnce(&OriginDownloadSummary::NotifyNow,
                                       base::Unretained(this)));
}
