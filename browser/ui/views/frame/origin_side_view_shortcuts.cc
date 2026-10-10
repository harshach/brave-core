// Copyright (c) 2026 The Brave Authors. All rights reserved.
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this file,
// You can obtain one at https://mozilla.org/MPL/2.0/.

#include "brave/browser/ui/views/frame/origin_side_view_shortcuts.h"

#include <algorithm>
#include <memory>
#include <utility>

#include "base/i18n/case_conversion.h"
#include "base/strings/string_util.h"
#include "brave/browser/ui/views/frame/origin_chrome_style.h"
#include "build/build_config.h"
#include "ui/base/metadata/metadata_impl_macros.h"
#include "ui/events/event.h"
#include "ui/events/keycodes/keyboard_codes.h"
#include "ui/views/background.h"
#include "ui/views/border.h"
#include "ui/views/controls/label.h"
#include "ui/views/controls/scroll_view.h"
#include "ui/views/controls/textfield/textfield.h"
#include "ui/views/layout/box_layout.h"
#include "ui/views/layout/box_layout_view.h"
#include "ui/views/view_utils.h"

namespace {

#if BUILDFLAG(IS_MAC)
constexpr char16_t kMod[] = u"⌘";
#else
constexpr char16_t kMod[] = u"Ctrl ";
#endif

constexpr int kHorizontalInset = 16;
constexpr int kRowHeight = 30;

std::u16string Mod(std::u16string_view key) {
  return std::u16string(kMod) + std::u16string(key);
}

// One key cap. Keys read as keys, not as prose.
std::unique_ptr<views::Label> MakeKeyCap(const std::u16string& key) {
  auto cap = std::make_unique<views::Label>(key);
  cap->SetFontList(origin_chrome_style::Font(11, gfx::Font::Weight::SEMIBOLD));
  cap->SetHorizontalAlignment(gfx::ALIGN_CENTER);
  cap->SetBorder(views::CreateEmptyBorder(gfx::Insets::VH(2, 6)));
  return cap;
}

}  // namespace

// static
std::vector<OriginSideViewShortcuts::Section>
OriginSideViewShortcuts::GetSections() {
  // Keep in step with BraveBrowserView::PreHandleKeyboardEvent(). Single keys
  // work while the page has no text field focused.
  return {
      {u"Pages",
       {
           {{u"j", u"↓"}, u"Next page"},
           {{u"k", u"↑"}, u"Previous page"},
           {{u"d"}, u"Close the page and its subpages"},
           {{u"w"}, u"Close the page"},
           {{u"z"}, u"Reopen the last closed page"},
           {{u"r"}, u"Reload"},
           {{u"["}, u"Back"},
           {{u"]"}, u"Forward"},
           {{u"p"}, u"Pin or unpin"},
           {{u"h"}, u"Return a pinned page to its pinned address"},
           {{u"c"}, u"Rename the page"},
           {{u"m"}, u"Mute or unmute this window"},
           {{u"f"}, u"Focus mode"},
       }},
      {u"Spaces",
       {
           {{u"1–9"}, u"Go to a Space in the bottom row"},
           {{Mod(u"↑"), Mod(u"↓")}, u"Previous or next Space"},
           {{u"e"}, u"Edit this Space"},
       }},
      {u"Open",
       {
           {{u"o", u"n"}, u"Find or open a page"},
           {{Mod(u"T")}, u"Find or open a page"},
           {{Mod(u"K")}, u"Commands"},
           {{u"#"}, u"In Quick Open, search tagged pages"},
       }},
      {u"Panels",
       {
           {{u"\\"}, u"Show or hide this panel"},
           {{u"."}, u"Move the page to the panel, or back to the list"},
           {{u"?"}, u"Shortcuts"},
           {{u"s"}, u"Split with a new page"},
           {{Mod(u"→")}, u"Close the split"},
           {{Mod(u"\\"), Mod(u"←")}, u"Show or hide the sidebar"},
       }},
      {u"Later",
       {
           {{u"l"}, u"Snooze the page"},
           {{u"a"}, u"Remind me about the page"},
           {{u"t"}, u"Tag the page"},
       }},
      {u"Typing",
       {
           {{u"i"}, u"Type into the page"},
           {{u"Esc"}, u"Back to single-key shortcuts"},
       }},
  };
}

OriginSideViewShortcuts::OriginSideViewShortcuts() {
  auto* layout = SetLayoutManager(std::make_unique<views::BoxLayout>(
      views::BoxLayout::Orientation::kVertical));

  auto* search_row = AddChildView(std::make_unique<views::View>());
  search_row->SetLayoutManager(std::make_unique<views::BoxLayout>(
      views::BoxLayout::Orientation::kVertical,
      gfx::Insets::TLBR(12, kHorizontalInset, 8, kHorizontalInset)));
  // The field sits on a rounded fill of its own, as Origin's other fields do.
  search_field_ = search_row->AddChildView(std::make_unique<views::View>());
  search_field_->SetLayoutManager(std::make_unique<views::BoxLayout>(
      views::BoxLayout::Orientation::kVertical, gfx::Insets::VH(6, 10)));
  search_ = search_field_->AddChildView(std::make_unique<views::Textfield>());
  search_->SetPlaceholderText(u"Search shortcuts");
  search_->SetAccessibleName(u"Search shortcuts");
  search_->SetFontList(origin_chrome_style::Font(13, gfx::Font::Weight::NORMAL));
  search_->SetBackgroundEnabled(false);
  search_->SetBorder(views::CreateEmptyBorder(gfx::Insets()));
  search_->set_controller(this);

  auto list = std::make_unique<views::BoxLayoutView>();
  list->SetOrientation(views::BoxLayout::Orientation::kVertical);
  list->SetInsideBorderInsets(
      gfx::Insets::TLBR(0, kHorizontalInset, 16, kHorizontalInset));

  const std::vector<Section> sections = GetSections();
  for (size_t section_index = 0; section_index < sections.size();
       ++section_index) {
    const Section& section = sections[section_index];
    auto* title = list->AddChildView(std::make_unique<views::Label>(
        base::i18n::ToUpper(section.title)));
    title->SetFontList(origin_chrome_style::Font(10, gfx::Font::Weight::BOLD));
    title->SetHorizontalAlignment(gfx::ALIGN_LEFT);
    title->SetBorder(views::CreateEmptyBorder(gfx::Insets::TLBR(14, 0, 4, 0)));
    section_titles_.push_back(title);

    for (const Shortcut& shortcut : section.shortcuts) {
      auto* row = list->AddChildView(std::make_unique<views::BoxLayoutView>());
      row->SetOrientation(views::BoxLayout::Orientation::kHorizontal);
      row->SetCrossAxisAlignment(views::BoxLayout::CrossAxisAlignment::kCenter);
      row->SetBetweenChildSpacing(4);
      row->SetMinimumCrossAxisSize(kRowHeight);
      auto* description =
          row->AddChildView(std::make_unique<views::Label>(shortcut.description));
      description->SetFontList(
          origin_chrome_style::Font(13, gfx::Font::Weight::NORMAL));
      description->SetHorizontalAlignment(gfx::ALIGN_LEFT);
      description->SetElideBehavior(gfx::ELIDE_TAIL);
      row->SetFlexForView(description, 1);

      std::u16string haystack = shortcut.description;
      for (const std::u16string& key : shortcut.keys) {
        row->AddChildView(MakeKeyCap(key));
        haystack += u" " + key;
      }
      rows_.push_back(
          {row, base::i18n::ToLower(haystack), section_index});
    }
  }

  no_match_ = list->AddChildView(
      std::make_unique<views::Label>(u"No shortcut matches that."));
  no_match_->SetFontList(origin_chrome_style::Font(12, gfx::Font::Weight::NORMAL));
  no_match_->SetBorder(views::CreateEmptyBorder(gfx::Insets::VH(16, 0)));
  no_match_->SetVisible(false);

  scroll_ = AddChildView(std::make_unique<views::ScrollView>());
  scroll_->SetBackgroundColor(std::nullopt);
  scroll_->SetHorizontalScrollBarMode(
      views::ScrollView::ScrollBarMode::kDisabled);
  scroll_->SetVerticalScrollBarMode(
      views::ScrollView::ScrollBarMode::kHiddenButEnabled);
  scroll_->SetDrawOverflowIndicator(false);
  scroll_->SetContents(std::move(list));
  layout->SetFlexForView(scroll_, 1);
}

OriginSideViewShortcuts::~OriginSideViewShortcuts() = default;

void OriginSideViewShortcuts::FocusSearch() {
  search_->RequestFocus();
  search_->SelectAll(false);
}

size_t OriginSideViewShortcuts::GetVisibleRowCountForTesting() const {
  return std::ranges::count_if(rows_,
                               [](const Row& row) { return row.view->GetVisible(); });
}

void OriginSideViewShortcuts::OnThemeChanged() {
  views::View::OnThemeChanged();
  UpdateColors();
}

void OriginSideViewShortcuts::ContentsChanged(
    views::Textfield* sender,
    const std::u16string& new_contents) {
  ApplyFilter(new_contents);
}

bool OriginSideViewShortcuts::HandleKeyEvent(views::Textfield* sender,
                                             const ui::KeyEvent& key_event) {
  // Escape clears the search first, so a second Escape can leave the field.
  if (key_event.type() == ui::EventType::kKeyPressed &&
      key_event.key_code() == ui::VKEY_ESCAPE && !sender->GetText().empty()) {
    sender->SetText(std::u16string());
    ApplyFilter(std::u16string());
    return true;
  }
  return false;
}

void OriginSideViewShortcuts::ApplyFilter(const std::u16string& query) {
  const std::u16string needle = base::i18n::ToLower(
      base::TrimWhitespace(query, base::TRIM_ALL));
  std::vector<bool> section_has_rows(section_titles_.size(), false);
  bool any = false;
  for (Row& row : rows_) {
    const bool match =
        needle.empty() || row.haystack.find(needle) != std::u16string::npos;
    row.view->SetVisible(match);
    if (match) {
      section_has_rows[row.section] = true;
      any = true;
    }
  }
  for (size_t i = 0; i < section_titles_.size(); ++i) {
    section_titles_[i]->SetVisible(section_has_rows[i]);
  }
  no_match_->SetVisible(!any);
  scroll_->contents()->InvalidateLayout();
  scroll_->ScrollToPosition(scroll_->vertical_scroll_bar(), 0);
}

void OriginSideViewShortcuts::UpdateColors() {
  const bool dark = origin_chrome_style::IsDark(this);
  search_field_->SetBackground(views::CreateRoundedRectBackground(
      origin_chrome_style::HoverFill(dark), 8));
  search_->SetColor(origin_chrome_style::PrimaryText(dark));
  for (auto& title : section_titles_) {
    title->SetEnabledColor(origin_chrome_style::MutedText(dark));
  }
  no_match_->SetEnabledColor(origin_chrome_style::SecondaryText(dark));
  for (Row& row : rows_) {
    auto children = row.view->children();
    for (size_t i = 0; i < children.size(); ++i) {
      auto* label = views::AsViewClass<views::Label>(children[i]);
      if (!label) {
        continue;
      }
      if (i == 0) {
        label->SetEnabledColor(origin_chrome_style::PrimaryText(dark));
        continue;
      }
      label->SetEnabledColor(origin_chrome_style::PrimaryText(dark));
      label->SetBackground(views::CreateRoundedRectBackground(
          origin_chrome_style::SelectedFill(dark), 5));
    }
  }
}

BEGIN_METADATA(OriginSideViewShortcuts)
END_METADATA
