// Copyright (c) 2026 The Brave Authors. All rights reserved.
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this file,
// You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef BRAVE_BROWSER_UI_VIEWS_FRAME_ORIGIN_LATER_PICKER_VIEW_H_
#define BRAVE_BROWSER_UI_VIEWS_FRAME_ORIGIN_LATER_PICKER_VIEW_H_

#include <optional>
#include <string>
#include <vector>

#include "base/functional/callback.h"
#include "base/memory/raw_ptr.h"
#include "base/time/time.h"
#include "brave/browser/ui/tabs/origin_time_parser.h"
#include "ui/base/metadata/metadata_header_macros.h"
#include "ui/events/keycodes/keyboard_codes.h"
#include "ui/views/controls/textfield/textfield_controller.h"
#include "ui/views/view.h"

namespace views {
class Label;
class Textfield;
}  // namespace views

// Asks "when?" for snoozing a page (l) or a reminder (a), after SigmaOS's
// snooze and reminder fields. Times are typed in plain words or picked from a
// few presets; a reminder also takes what to be reminded of.
class OriginLaterPickerView : public views::View,
                              public views::TextfieldController {
  METADATA_HEADER(OriginLaterPickerView, views::View)

 public:
  enum class Kind { kSnooze, kReminder };
  using DoneCallback =
      base::OnceCallback<void(std::u16string text,
                              origin_time_parser::ParsedTime when)>;

  explicit OriginLaterPickerView(base::RepeatingClosure on_close);
  OriginLaterPickerView(const OriginLaterPickerView&) = delete;
  OriginLaterPickerView& operator=(const OriginLaterPickerView&) = delete;
  ~OriginLaterPickerView() override;

  // `activation_key` is the key that opened the picker; its character is kept
  // out of the field if it arrives after focus has moved there.
  void Show(Kind kind,
            std::u16string page_title,
            std::optional<ui::KeyboardCode> activation_key,
            DoneCallback done);
  void Dismiss();

  // views::View:
  void Layout(PassKey) override;
  void OnThemeChanged() override;
  bool OnMousePressed(const ui::MouseEvent& event) override;

  // views::TextfieldController:
  void ContentsChanged(views::Textfield* sender,
                       const std::u16string& new_contents) override;
  bool HandleKeyEvent(views::Textfield* sender,
                      const ui::KeyEvent& key_event) override;

  views::Textfield* when_field_for_testing() { return when_field_; }
  views::Textfield* what_field_for_testing() { return what_field_; }
  void SetNowForTesting(base::Time now) { now_for_testing_ = now; }
  // Confirms as Enter does.
  void SubmitForTesting() { Submit(); }

 private:
  class PresetRow;

  base::Time Now() const;
  void RebuildPresets();
  void SelectPreset(size_t index);
  void UpdatePreview();
  std::optional<origin_time_parser::ParsedTime> GetChosenTime() const;
  void Submit();
  void RefreshTheme();

  base::RepeatingClosure on_close_;
  DoneCallback done_;
  Kind kind_ = Kind::kSnooze;
  std::optional<ui::KeyboardCode> activation_key_to_suppress_;
  std::optional<base::Time> now_for_testing_;

  raw_ptr<views::View> panel_ = nullptr;
  raw_ptr<views::Label> heading_ = nullptr;
  raw_ptr<views::Label> page_title_ = nullptr;
  raw_ptr<views::View> what_container_ = nullptr;
  raw_ptr<views::Textfield> what_field_ = nullptr;
  raw_ptr<views::View> when_container_ = nullptr;
  raw_ptr<views::Textfield> when_field_ = nullptr;
  raw_ptr<views::Label> preview_ = nullptr;
  raw_ptr<views::View> presets_container_ = nullptr;
  raw_ptr<views::Label> footer_ = nullptr;
  std::vector<origin_time_parser::Preset> presets_;
  std::vector<raw_ptr<PresetRow>> preset_rows_;
  size_t selected_preset_ = 0;
};

#endif  // BRAVE_BROWSER_UI_VIEWS_FRAME_ORIGIN_LATER_PICKER_VIEW_H_
