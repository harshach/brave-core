// Copyright (c) 2026 The Brave Authors. All rights reserved.
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this file,
// You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef BRAVE_BROWSER_UI_VIEWS_FRAME_ORIGIN_SIDE_VIEW_SHORTCUTS_H_
#define BRAVE_BROWSER_UI_VIEWS_FRAME_ORIGIN_SIDE_VIEW_SHORTCUTS_H_

#include <string>
#include <vector>

#include "base/memory/raw_ptr.h"
#include "ui/base/metadata/metadata_header_macros.h"
#include "ui/views/controls/textfield/textfield_controller.h"
#include "ui/views/view.h"

namespace views {
class Label;
class ScrollView;
class Textfield;
}  // namespace views

// The side view's cheat sheet: every navigation-mode key, searchable. After
// SigmaOS's cheat sheet, which lives in the same panel.
class OriginSideViewShortcuts : public views::View,
                                public views::TextfieldController {
  METADATA_HEADER(OriginSideViewShortcuts, views::View)

 public:
  struct Shortcut {
    std::vector<std::u16string> keys;
    std::u16string description;
  };
  struct Section {
    std::u16string title;
    std::vector<Shortcut> shortcuts;
  };
  static std::vector<Section> GetSections();

  OriginSideViewShortcuts();
  OriginSideViewShortcuts(const OriginSideViewShortcuts&) = delete;
  OriginSideViewShortcuts& operator=(const OriginSideViewShortcuts&) = delete;
  ~OriginSideViewShortcuts() override;

  void FocusSearch();
  // Rows left after filtering, for tests.
  size_t GetVisibleRowCountForTesting() const;

  // views::View:
  void OnThemeChanged() override;

  // views::TextfieldController:
  void ContentsChanged(views::Textfield* sender,
                       const std::u16string& new_contents) override;
  bool HandleKeyEvent(views::Textfield* sender,
                      const ui::KeyEvent& key_event) override;

 private:
  struct Row {
    raw_ptr<views::View> view;
    std::u16string haystack;
    size_t section;
  };

  void ApplyFilter(const std::u16string& query);
  void UpdateColors();

  raw_ptr<views::View> search_field_ = nullptr;
  raw_ptr<views::Textfield> search_ = nullptr;
  raw_ptr<views::ScrollView> scroll_ = nullptr;
  std::vector<raw_ptr<views::Label>> section_titles_;
  std::vector<Row> rows_;
  raw_ptr<views::Label> no_match_ = nullptr;
};

#endif  // BRAVE_BROWSER_UI_VIEWS_FRAME_ORIGIN_SIDE_VIEW_SHORTCUTS_H_
