// Copyright (c) 2026 The Brave Authors. All rights reserved.
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this file,
// You can obtain one at https://mozilla.org/MPL/2.0/.

#include "brave/browser/ui/views/frame/origin_later_picker_view.h"

#include <algorithm>
#include <memory>
#include <utility>

#include "base/check.h"
#include "base/functional/bind.h"
#include "base/i18n/case_conversion.h"
#include "base/strings/string_util.h"
#include "brave/browser/ui/views/frame/origin_chrome_style.h"
#include "cc/paint/paint_flags.h"
#include "ui/base/metadata/metadata_impl_macros.h"
#include "ui/compositor/layer.h"
#include "ui/events/event.h"
#include "ui/gfx/canvas.h"
#include "ui/gfx/color_utils.h"
#include "ui/views/background.h"
#include "ui/views/border.h"
#include "ui/views/controls/button/button.h"
#include "ui/views/controls/label.h"
#include "ui/views/controls/textfield/textfield.h"
#include "ui/views/layout/box_layout.h"

namespace {

constexpr int kPanelWidth = 420;
constexpr int kPanelCornerRadius = 14;
constexpr int kRowHeight = 36;
constexpr int kRowCornerRadius = 8;

std::unique_ptr<views::Textfield> MakeField(views::TextfieldController* owner,
                                            const std::u16string& placeholder) {
  auto field = std::make_unique<views::Textfield>();
  field->SetPlaceholderText(placeholder);
  field->SetAccessibleName(placeholder);
  field->SetFontList(origin_chrome_style::Font(14, gfx::Font::Weight::NORMAL));
  field->SetBackgroundEnabled(false);
  field->SetBorder(views::CreateEmptyBorder(gfx::Insets()));
  field->set_controller(owner);
  return field;
}

}  // namespace

// One preset: its name on the left, the time it resolves to on the right.
class OriginLaterPickerView::PresetRow : public views::Button {
  METADATA_HEADER(PresetRow, views::Button)

 public:
  PresetRow(PressedCallback callback,
            const std::u16string& label,
            const std::u16string& when)
      : views::Button(std::move(callback)) {
    SetPreferredSize(gfx::Size(0, kRowHeight));
    SetAccessibleName(label + u", " + when);
    auto* layout = SetLayoutManager(std::make_unique<views::BoxLayout>(
        views::BoxLayout::Orientation::kHorizontal, gfx::Insets::VH(0, 10), 8));
    layout->set_cross_axis_alignment(
        views::BoxLayout::CrossAxisAlignment::kCenter);
    label_ = AddChildView(std::make_unique<views::Label>(label));
    label_->SetFontList(
        origin_chrome_style::Font(13, gfx::Font::Weight::NORMAL));
    label_->SetHorizontalAlignment(gfx::ALIGN_LEFT);
    layout->SetFlexForView(label_, 1);
    when_ = AddChildView(std::make_unique<views::Label>(when));
    when_->SetFontList(
        origin_chrome_style::Font(12, gfx::Font::Weight::NORMAL));
  }

  void SetSelected(bool selected) {
    if (selected_ == selected) {
      return;
    }
    selected_ = selected;
    RefreshStyle();
  }

  void StateChanged(ButtonState old_state) override {
    views::Button::StateChanged(old_state);
    RefreshStyle();
  }

  void OnThemeChanged() override {
    views::Button::OnThemeChanged();
    RefreshStyle();
  }

 private:
  void RefreshStyle() {
    const origin_chrome_style::PickerColors colors =
        origin_chrome_style::GetPickerColors(origin_chrome_style::IsDark(this));
    const bool hovered =
        GetState() == STATE_HOVERED || GetState() == STATE_PRESSED;
    SetBackground(views::CreateRoundedRectBackground(
        selected_ ? colors.selected_row
                  : (hovered ? colors.hovered_row : SK_ColorTRANSPARENT),
        kRowCornerRadius));
    label_->SetEnabledColor(colors.primary_text);
    when_->SetEnabledColor(colors.secondary_text);
  }

  bool selected_ = false;
  raw_ptr<views::Label> label_ = nullptr;
  raw_ptr<views::Label> when_ = nullptr;
};

BEGIN_METADATA(OriginLaterPickerView, PresetRow)
END_METADATA

OriginLaterPickerView::OriginLaterPickerView(base::RepeatingClosure on_close)
    : on_close_(std::move(on_close)) {
  SetVisible(false);
  SetPaintToLayer();
  layer()->SetFillsBoundsOpaquely(false);

  panel_ = AddChildView(std::make_unique<views::View>());
  panel_->SetPaintToLayer();
  panel_->layer()->SetFillsBoundsOpaquely(false);
  panel_->layer()->SetRoundedCornerRadius(
      gfx::RoundedCornersF(kPanelCornerRadius));
  panel_->layer()->SetBackgroundBlur(origin_chrome_style::kPickerBlurSigma);
  panel_->layer()->SetBackdropFilterQuality(
      origin_chrome_style::kPickerBackdropQuality);
  panel_->SetLayoutManager(std::make_unique<views::BoxLayout>(
      views::BoxLayout::Orientation::kVertical,
      gfx::Insets::TLBR(16, 18, 12, 18), 6));

  heading_ = panel_->AddChildView(std::make_unique<views::Label>());
  heading_->SetFontList(
      origin_chrome_style::Font(15, gfx::Font::Weight::SEMIBOLD));
  heading_->SetHorizontalAlignment(gfx::ALIGN_LEFT);

  page_title_ = panel_->AddChildView(std::make_unique<views::Label>());
  page_title_->SetFontList(
      origin_chrome_style::Font(12, gfx::Font::Weight::NORMAL));
  page_title_->SetHorizontalAlignment(gfx::ALIGN_LEFT);
  page_title_->SetElideBehavior(gfx::ELIDE_TAIL);

  what_container_ = panel_->AddChildView(std::make_unique<views::View>());
  what_container_->SetLayoutManager(std::make_unique<views::BoxLayout>(
      views::BoxLayout::Orientation::kVertical, gfx::Insets::VH(8, 10)));
  what_field_ =
      what_container_->AddChildView(MakeField(this, u"What to be reminded of"));

  when_container_ = panel_->AddChildView(std::make_unique<views::View>());
  when_container_->SetLayoutManager(std::make_unique<views::BoxLayout>(
      views::BoxLayout::Orientation::kVertical, gfx::Insets::VH(8, 10)));
  when_field_ = when_container_->AddChildView(
      MakeField(this, u"When? Try in 2h, fri 3pm, tomorrow evening"));

  preview_ = panel_->AddChildView(std::make_unique<views::Label>());
  preview_->SetFontList(
      origin_chrome_style::Font(12, gfx::Font::Weight::NORMAL));
  preview_->SetHorizontalAlignment(gfx::ALIGN_LEFT);
  preview_->SetBorder(views::CreateEmptyBorder(gfx::Insets::VH(0, 2)));

  presets_container_ = panel_->AddChildView(std::make_unique<views::View>());
  presets_container_->SetLayoutManager(std::make_unique<views::BoxLayout>(
      views::BoxLayout::Orientation::kVertical));

  footer_ = panel_->AddChildView(std::make_unique<views::Label>(
      u"↑↓ choose   Enter confirm   Esc cancel"));
  footer_->SetFontList(
      origin_chrome_style::Font(11, gfx::Font::Weight::NORMAL));
  footer_->SetHorizontalAlignment(gfx::ALIGN_LEFT);
  footer_->SetBorder(views::CreateEmptyBorder(gfx::Insets::TLBR(4, 2, 0, 0)));
}

OriginLaterPickerView::~OriginLaterPickerView() = default;

void OriginLaterPickerView::Show(Kind kind,
                                 std::u16string page_title,
                                 std::optional<ui::KeyboardCode> activation_key,
                                 DoneCallback done) {
  kind_ = kind;
  done_ = std::move(done);
  heading_->SetText(kind == Kind::kSnooze ? u"Snooze until" : u"Remind me");
  page_title_->SetText(page_title);
  what_container_->SetVisible(kind == Kind::kReminder);
  what_field_->SetText(page_title);
  activation_key_to_suppress_ = activation_key;
  when_field_->SetText(std::u16string());
  RebuildPresets();
  UpdatePreview();
  SetVisible(true);
  InvalidateLayout();
  // A reminder starts on its time; the text is already filled in from the
  // page and is there to edit if wanted.
  when_field_->RequestFocus();
}

void OriginLaterPickerView::Dismiss() {
  if (!GetVisible()) {
    return;
  }
  done_.Reset();
  activation_key_to_suppress_.reset();
  SetVisible(false);
  on_close_.Run();
}

void OriginLaterPickerView::Layout(PassKey) {
  const gfx::Rect available = GetContentsBounds();
  const int panel_width =
      std::min(kPanelWidth, std::max(0, available.width() - 48));
  const int panel_height = std::min(panel_->GetPreferredSize().height(),
                                    std::max(0, available.height() - 24));
  // On the same shelf as Quick Open.
  const int free_height = std::max(0, available.height() - panel_height);
  const int panel_y = available.y() + std::clamp(free_height - 10, 12, 64);
  panel_->SetBounds(available.x() + (available.width() - panel_width) / 2,
                    panel_y, panel_width, panel_height);
}

void OriginLaterPickerView::OnThemeChanged() {
  views::View::OnThemeChanged();
  RefreshTheme();
}

bool OriginLaterPickerView::OnMousePressed(const ui::MouseEvent& event) {
  if (!panel_->bounds().Contains(event.location())) {
    Dismiss();
  }
  return true;
}

void OriginLaterPickerView::ContentsChanged(
    views::Textfield* sender,
    const std::u16string& new_contents) {
  if (sender != when_field_) {
    return;
  }
  // The key that opened the picker can arrive as a character after focus has
  // moved here. Drop just that character.
  if (activation_key_to_suppress_) {
    const char16_t lower = static_cast<char16_t>(
        u'a' + (*activation_key_to_suppress_ - ui::VKEY_A));
    const bool is_activation_character =
        new_contents.size() == 1 &&
        base::ToLowerASCII(new_contents[0]) == lower;
    activation_key_to_suppress_.reset();
    if (is_activation_character) {
      when_field_->SetText(std::u16string());
      UpdatePreview();
      return;
    }
  }
  UpdatePreview();
}

bool OriginLaterPickerView::HandleKeyEvent(views::Textfield* sender,
                                           const ui::KeyEvent& key_event) {
  if (key_event.type() != ui::EventType::kKeyPressed) {
    return false;
  }
  activation_key_to_suppress_.reset();
  switch (key_event.key_code()) {
    case ui::VKEY_ESCAPE:
      Dismiss();
      return true;
    case ui::VKEY_RETURN:
      if (sender == what_field_) {
        when_field_->RequestFocus();
        return true;
      }
      Submit();
      return true;
    case ui::VKEY_UP:
      if (sender == when_field_ && !presets_.empty()) {
        SelectPreset((selected_preset_ + presets_.size() - 1) %
                     presets_.size());
        return true;
      }
      return false;
    case ui::VKEY_DOWN:
      if (sender == when_field_ && !presets_.empty()) {
        SelectPreset((selected_preset_ + 1) % presets_.size());
        return true;
      }
      return false;
    case ui::VKEY_TAB:
      if (kind_ == Kind::kReminder) {
        (sender == what_field_ ? when_field_ : what_field_)->RequestFocus();
        return true;
      }
      return false;
    default:
      return false;
  }
}

base::Time OriginLaterPickerView::Now() const {
  return now_for_testing_.value_or(base::Time::Now());
}

void OriginLaterPickerView::RebuildPresets() {
  presets_ = origin_time_parser::GetPresets(Now());
  preset_rows_.clear();
  presets_container_->RemoveAllChildViews();
  for (size_t i = 0; i < presets_.size(); ++i) {
    auto* row = presets_container_->AddChildView(std::make_unique<PresetRow>(
        base::BindRepeating(
            [](OriginLaterPickerView* view, size_t index) {
              view->when_field_->SetText(std::u16string());
              view->SelectPreset(index);
              view->Submit();
            },
            base::Unretained(this), i),
        presets_[i].label, presets_[i].when.description));
    preset_rows_.push_back(row);
  }
  SelectPreset(0);
}

void OriginLaterPickerView::SelectPreset(size_t index) {
  selected_preset_ = index;
  for (size_t i = 0; i < preset_rows_.size(); ++i) {
    // A typed time takes over from the presets.
    preset_rows_[i]->SetSelected(i == index && when_field_->GetText().empty());
  }
}

void OriginLaterPickerView::UpdatePreview() {
  const origin_chrome_style::PickerColors colors =
      origin_chrome_style::GetPickerColors(origin_chrome_style::IsDark(this));
  const std::u16string_view text = when_field_->GetText();
  if (base::TrimWhitespace(text, base::TRIM_ALL).empty()) {
    preview_->SetVisible(false);
  } else if (auto parsed = origin_time_parser::Parse(text, Now())) {
    preview_->SetText(parsed->description);
    preview_->SetEnabledColor(origin_chrome_style::kAccent);
    preview_->SetVisible(true);
  } else {
    preview_->SetText(u"Try \"in 2 hours\", \"friday 3pm\" or \"oct 14\"");
    preview_->SetEnabledColor(colors.tertiary_text);
    preview_->SetVisible(true);
  }
  SelectPreset(selected_preset_);
  InvalidateLayout();
}

std::optional<origin_time_parser::ParsedTime>
OriginLaterPickerView::GetChosenTime() const {
  const std::u16string_view text = when_field_->GetText();
  if (!base::TrimWhitespace(text, base::TRIM_ALL).empty()) {
    return origin_time_parser::Parse(text, Now());
  }
  if (selected_preset_ < presets_.size()) {
    return presets_[selected_preset_].when;
  }
  return std::nullopt;
}

void OriginLaterPickerView::Submit() {
  const std::optional<origin_time_parser::ParsedTime> when = GetChosenTime();
  if (!when || !done_) {
    // Nothing usable typed yet; the preview already says what would work.
    return;
  }
  std::u16string text(base::TrimWhitespace(
      kind_ == Kind::kReminder ? what_field_->GetText()
                               : std::u16string_view(page_title_->GetText()),
      base::TRIM_ALL));
  DoneCallback done = std::move(done_);
  Dismiss();
  std::move(done).Run(std::move(text), *when);
}

void OriginLaterPickerView::RefreshTheme() {
  const origin_chrome_style::PickerColors colors =
      origin_chrome_style::GetPickerColors(origin_chrome_style::IsDark(this));
  SetBackground(views::CreateSolidBackground(colors.scrim));
  panel_->SetBackground(
      views::CreateRoundedRectBackground(colors.panel, kPanelCornerRadius));
  panel_->SetBorder(views::CreateRoundedRectBorder(1, kPanelCornerRadius,
                                                   colors.panel_stroke));
  heading_->SetEnabledColor(colors.primary_text);
  page_title_->SetEnabledColor(colors.secondary_text);
  for (views::View* container :
       {what_container_.get(), when_container_.get()}) {
    container->SetBackground(
        views::CreateRoundedRectBackground(colors.field, kRowCornerRadius));
  }
  what_field_->SetColor(colors.primary_text);
  when_field_->SetColor(colors.primary_text);
  footer_->SetEnabledColor(colors.tertiary_text);
  UpdatePreview();
}

BEGIN_METADATA(OriginLaterPickerView)
END_METADATA
