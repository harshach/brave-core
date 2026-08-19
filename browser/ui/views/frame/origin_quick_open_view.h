/* Copyright (c) 2026 The Brave Authors. All rights reserved.
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at https://mozilla.org/MPL/2.0/. */

#ifndef BRAVE_BROWSER_UI_VIEWS_FRAME_ORIGIN_QUICK_OPEN_VIEW_H_
#define BRAVE_BROWSER_UI_VIEWS_FRAME_ORIGIN_QUICK_OPEN_VIEW_H_

#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "base/functional/callback.h"
#include "base/memory/raw_ptr.h"
#include "components/omnibox/browser/autocomplete_controller.h"
#include "ui/base/metadata/metadata_header_macros.h"
#include "ui/events/keycodes/keyboard_codes.h"
#include "ui/views/controls/textfield/textfield_controller.h"
#include "ui/views/view.h"
#include "url/gurl.h"

class OriginQuickOpenResultButton;
class Profile;

namespace gfx {
struct VectorIcon;
}  // namespace gfx

namespace ui {
class KeyEvent;
class MouseEvent;
}  // namespace ui

namespace views {
class Label;
class Textfield;
}  // namespace views

enum class OriginQuickOpenDisposition {
  kNewPage,
  kReplace,
  kSplit,
};

// A resolved Quick Open choice. `destination_url` is populated by Chromium's
// native omnibox providers when a suggestion is selected. `switch_to_tab`
// distinguishes an existing-tab result from an otherwise identical history or
// URL result.
struct OriginQuickOpenSelection {
  std::u16string input;
  GURL destination_url;
  bool switch_to_tab = false;
};

// A window-scoped Quick Open surface for Brave Origin. It deliberately lives
// in browser chrome instead of the renderer so single-key navigation remains
// inactive while the user types and all opened pages retain normal Brave
// protections and tab semantics.
class OriginQuickOpenView : public views::View,
                            public views::TextfieldController,
                            public AutocompleteController::Observer {
  METADATA_HEADER(OriginQuickOpenView, views::View)

 public:
  using SubmitCallback =
      base::RepeatingCallback<void(OriginQuickOpenSelection,
                                   OriginQuickOpenDisposition)>;

  OriginQuickOpenView(Profile* profile,
                      SubmitCallback submit_callback,
                      base::RepeatingClosure close_callback);
  OriginQuickOpenView(const OriginQuickOpenView&) = delete;
  OriginQuickOpenView& operator=(const OriginQuickOpenView&) = delete;
  ~OriginQuickOpenView() override;

  // `activation_key` is consumed after focus moves into the search field. A
  // native single-key press can otherwise open Quick Open and then insert that
  // same character into the newly focused field. Modifier accelerators such as
  // Ctrl/Cmd+T pass no activation key.
  void ShowAndFocus(
      std::optional<ui::KeyboardCode> activation_key = std::nullopt);
  void Dismiss();

  // views::TextfieldController:
  void ContentsChanged(views::Textfield* sender,
                       const std::u16string& new_contents) override;
  bool HandleKeyEvent(views::Textfield* sender,
                      const ui::KeyEvent& key_event) override;

  // AutocompleteController::Observer:
  void OnResultChanged(AutocompleteController* controller,
                       bool default_match_changed) override;

  // views::View:
  void Layout(PassKey) override;
  bool OnMousePressed(const ui::MouseEvent& event) override;

 private:
  struct Result {
    std::u16string title;
    std::u16string subtitle;
    std::u16string badge;
    GURL destination_url;
    bool switch_to_tab = false;
    raw_ptr<const gfx::VectorIcon> icon = nullptr;
  };

  void Submit(OriginQuickOpenDisposition disposition);
  void OnResultPressed(size_t index);
  void StartAutocomplete(const std::u16string& input);
  void RebuildResults();
  void UpdateResultRows();
  void SelectResult(size_t index);

  SubmitCallback submit_callback_;
  base::RepeatingClosure close_callback_;
  std::unique_ptr<AutocompleteController> autocomplete_controller_;
  std::vector<Result> results_;
  size_t selected_result_ = 0;
  raw_ptr<views::View> panel_ = nullptr;
  raw_ptr<views::Textfield> search_field_ = nullptr;
  raw_ptr<views::Label> section_label_ = nullptr;
  raw_ptr<views::Label> empty_state_label_ = nullptr;
  std::vector<raw_ptr<OriginQuickOpenResultButton>> result_rows_;
  std::optional<ui::KeyboardCode> activation_key_to_suppress_;
};

#endif  // BRAVE_BROWSER_UI_VIEWS_FRAME_ORIGIN_QUICK_OPEN_VIEW_H_
