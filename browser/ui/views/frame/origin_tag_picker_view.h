// Copyright (c) 2026 The Brave Authors. All rights reserved.
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this file,
// You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef BRAVE_BROWSER_UI_VIEWS_FRAME_ORIGIN_TAG_PICKER_VIEW_H_
#define BRAVE_BROWSER_UI_VIEWS_FRAME_ORIGIN_TAG_PICKER_VIEW_H_

#include <optional>
#include <string>
#include <vector>

#include "base/functional/callback.h"
#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "ui/base/metadata/metadata_header_macros.h"
#include "ui/events/keycodes/keyboard_codes.h"
#include "ui/views/controls/textfield/textfield_controller.h"
#include "ui/views/view.h"
#include "url/gurl.h"

namespace bookmarks {
class BookmarkModel;
}  // namespace bookmarks

namespace views {
class Label;
class ScrollView;
class Textfield;
}  // namespace views

// Tags the current page, after SigmaOS: `t` opens a small card over the page
// listing every tag, checked where the page has it. Typing filters the list
// and can name a new tag. Each choice applies at once and the card stays
// open, so a page can take several tags in one go.
class OriginTagPickerView : public views::View,
                            public views::TextfieldController {
  METADATA_HEADER(OriginTagPickerView, views::View)

 public:
  // `on_close` runs each time the card hides.
  OriginTagPickerView(bookmarks::BookmarkModel* model,
                      base::RepeatingClosure on_close);
  OriginTagPickerView(const OriginTagPickerView&) = delete;
  OriginTagPickerView& operator=(const OriginTagPickerView&) = delete;
  ~OriginTagPickerView() override;

  // Shows the card for the page at `url` and focuses its field. The owner
  // sizes this view to the page area, as it does Quick Open. `activation_key`
  // is the key that opened the picker; its character is kept out of the field
  // if it arrives after focus has moved there.
  void Show(const GURL& url,
            const std::u16string& title,
            std::optional<ui::KeyboardCode> activation_key);
  // Hides the card and runs `on_close`. Does nothing while hidden.
  void Dismiss();

  size_t GetRowCountForTesting() const { return rows_.size(); }
  std::optional<size_t> GetHighlightedRowForTesting() const;
  views::Textfield* text_field_for_testing() { return text_field_; }
  bool IsSuppressingActivationKeyForTesting() const {
    return activation_key_to_suppress_.has_value();
  }
  // Tags or untags the page as choosing `tag` does, creating the tag if
  // needed, without waiting for the task a click posts.
  void ToggleTagForTesting(const std::u16string& tag);

  // views::View:
  void Layout(PassKey) override;
  void OnThemeChanged() override;
  bool OnMousePressed(const ui::MouseEvent& event) override;

  // views::TextfieldController:
  void ContentsChanged(views::Textfield* sender,
                       const std::u16string& new_contents) override;
  bool HandleKeyEvent(views::Textfield* sender,
                      const ui::KeyEvent& key_event) override;

 private:
  class Row;

  struct Choice {
    std::u16string name;
    size_t page_count = 0;
    bool checked = false;
    // The "Create tag" row, for a typed name that isn't a tag yet.
    bool create = false;
  };

  // A row calls this from inside its own event handling, and toggling
  // rebuilds the rows, so the toggle waits for a posted task.
  void OnRowPressed(const std::u16string& name);
  void ToggleTag(const std::u16string& name);
  void Rebuild();
  void Highlight(size_t index);
  void RefreshTheme();

  const base::RepeatingClosure on_close_;
  GURL url_;
  std::u16string title_;
  std::vector<Choice> choices_;
  size_t highlighted_ = 0;
  // The folded name of the tag last toggled, which keeps the highlight
  // through the rebuild that follows.
  std::u16string highlighted_key_;
  std::optional<ui::KeyboardCode> activation_key_to_suppress_;

  raw_ptr<bookmarks::BookmarkModel> model_;
  raw_ptr<views::View> panel_ = nullptr;
  raw_ptr<views::Label> heading_ = nullptr;
  raw_ptr<views::Label> page_title_ = nullptr;
  raw_ptr<views::View> field_container_ = nullptr;
  raw_ptr<views::Textfield> text_field_ = nullptr;
  raw_ptr<views::ScrollView> scroll_ = nullptr;
  raw_ptr<views::View> list_ = nullptr;
  raw_ptr<views::Label> hint_ = nullptr;
  raw_ptr<views::Label> footer_ = nullptr;
  std::vector<raw_ptr<Row>> rows_;
  base::WeakPtrFactory<OriginTagPickerView> weak_factory_{this};
};

#endif  // BRAVE_BROWSER_UI_VIEWS_FRAME_ORIGIN_TAG_PICKER_VIEW_H_
