// Copyright (c) 2026 The Brave Authors. All rights reserved.
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this file,
// You can obtain one at https://mozilla.org/MPL/2.0/.

#include "brave/browser/ui/views/frame/origin_tag_picker_view.h"

#include <algorithm>
#include <memory>
#include <utility>

#include "base/containers/extend.h"
#include "base/functional/bind.h"
#include "base/i18n/case_conversion.h"
#include "base/location.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/string_util.h"
#include "base/strings/utf_string_conversions.h"
#include "base/task/sequenced_task_runner.h"
#include "brave/browser/ui/tabs/origin_tags.h"
#include "brave/browser/ui/views/frame/origin_chrome_style.h"
#include "brave/components/vector_icons/vector_icons.h"
#include "ui/base/metadata/metadata_impl_macros.h"
#include "ui/base/models/image_model.h"
#include "ui/compositor/layer.h"
#include "ui/events/event.h"
#include "ui/gfx/geometry/insets.h"
#include "ui/gfx/geometry/rect.h"
#include "ui/gfx/geometry/rounded_corners_f.h"
#include "ui/gfx/geometry/size.h"
#include "ui/views/background.h"
#include "ui/views/border.h"
#include "ui/views/controls/button/button.h"
#include "ui/views/controls/image_view.h"
#include "ui/views/controls/label.h"
#include "ui/views/controls/scroll_view.h"
#include "ui/views/controls/textfield/textfield.h"
#include "ui/views/layout/box_layout.h"
#include "ui/views/layout/box_layout_view.h"

namespace {

constexpr int kPanelWidth = 380;
constexpr int kPanelCornerRadius = 14;
constexpr int kRowHeight = 36;
constexpr int kRowCornerRadius = 8;
constexpr int kIconSize = 16;
// Rows past this many scroll.
constexpr int kMaxVisibleRows = 7;
constexpr float kPanelBlurSigma = 32.0f;
constexpr float kPanelBackdropQuality = 0.40f;

// The glass card Quick Open and the snooze picker use, so all three read as
// one family.
struct PickerColors {
  SkColor scrim;
  SkColor panel;
  SkColor panel_stroke;
  SkColor field;
  SkColor selected_row;
  SkColor hovered_row;
  SkColor primary_text;
  SkColor secondary_text;
  SkColor tertiary_text;
};

PickerColors GetPickerColors(bool dark) {
  if (dark) {
    return {
        .scrim = SkColorSetARGB(0x80, 0x0A, 0x0D, 0x12),
        .panel = SkColorSetARGB(0xEB, 0x1C, 0x1F, 0x25),
        .panel_stroke = SkColorSetARGB(0x12, 0xFF, 0xFF, 0xFF),
        .field = SkColorSetARGB(0x0F, 0xFF, 0xFF, 0xFF),
        .selected_row = SkColorSetARGB(0x1F, 0xFF, 0xFF, 0xFF),
        .hovered_row = SkColorSetARGB(0x0F, 0xFF, 0xFF, 0xFF),
        .primary_text = origin_chrome_style::PrimaryText(dark),
        .secondary_text = origin_chrome_style::SecondaryText(dark),
        .tertiary_text = origin_chrome_style::MutedText(dark),
    };
  }
  return {
      .scrim = SkColorSetARGB(0x5C, 0x18, 0x1D, 0x27),
      .panel = SkColorSetARGB(0xF7, 0xFF, 0xFF, 0xFF),
      .panel_stroke = SkColorSetARGB(0x1F, 0x18, 0x1D, 0x27),
      .field = SkColorSetARGB(0x0A, 0x18, 0x1D, 0x27),
      .selected_row = SkColorSetARGB(0x14, 0x18, 0x1D, 0x27),
      .hovered_row = SkColorSetARGB(0x0A, 0x18, 0x1D, 0x27),
      .primary_text = origin_chrome_style::PrimaryText(dark),
      .secondary_text = origin_chrome_style::SecondaryText(dark),
      .tertiary_text = origin_chrome_style::MutedText(dark),
  };
}

std::unique_ptr<views::Label> MakeLabel(std::u16string text,
                                        int size,
                                        gfx::Font::Weight weight) {
  auto label = std::make_unique<views::Label>(std::move(text));
  label->SetFontList(origin_chrome_style::Font(size, weight));
  label->SetHorizontalAlignment(gfx::ALIGN_LEFT);
  // The colors are picked for the card, not for the dialog background a
  // label otherwise assumes it sits on.
  label->SetAutoColorReadabilityEnabled(false);
  return label;
}

std::u16string Fold(std::u16string_view text) {
  return base::i18n::FoldCase(text);
}

// Whether `text` is just the character `key` types, in either case.
bool IsActivationCharacter(ui::KeyboardCode key, std::u16string_view text) {
  if (key < ui::VKEY_A || key > ui::VKEY_Z || text.size() != 1) {
    return false;
  }
  return base::ToLowerASCII(text[0]) == u'a' + (key - ui::VKEY_A);
}

}  // namespace

// One tag, or the row that creates one. The field keeps focus throughout;
// rows show the keyboard highlight, as Quick Open's do.
class OriginTagPickerView::Row : public views::Button {
  METADATA_HEADER(Row, views::Button)

 public:
  Row(PressedCallback callback, const Choice& choice)
      : views::Button(std::move(callback)),
        create_(choice.create),
        checked_(choice.checked) {
    SetFocusBehavior(FocusBehavior::NEVER);
    SetRequestFocusOnPress(false);
    SetPreferredSize(gfx::Size(0, kRowHeight));

    const std::u16string title =
        create_ ? u"Create tag “" + choice.name + u"”" : choice.name;
    SetAccessibleName(checked_ ? title + u", tagged" : title);

    auto* layout = SetLayoutManager(std::make_unique<views::BoxLayout>(
        views::BoxLayout::Orientation::kHorizontal, gfx::Insets::VH(0, 10), 8));
    layout->set_cross_axis_alignment(
        views::BoxLayout::CrossAxisAlignment::kCenter);

    icon_ = AddChildView(std::make_unique<views::ImageView>());
    icon_->SetImageSize(gfx::Size(kIconSize, kIconSize));

    label_ = AddChildView(MakeLabel(title, 13, gfx::Font::Weight::NORMAL));
    label_->SetElideBehavior(gfx::ELIDE_TAIL);
    layout->SetFlexForView(label_, 1);

    count_ = AddChildView(MakeLabel(base::NumberToString16(choice.page_count),
                                    12, gfx::Font::Weight::NORMAL));
    count_->SetVisible(!create_);

    check_ = AddChildView(std::make_unique<views::ImageView>());
    check_->SetImageSize(gfx::Size(kIconSize, kIconSize));
    check_->SetVisible(checked_);

    RefreshStyle();
  }

  void SetHighlighted(bool highlighted) {
    if (highlighted_ == highlighted) {
      return;
    }
    highlighted_ = highlighted;
    RefreshStyle();
  }

  // views::Button:
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
    const PickerColors colors =
        GetPickerColors(origin_chrome_style::IsDark(this));
    const bool hovered =
        GetState() == STATE_HOVERED || GetState() == STATE_PRESSED;
    SetBackground(views::CreateRoundedRectBackground(
        highlighted_ ? colors.selected_row
                     : (hovered ? colors.hovered_row : SK_ColorTRANSPARENT),
        kRowCornerRadius));
    icon_->SetImage(ui::ImageModel::FromVectorIcon(
        create_ ? kLeoPlusAddIcon : kLeoLabelIcon,
        highlighted_ ? colors.primary_text : colors.secondary_text, kIconSize));
    label_->SetEnabledColor(colors.primary_text);
    count_->SetEnabledColor(colors.tertiary_text);
    check_->SetImage(ui::ImageModel::FromVectorIcon(
        kLeoCheckCircleFilledIcon, origin_chrome_style::kAccent, kIconSize));
  }

  const bool create_;
  const bool checked_;
  bool highlighted_ = false;
  raw_ptr<views::ImageView> icon_ = nullptr;
  raw_ptr<views::Label> label_ = nullptr;
  raw_ptr<views::Label> count_ = nullptr;
  raw_ptr<views::ImageView> check_ = nullptr;
};

BEGIN_METADATA(OriginTagPickerView, Row)
END_METADATA

OriginTagPickerView::OriginTagPickerView(bookmarks::BookmarkModel* model,
                                         base::RepeatingClosure on_close)
    : on_close_(std::move(on_close)), model_(model) {
  SetVisible(false);
  SetPaintToLayer();
  layer()->SetFillsBoundsOpaquely(false);

  panel_ = AddChildView(std::make_unique<views::View>());
  panel_->SetPaintToLayer();
  panel_->layer()->SetFillsBoundsOpaquely(false);
  panel_->layer()->SetRoundedCornerRadius(
      gfx::RoundedCornersF(kPanelCornerRadius));
  panel_->layer()->SetBackgroundBlur(kPanelBlurSigma);
  panel_->layer()->SetBackdropFilterQuality(kPanelBackdropQuality);
  panel_->SetLayoutManager(std::make_unique<views::BoxLayout>(
      views::BoxLayout::Orientation::kVertical,
      gfx::Insets::TLBR(16, 18, 12, 18), 6));

  heading_ = panel_->AddChildView(
      MakeLabel(u"Tag page", 15, gfx::Font::Weight::SEMIBOLD));

  page_title_ = panel_->AddChildView(
      MakeLabel(std::u16string(), 12, gfx::Font::Weight::NORMAL));
  page_title_->SetElideBehavior(gfx::ELIDE_TAIL);

  field_container_ = panel_->AddChildView(std::make_unique<views::View>());
  field_container_->SetLayoutManager(std::make_unique<views::BoxLayout>(
      views::BoxLayout::Orientation::kVertical, gfx::Insets::VH(8, 10)));
  text_field_ =
      field_container_->AddChildView(std::make_unique<views::Textfield>());
  text_field_->SetPlaceholderText(u"Find or create a tag");
  text_field_->SetAccessibleName(u"Find or create a tag");
  text_field_->SetFontList(
      origin_chrome_style::Font(14, gfx::Font::Weight::NORMAL));
  text_field_->SetBackgroundEnabled(false);
  text_field_->SetBorder(views::CreateEmptyBorder(gfx::Insets()));
  text_field_->set_controller(this);

  auto list = std::make_unique<views::BoxLayoutView>();
  list->SetOrientation(views::BoxLayout::Orientation::kVertical);
  scroll_ = panel_->AddChildView(std::make_unique<views::ScrollView>());
  scroll_->SetBackgroundColor(std::nullopt);
  scroll_->SetHorizontalScrollBarMode(
      views::ScrollView::ScrollBarMode::kDisabled);
  scroll_->SetVerticalScrollBarMode(
      views::ScrollView::ScrollBarMode::kHiddenButEnabled);
  scroll_->SetDrawOverflowIndicator(false);
  scroll_->ClipHeightTo(0, kMaxVisibleRows * kRowHeight);
  list_ = scroll_->SetContents(std::move(list));

  hint_ = panel_->AddChildView(MakeLabel(u"Type a name to make your first tag.",
                                         12, gfx::Font::Weight::NORMAL));
  hint_->SetBorder(views::CreateEmptyBorder(gfx::Insets::VH(4, 10)));

  footer_ = panel_->AddChildView(
      MakeLabel(u"↑↓ choose   Enter tag or untag   Esc done", 11,
                gfx::Font::Weight::NORMAL));
  footer_->SetBorder(views::CreateEmptyBorder(gfx::Insets::TLBR(4, 2, 0, 0)));

  RefreshTheme();
}

OriginTagPickerView::~OriginTagPickerView() {
  text_field_->set_controller(nullptr);
}

void OriginTagPickerView::Show(const GURL& url,
                               const std::u16string& title,
                               std::optional<ui::KeyboardCode> activation_key) {
  url_ = url;
  title_ = title;
  page_title_->SetText(title.empty() ? base::UTF8ToUTF16(url.spec()) : title);
  highlighted_key_.clear();
  activation_key_to_suppress_.reset();
  text_field_->SetText(std::u16string());
  activation_key_to_suppress_ = activation_key;
  Rebuild();
  scroll_->ScrollToPosition(scroll_->vertical_scroll_bar(), 0);
  SetVisible(true);
  InvalidateLayout();
  text_field_->RequestFocus();
}

void OriginTagPickerView::Dismiss() {
  if (!GetVisible()) {
    return;
  }
  activation_key_to_suppress_.reset();
  SetVisible(false);
  on_close_.Run();
}

std::optional<size_t> OriginTagPickerView::GetHighlightedRowForTesting() const {
  if (highlighted_ >= rows_.size()) {
    return std::nullopt;
  }
  return highlighted_;
}

void OriginTagPickerView::ToggleTagForTesting(const std::u16string& tag) {
  ToggleTag(tag);
}

void OriginTagPickerView::Layout(PassKey) {
  const gfx::Rect available = GetContentsBounds();
  const int panel_width =
      std::min(kPanelWidth, std::max(0, available.width() - 48));
  const int panel_height = std::min(panel_->GetHeightForWidth(panel_width),
                                    std::max(0, available.height() - 24));
  // On the same shelf as Quick Open.
  const int free_height = std::max(0, available.height() - panel_height);
  const int panel_y = available.y() + std::clamp(free_height - 10, 12, 64);
  panel_->SetBounds(available.x() + (available.width() - panel_width) / 2,
                    panel_y, panel_width, panel_height);
}

void OriginTagPickerView::OnThemeChanged() {
  views::View::OnThemeChanged();
  RefreshTheme();
}

bool OriginTagPickerView::OnMousePressed(const ui::MouseEvent& event) {
  if (!panel_->bounds().Contains(event.location())) {
    Dismiss();
  }
  return true;
}

void OriginTagPickerView::ContentsChanged(views::Textfield* sender,
                                          const std::u16string& new_contents) {
  // On some platforms the character of the key that opened the picker
  // arrives after focus has moved here. Drop only that first character.
  if (activation_key_to_suppress_) {
    const ui::KeyboardCode key = *activation_key_to_suppress_;
    activation_key_to_suppress_.reset();
    if (IsActivationCharacter(key, new_contents)) {
      text_field_->SetText(std::u16string());
    }
  }
  highlighted_key_.clear();
  Rebuild();
  scroll_->ScrollToPosition(scroll_->vertical_scroll_bar(), 0);
}

bool OriginTagPickerView::HandleKeyEvent(views::Textfield* sender,
                                         const ui::KeyEvent& key_event) {
  if (activation_key_to_suppress_) {
    if (key_event.key_code() == *activation_key_to_suppress_) {
      // Stay armed until key-up, so a character that follows key-down is
      // still caught by ContentsChanged().
      if (key_event.type() == ui::EventType::kKeyReleased) {
        activation_key_to_suppress_.reset();
      }
      return true;
    }
    if (key_event.type() == ui::EventType::kKeyPressed) {
      activation_key_to_suppress_.reset();
    }
  }

  if (key_event.type() != ui::EventType::kKeyPressed) {
    return false;
  }
  switch (key_event.key_code()) {
    case ui::VKEY_ESCAPE:
      Dismiss();
      return true;
    case ui::VKEY_RETURN:
      if (highlighted_ < choices_.size()) {
        ToggleTag(choices_[highlighted_].name);
        // Typing again starts the next tag.
        text_field_->SelectAll(false);
      }
      return true;
    case ui::VKEY_DOWN:
      if (!rows_.empty()) {
        Highlight((highlighted_ + 1) % rows_.size());
      }
      return true;
    case ui::VKEY_UP:
      if (!rows_.empty()) {
        Highlight((highlighted_ + rows_.size() - 1) % rows_.size());
      }
      return true;
    default:
      return false;
  }
}

void OriginTagPickerView::OnRowPressed(const std::u16string& name) {
  base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
      FROM_HERE, base::BindOnce(&OriginTagPickerView::ToggleTag,
                                weak_factory_.GetWeakPtr(), name));
}

void OriginTagPickerView::ToggleTag(const std::u16string& name) {
  const std::u16string key = Fold(origin_tags::NormalizeName(name));
  if (!model_ || !url_.is_valid() || key.empty()) {
    return;
  }
  const std::vector<std::u16string> tags =
      origin_tags::GetTagsForUrl(model_, url_);
  const bool tagged = std::ranges::any_of(
      tags, [&key](const std::u16string& tag) { return Fold(tag) == key; });
  origin_tags::SetTagged(model_, name, url_, title_, !tagged);
  highlighted_key_ = key;
  Rebuild();
}

void OriginTagPickerView::Rebuild() {
  const std::u16string name =
      origin_tags::NormalizeName(text_field_->GetText());
  const std::u16string key = Fold(name);
  std::vector<std::u16string> checked;
  for (const std::u16string& tag : origin_tags::GetTagsForUrl(model_, url_)) {
    checked.push_back(Fold(tag));
  }

  // Tags starting with the text come first, then those containing it.
  std::vector<Choice> starts_with;
  std::vector<Choice> contains;
  bool exact = false;
  for (const origin_tags::Tag& tag : origin_tags::GetTags(model_)) {
    const std::u16string tag_key = Fold(tag.name);
    const size_t at = tag_key.find(key);
    if (at == std::u16string::npos) {
      continue;
    }
    exact |= tag_key == key;
    (at == 0 ? starts_with : contains)
        .push_back(
            {.name = tag.name,
             .page_count = tag.page_count,
             .checked = std::ranges::find(checked, tag_key) != checked.end()});
  }
  choices_.clear();
  if (!key.empty() && !exact) {
    choices_.push_back({.name = name, .create = true});
  }
  base::Extend(choices_, std::move(starts_with));
  base::Extend(choices_, std::move(contains));

  rows_.clear();
  list_->RemoveAllChildViews();
  for (const Choice& choice : choices_) {
    rows_.push_back(list_->AddChildView(std::make_unique<Row>(
        base::BindRepeating(&OriginTagPickerView::OnRowPressed,
                            base::Unretained(this), choice.name),
        choice)));
  }
  scroll_->SetVisible(!rows_.empty());
  hint_->SetVisible(rows_.empty());

  // The tag just toggled keeps the highlight; otherwise it starts on top.
  size_t highlighted = 0;
  for (size_t i = 0; i < choices_.size(); ++i) {
    if (!highlighted_key_.empty() && !choices_[i].create &&
        Fold(choices_[i].name) == highlighted_key_) {
      highlighted = i;
      break;
    }
  }
  Highlight(highlighted);
  InvalidateLayout();
}

void OriginTagPickerView::Highlight(size_t index) {
  highlighted_ = index;
  for (size_t i = 0; i < rows_.size(); ++i) {
    rows_[i]->SetHighlighted(i == index);
  }
  if (index < rows_.size()) {
    rows_[index]->ScrollViewToVisible();
  }
}

void OriginTagPickerView::RefreshTheme() {
  const PickerColors colors =
      GetPickerColors(origin_chrome_style::IsDark(this));
  SetBackground(views::CreateSolidBackground(colors.scrim));
  panel_->SetBackground(
      views::CreateRoundedRectBackground(colors.panel, kPanelCornerRadius));
  panel_->SetBorder(views::CreateRoundedRectBorder(1, kPanelCornerRadius,
                                                   colors.panel_stroke));
  heading_->SetEnabledColor(colors.primary_text);
  page_title_->SetEnabledColor(colors.secondary_text);
  field_container_->SetBackground(
      views::CreateRoundedRectBackground(colors.field, kRowCornerRadius));
  text_field_->SetColor(colors.primary_text);
  hint_->SetEnabledColor(colors.secondary_text);
  footer_->SetEnabledColor(colors.tertiary_text);
}

BEGIN_METADATA(OriginTagPickerView)
END_METADATA
