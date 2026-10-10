// Copyright (c) 2026 The Brave Authors. All rights reserved.
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this file,
// You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef BRAVE_BROWSER_UI_VIEWS_FRAME_ORIGIN_SIDE_VIEW_DOWNLOADS_H_
#define BRAVE_BROWSER_UI_VIEWS_FRAME_ORIGIN_SIDE_VIEW_DOWNLOADS_H_

#include <memory>
#include <vector>

#include "base/functional/callback.h"
#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "base/timer/timer.h"
#include "components/download/content/public/all_download_item_notifier.h"
#include "ui/base/metadata/metadata_header_macros.h"
#include "ui/views/view.h"

class Profile;

namespace views {
class BoxLayoutView;
class ImageView;
class Label;
class LabelButton;
class ScrollView;
}  // namespace views

// The side view's Downloads tool: the profile's recent downloads, newest first,
// each with the one or two actions it needs. Everything else stays on
// chrome://downloads, which the footer opens.
class OriginSideViewDownloads
    : public views::View,
      public download::AllDownloadItemNotifier::Observer {
  METADATA_HEADER(OriginSideViewDownloads, views::View)

 public:
  // Older downloads are for chrome://downloads.
  static constexpr size_t kMaxRows = 50;

  // `open_all_downloads` runs from the footer, and from a row whose download
  // is waiting on a safety decision, which only chrome://downloads offers.
  OriginSideViewDownloads(Profile* profile,
                          base::RepeatingClosure open_all_downloads);
  OriginSideViewDownloads(const OriginSideViewDownloads&) = delete;
  OriginSideViewDownloads& operator=(const OriginSideViewDownloads&) = delete;
  ~OriginSideViewDownloads() override;

  size_t GetRowCountForTesting() const { return rows_.size(); }
  views::View* empty_state_for_testing() { return empty_state_; }

  // views::View:
  void OnThemeChanged() override;
  void AddedToWidget() override;
  void RemovedFromWidget() override;
  void VisibilityChanged(views::View* starting_from, bool is_visible) override;

 private:
  class Row;

  // download::AllDownloadItemNotifier::Observer:
  void OnManagerInitialized(content::DownloadManager* manager) override;
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
  Row* FindRow(const download::DownloadItem* item) const;
  // Whether `item` would make the list if it were rebuilt now.
  bool FitsInList(const download::DownloadItem* item) const;

  // Download events can arrive from inside a row's own button handler, so
  // they only mark what changed; the rows are touched from a posted task.
  void ScheduleUpdate();
  void ApplyUpdates();
  void Rebuild();
  void RefreshRows();
  void UpdateRefreshTimer();
  void UpdateColors();

  const base::RepeatingClosure open_all_downloads_;

  raw_ptr<views::ScrollView> scroll_ = nullptr;
  raw_ptr<views::BoxLayoutView> list_ = nullptr;
  std::vector<raw_ptr<Row>> rows_;

  raw_ptr<views::View> empty_state_ = nullptr;
  raw_ptr<views::ImageView> empty_icon_ = nullptr;
  raw_ptr<views::Label> empty_title_ = nullptr;
  raw_ptr<views::Label> empty_body_ = nullptr;

  raw_ptr<views::View> footer_ = nullptr;
  raw_ptr<views::LabelButton> open_all_button_ = nullptr;

  bool needs_rebuild_ = false;
  bool update_scheduled_ = false;
  // Runs exactly while the list is on screen, keeping relative times
  // ("3 minutes ago") honest.
  base::RepeatingTimer refresh_timer_;

  std::unique_ptr<download::AllDownloadItemNotifier> notifier_;
  base::WeakPtrFactory<OriginSideViewDownloads> weak_factory_{this};
};

#endif  // BRAVE_BROWSER_UI_VIEWS_FRAME_ORIGIN_SIDE_VIEW_DOWNLOADS_H_
