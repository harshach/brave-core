/* Copyright (c) 2026 The Brave Authors. All rights reserved.
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at https://mozilla.org/MPL/2.0/. */

#ifndef BRAVE_BROWSER_UI_VIEWS_FRAME_ORIGIN_QUICK_OPEN_VIEW_H_
#define BRAVE_BROWSER_UI_VIEWS_FRAME_ORIGIN_QUICK_OPEN_VIEW_H_

#include <string>

#include "base/functional/callback.h"
#include "base/memory/raw_ptr.h"
#include "ui/base/metadata/metadata_header_macros.h"
#include "ui/views/controls/textfield/textfield_controller.h"
#include "ui/views/view.h"

namespace ui {
class KeyEvent;
class MouseEvent;
}  // namespace ui

namespace views {
class LabelButton;
class Textfield;
}  // namespace views

enum class OriginQuickOpenDisposition {
  kNewPage,
  kReplace,
  kSplit,
};

// A window-scoped Quick Open surface for Brave Origin. It deliberately lives
// in browser chrome instead of the renderer so single-key navigation remains
// inactive while the user types and all opened pages retain normal Brave
// protections and tab semantics.
class OriginQuickOpenView : public views::View,
                            public views::TextfieldController {
  METADATA_HEADER(OriginQuickOpenView, views::View)

 public:
  using SubmitCallback =
      base::RepeatingCallback<void(std::u16string, OriginQuickOpenDisposition)>;

  OriginQuickOpenView(SubmitCallback submit_callback,
                      base::RepeatingClosure close_callback);
  OriginQuickOpenView(const OriginQuickOpenView&) = delete;
  OriginQuickOpenView& operator=(const OriginQuickOpenView&) = delete;
  ~OriginQuickOpenView() override;

  void ShowAndFocus();

  // views::TextfieldController:
  void ContentsChanged(views::Textfield* sender,
                       const std::u16string& new_contents) override;
  bool HandleKeyEvent(views::Textfield* sender,
                      const ui::KeyEvent& key_event) override;

  // views::View:
  void Layout(PassKey) override;
  bool OnMousePressed(const ui::MouseEvent& event) override;

 private:
  void Submit(OriginQuickOpenDisposition disposition);
  void UpdatePrimaryResult(const std::u16string& input);

  SubmitCallback submit_callback_;
  base::RepeatingClosure close_callback_;
  raw_ptr<views::View> panel_ = nullptr;
  raw_ptr<views::Textfield> search_field_ = nullptr;
  raw_ptr<views::LabelButton> primary_result_ = nullptr;
};

#endif  // BRAVE_BROWSER_UI_VIEWS_FRAME_ORIGIN_QUICK_OPEN_VIEW_H_
