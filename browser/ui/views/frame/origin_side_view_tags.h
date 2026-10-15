// Copyright (c) 2026 The Brave Authors. All rights reserved.
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this file,
// You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef BRAVE_BROWSER_UI_VIEWS_FRAME_ORIGIN_SIDE_VIEW_TAGS_H_
#define BRAVE_BROWSER_UI_VIEWS_FRAME_ORIGIN_SIDE_VIEW_TAGS_H_

#include <string>
#include <vector>

#include "base/containers/flat_set.h"
#include "base/functional/callback.h"
#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "base/scoped_observation.h"
#include "components/bookmarks/browser/base_bookmark_model_observer.h"
#include "ui/base/metadata/metadata_header_macros.h"
#include "ui/views/controls/textfield/textfield_controller.h"
#include "ui/views/view.h"

class GURL;

namespace bookmarks {
class BookmarkModel;
}  // namespace bookmarks

namespace views {
class BoxLayoutView;
class ImageView;
class Label;
class ScrollView;
class Textfield;
}  // namespace views

// The side view's Tags tool: every tag, each opening onto its pages, with one
// search over both. Tags are bookmarks (see origin_tags.h), so the list
// follows the bookmark model whoever changes it, sync included.
class OriginSideViewTags : public views::View,
                           public views::TextfieldController,
                           public bookmarks::BaseBookmarkModelObserver {
  METADATA_HEADER(OriginSideViewTags, views::View)

 public:
  // `open_url` runs when a page is clicked. Nothing is listed until `model`
  // has loaded.
  OriginSideViewTags(bookmarks::BookmarkModel* model,
                     base::RepeatingCallback<void(const GURL&)> open_url);
  OriginSideViewTags(const OriginSideViewTags&) = delete;
  OriginSideViewTags& operator=(const OriginSideViewTags&) = delete;
  ~OriginSideViewTags() override;

  void FocusSearch();

  size_t GetTagRowCountForTesting() const { return tag_rows_.size(); }
  size_t GetPageRowCountForTesting() const { return page_rows_.size(); }
  views::View* empty_state_for_testing() { return empty_state_; }
  views::Textfield* search_field_for_testing() { return search_; }
  // Opens or closes `tag` as a click does, without waiting for the rebuild a
  // click posts.
  void ToggleTagForTesting(const std::u16string& tag);

  // views::View:
  void OnThemeChanged() override;

  // views::TextfieldController:
  void ContentsChanged(views::Textfield* sender,
                       const std::u16string& new_contents) override;
  bool HandleKeyEvent(views::Textfield* sender,
                      const ui::KeyEvent& key_event) override;

 private:
  class TagRow;
  class PageRow;

  // bookmarks::BaseBookmarkModelObserver:
  void BookmarkModelChanged() override;
  void BookmarkModelBeingDeleted() override;
  void ExtensiveBookmarkChangesEnded() override;

  // Rows call these from inside their own event handling, so they never
  // touch the rows or the model directly: both wait for a posted task.
  // `opened_by_search` is whether the row is open only because the search
  // matched some of its pages.
  void OnTagPressed(const std::u16string& key, bool opened_by_search);
  void OnDeleteTagPressed(const std::u16string& tag);
  void OnPagePressed(const GURL& url);
  void OnRemovePagePressed(const std::u16string& tag, const GURL& url);

  void ToggleTag(const std::u16string& key, bool opened_by_search);
  void DeleteTag(const std::u16string& tag);
  void RemovePage(const std::u16string& tag, const GURL& url);
  void OpenUrl(const GURL& url);

  void ScheduleRebuild();
  void Rebuild();
  void UpdateColors();

  const base::RepeatingCallback<void(const GURL&)> open_url_;
  std::u16string query_;
  // Tags the user opened, by folded name; kept across rebuilds.
  base::flat_set<std::u16string> expanded_;
  // Tags a search opened that the user closed again; forgotten when the
  // search changes.
  base::flat_set<std::u16string> collapsed_in_search_;
  bool rebuild_scheduled_ = false;

  raw_ptr<bookmarks::BookmarkModel> model_;
  raw_ptr<views::View> search_row_ = nullptr;
  raw_ptr<views::View> search_field_ = nullptr;
  raw_ptr<views::Textfield> search_ = nullptr;
  raw_ptr<views::ScrollView> scroll_ = nullptr;
  raw_ptr<views::BoxLayoutView> list_ = nullptr;
  raw_ptr<views::Label> no_match_ = nullptr;
  std::vector<raw_ptr<TagRow>> tag_rows_;
  std::vector<raw_ptr<PageRow>> page_rows_;
  // "No pages" under an open tag that has none.
  std::vector<raw_ptr<views::Label>> no_pages_labels_;

  raw_ptr<views::View> empty_state_ = nullptr;
  raw_ptr<views::ImageView> empty_icon_ = nullptr;
  raw_ptr<views::Label> empty_title_ = nullptr;
  raw_ptr<views::Label> empty_body_ = nullptr;

  base::ScopedObservation<bookmarks::BookmarkModel,
                          bookmarks::BookmarkModelObserver>
      model_observation_{this};
  base::WeakPtrFactory<OriginSideViewTags> weak_factory_{this};
};

#endif  // BRAVE_BROWSER_UI_VIEWS_FRAME_ORIGIN_SIDE_VIEW_TAGS_H_
