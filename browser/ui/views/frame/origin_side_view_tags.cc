// Copyright (c) 2026 The Brave Authors. All rights reserved.
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this file,
// You can obtain one at https://mozilla.org/MPL/2.0/.

#include "brave/browser/ui/views/frame/origin_side_view_tags.h"

#include <memory>
#include <optional>
#include <utility>

#include "base/functional/bind.h"
#include "base/i18n/case_conversion.h"
#include "base/location.h"
#include "base/memory/raw_ref.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/string_util.h"
#include "base/strings/utf_string_conversions.h"
#include "base/task/sequenced_task_runner.h"
#include "brave/browser/ui/tabs/origin_tags.h"
#include "brave/browser/ui/views/frame/origin_chrome_style.h"
#include "brave/components/vector_icons/vector_icons.h"
#include "cc/paint/paint_flags.h"
#include "components/bookmarks/browser/bookmark_model.h"
#include "components/url_formatter/elide_url.h"
#include "ui/base/metadata/metadata_impl_macros.h"
#include "ui/base/models/image_model.h"
#include "ui/events/event.h"
#include "ui/events/keycodes/keyboard_codes.h"
#include "ui/gfx/canvas.h"
#include "ui/gfx/geometry/insets.h"
#include "ui/gfx/geometry/rect_f.h"
#include "ui/gfx/geometry/size.h"
#include "ui/views/accessibility/view_accessibility.h"
#include "ui/views/animation/ink_drop.h"
#include "ui/views/background.h"
#include "ui/views/border.h"
#include "ui/views/controls/button/button.h"
#include "ui/views/controls/button/image_button.h"
#include "ui/views/controls/highlight_path_generator.h"
#include "ui/views/controls/image_view.h"
#include "ui/views/controls/label.h"
#include "ui/views/controls/scroll_view.h"
#include "ui/views/controls/textfield/textfield.h"
#include "ui/views/layout/box_layout.h"
#include "ui/views/layout/box_layout_view.h"
#include "url/gurl.h"

namespace {

// The search field sits where the Shortcuts tool's does; rows span the card
// as the Downloads tool's do.
constexpr int kHorizontalInset = 16;
constexpr int kListInset = 8;
constexpr int kTagRowHeight = 32;
constexpr int kPageRowHeight = 44;
constexpr int kRowCornerRadius = 8;
constexpr int kRowLeadingInset = 8;
constexpr int kRowSpacing = 8;
constexpr int kCaretSize = 12;
// Page rows line up with their tag's name.
constexpr int kPageIndent = kRowLeadingInset + kCaretSize + kRowSpacing;
constexpr int kActionButtonSize = 26;
constexpr int kActionIconSize = 16;
constexpr int kActionCornerRadius = 6;

std::unique_ptr<views::Label> MakeLabel(std::u16string text,
                                        int size,
                                        gfx::Font::Weight weight) {
  auto label = std::make_unique<views::Label>(std::move(text));
  label->SetFontList(origin_chrome_style::Font(size, weight));
  // The colors are picked for the side view's card, not for the dialog
  // background a label otherwise assumes it sits on.
  label->SetAutoColorReadabilityEnabled(false);
  return label;
}

std::u16string Fold(std::u16string_view text) {
  return base::i18n::FoldCase(text);
}

bool ContainsText(std::u16string_view haystack, std::u16string_view needle) {
  return haystack.find(needle) != std::u16string_view::npos;
}

std::u16string GetHost(const GURL& url) {
  std::u16string host =
      url_formatter::FormatUrlForDisplayOmitSchemePathAndTrivialSubdomains(url);
  return host.empty() ? base::UTF8ToUTF16(url.spec()) : host;
}

bool PageMatches(const origin_tags::Page& page, std::u16string_view needle) {
  return ContainsText(Fold(page.title), needle) ||
         ContainsText(Fold(base::UTF8ToUTF16(page.url.spec())), needle) ||
         ContainsText(Fold(GetHost(page.url)), needle);
}

// A row's trailing icon button, shown while the row is hovered or either has
// keyboard focus. The row is already highlighted then, so this hover takes
// the stronger selected fill.
class TagsRowActionButton : public views::ImageButton {
  METADATA_HEADER(TagsRowActionButton, views::ImageButton)

 public:
  // `focus_changed` runs whenever the button gains or loses focus.
  TagsRowActionButton(PressedCallback callback,
                      base::RepeatingClosure focus_changed,
                      const gfx::VectorIcon& icon,
                      const std::u16string& name)
      : views::ImageButton(std::move(callback)),
        focus_changed_(std::move(focus_changed)),
        icon_(icon) {
    SetPreferredSize(gfx::Size(kActionButtonSize, kActionButtonSize));
    SetImageHorizontalAlignment(ALIGN_CENTER);
    SetImageVerticalAlignment(ALIGN_MIDDLE);
    SetAccessibleName(name);
    SetTooltipText(name);
    views::InkDrop::Get(this)->SetMode(views::InkDropHost::InkDropMode::OFF);
    views::InstallRoundRectHighlightPathGenerator(this, gfx::Insets(),
                                                  kActionCornerRadius);
    UpdateImage();
  }

  // views::ImageButton:
  void StateChanged(ButtonState old_state) override {
    views::ImageButton::StateChanged(old_state);
    UpdateImage();
  }

  void OnThemeChanged() override {
    views::ImageButton::OnThemeChanged();
    UpdateImage();
  }

  void OnFocus() override {
    views::ImageButton::OnFocus();
    focus_changed_.Run();
  }

  void OnBlur() override {
    views::ImageButton::OnBlur();
    focus_changed_.Run();
  }

  void OnPaintBackground(gfx::Canvas* canvas) override {
    if (!IsHot()) {
      return;
    }
    cc::PaintFlags fill;
    fill.setAntiAlias(true);
    fill.setColor(
        origin_chrome_style::SelectedFill(origin_chrome_style::IsDark(this)));
    canvas->DrawRoundRect(gfx::RectF(GetLocalBounds()), kActionCornerRadius,
                          fill);
  }

 private:
  bool IsHot() const {
    return GetState() == STATE_HOVERED || GetState() == STATE_PRESSED;
  }

  void UpdateImage() {
    const bool dark = origin_chrome_style::IsDark(this);
    SetImageModel(STATE_NORMAL,
                  ui::ImageModel::FromVectorIcon(
                      *icon_,
                      IsHot() ? origin_chrome_style::PrimaryText(dark)
                              : origin_chrome_style::SecondaryText(dark),
                      kActionIconSize));
  }

  const base::RepeatingClosure focus_changed_;
  const raw_ref<const gfx::VectorIcon> icon_;
};

BEGIN_METADATA(TagsRowActionButton)
END_METADATA

// Paints the hover fill every side view row uses.
void PaintRowHover(views::Button* row, gfx::Canvas* canvas) {
  if (row->GetState() != views::Button::STATE_HOVERED &&
      row->GetState() != views::Button::STATE_PRESSED) {
    return;
  }
  cc::PaintFlags fill;
  fill.setAntiAlias(true);
  fill.setColor(
      origin_chrome_style::HoverFill(origin_chrome_style::IsDark(row)));
  canvas->DrawRoundRect(gfx::RectF(row->GetLocalBounds()), kRowCornerRadius,
                        fill);
}

void SetUpRow(views::Button* row, int height) {
  row->SetPreferredSize(gfx::Size(0, height));
  // Stay highlighted while the pointer is over the trailing button.
  row->SetNotifyEnterExitOnChild(true);
  views::InkDrop::Get(row)->SetMode(views::InkDropHost::InkDropMode::OFF);
  views::InstallRoundRectHighlightPathGenerator(row, gfx::Insets(),
                                                kRowCornerRadius);
}

}  // namespace

// One tag. Clicking it opens or closes its pages; hovering trades the page
// count for a delete button.
class OriginSideViewTags::TagRow : public views::Button {
  METADATA_HEADER(TagRow, views::Button)

 public:
  TagRow(const origin_tags::Tag& tag,
         bool expanded,
         PressedCallback on_toggle,
         PressedCallback on_delete)
      : views::Button(std::move(on_toggle)), expanded_(expanded) {
    SetUpRow(this, kTagRowHeight);
    SetAccessibleName(
        tag.name + u", " +
        (tag.page_count == 1
             ? std::u16string(u"1 page")
             : base::NumberToString16(tag.page_count) + u" pages"));
    if (expanded) {
      GetViewAccessibility().SetIsExpanded();
    } else {
      GetViewAccessibility().SetIsCollapsed();
    }

    auto* layout = SetLayoutManager(std::make_unique<views::BoxLayout>(
        views::BoxLayout::Orientation::kHorizontal,
        gfx::Insets::TLBR(0, kRowLeadingInset, 0, 6), kRowSpacing));
    layout->set_cross_axis_alignment(
        views::BoxLayout::CrossAxisAlignment::kCenter);

    caret_ = AddChildView(std::make_unique<views::ImageView>());
    caret_->SetImageSize(gfx::Size(kCaretSize, kCaretSize));
    caret_->SetCanProcessEventsWithinSubtree(false);

    name_ = AddChildView(MakeLabel(tag.name, 13, gfx::Font::Weight::MEDIUM));
    name_->SetHorizontalAlignment(gfx::ALIGN_LEFT);
    name_->SetElideBehavior(gfx::ELIDE_TAIL);
    name_->SetCanProcessEventsWithinSubtree(false);
    layout->SetFlexForView(name_, 1);

    // A key cap, as the Shortcuts tool draws them.
    count_ = AddChildView(MakeLabel(base::NumberToString16(tag.page_count), 11,
                                    gfx::Font::Weight::SEMIBOLD));
    count_->SetBorder(views::CreateEmptyBorder(gfx::Insets::VH(2, 6)));
    count_->SetCanProcessEventsWithinSubtree(false);

    delete_button_ = AddChildView(std::make_unique<TagsRowActionButton>(
        std::move(on_delete),
        base::BindRepeating(&TagRow::UpdateHover, base::Unretained(this)),
        kLeoTrashIcon, u"Delete tag"));

    UpdateHover();
    UpdateColors();
  }

  // views::Button:
  void StateChanged(ButtonState old_state) override {
    views::Button::StateChanged(old_state);
    UpdateHover();
  }

  void OnThemeChanged() override {
    views::Button::OnThemeChanged();
    UpdateColors();
  }

  void OnFocus() override {
    views::Button::OnFocus();
    UpdateHover();
  }

  void OnBlur() override {
    views::Button::OnBlur();
    UpdateHover();
  }

  void OnPaintBackground(gfx::Canvas* canvas) override {
    PaintRowHover(this, canvas);
  }

 private:
  void UpdateHover() {
    const bool hot = GetState() == STATE_HOVERED ||
                     GetState() == STATE_PRESSED || HasFocus() ||
                     delete_button_->HasFocus();
    count_->SetVisible(!hot);
    delete_button_->SetVisible(hot);
  }

  void UpdateColors() {
    const bool dark = origin_chrome_style::IsDark(this);
    caret_->SetImage(ui::ImageModel::FromVectorIcon(
        expanded_ ? kLeoCaratDownIcon : kLeoCaratRightIcon,
        origin_chrome_style::MutedText(dark), kCaretSize));
    name_->SetEnabledColor(origin_chrome_style::PrimaryText(dark));
    count_->SetEnabledColor(origin_chrome_style::SecondaryText(dark));
    count_->SetBackground(views::CreateRoundedRectBackground(
        origin_chrome_style::SelectedFill(dark), 5));
  }

  const bool expanded_;
  raw_ptr<views::ImageView> caret_ = nullptr;
  raw_ptr<views::Label> name_ = nullptr;
  raw_ptr<views::Label> count_ = nullptr;
  raw_ptr<TagsRowActionButton> delete_button_ = nullptr;
};

BEGIN_METADATA(OriginSideViewTags, TagRow)
END_METADATA

// A page under an open tag: its title over its host. Clicking opens it;
// hovering offers to take it out of the tag.
class OriginSideViewTags::PageRow : public views::Button {
  METADATA_HEADER(PageRow, views::Button)

 public:
  PageRow(const origin_tags::Page& page,
          PressedCallback on_open,
          PressedCallback on_remove)
      : views::Button(std::move(on_open)) {
    SetUpRow(this, kPageRowHeight);
    const std::u16string host = GetHost(page.url);
    const std::u16string title =
        page.title.empty() ? base::UTF8ToUTF16(page.url.spec()) : page.title;
    SetAccessibleName(title + u", " + host);
    SetTooltipText(base::UTF8ToUTF16(page.url.spec()));

    auto* layout = SetLayoutManager(std::make_unique<views::BoxLayout>(
        views::BoxLayout::Orientation::kHorizontal,
        gfx::Insets::TLBR(0, kPageIndent, 0, 6), kRowSpacing));
    layout->set_cross_axis_alignment(
        views::BoxLayout::CrossAxisAlignment::kCenter);

    auto* text = AddChildView(std::make_unique<views::BoxLayoutView>());
    text->SetOrientation(views::BoxLayout::Orientation::kVertical);
    text->SetMainAxisAlignment(views::BoxLayout::MainAxisAlignment::kCenter);
    text->SetBetweenChildSpacing(2);
    text->SetCanProcessEventsWithinSubtree(false);
    layout->SetFlexForView(text, 1);

    title_ =
        text->AddChildView(MakeLabel(title, 13, gfx::Font::Weight::NORMAL));
    title_->SetHorizontalAlignment(gfx::ALIGN_LEFT);
    title_->SetElideBehavior(gfx::ELIDE_TAIL);

    host_ = text->AddChildView(MakeLabel(host, 11, gfx::Font::Weight::NORMAL));
    host_->SetHorizontalAlignment(gfx::ALIGN_LEFT);
    // The registrable domain says more than a long subdomain.
    host_->SetElideBehavior(gfx::ELIDE_HEAD);

    remove_button_ = AddChildView(std::make_unique<TagsRowActionButton>(
        std::move(on_remove),
        base::BindRepeating(&PageRow::UpdateHover, base::Unretained(this)),
        kLeoCloseIcon, u"Remove from tag"));

    UpdateHover();
    UpdateColors();
  }

  // views::Button:
  void StateChanged(ButtonState old_state) override {
    views::Button::StateChanged(old_state);
    UpdateHover();
  }

  void OnThemeChanged() override {
    views::Button::OnThemeChanged();
    UpdateColors();
  }

  void OnFocus() override {
    views::Button::OnFocus();
    UpdateHover();
  }

  void OnBlur() override {
    views::Button::OnBlur();
    UpdateHover();
  }

  void OnPaintBackground(gfx::Canvas* canvas) override {
    PaintRowHover(this, canvas);
  }

 private:
  void UpdateHover() {
    remove_button_->SetVisible(GetState() == STATE_HOVERED ||
                               GetState() == STATE_PRESSED || HasFocus() ||
                               remove_button_->HasFocus());
  }

  void UpdateColors() {
    const bool dark = origin_chrome_style::IsDark(this);
    title_->SetEnabledColor(origin_chrome_style::PrimaryText(dark));
    host_->SetEnabledColor(origin_chrome_style::SecondaryText(dark));
  }

  raw_ptr<views::Label> title_ = nullptr;
  raw_ptr<views::Label> host_ = nullptr;
  raw_ptr<TagsRowActionButton> remove_button_ = nullptr;
};

BEGIN_METADATA(OriginSideViewTags, PageRow)
END_METADATA

OriginSideViewTags::OriginSideViewTags(
    bookmarks::BookmarkModel* model,
    base::RepeatingCallback<void(const GURL&)> open_url)
    : open_url_(std::move(open_url)), model_(model) {
  auto* layout = SetLayoutManager(std::make_unique<views::BoxLayout>(
      views::BoxLayout::Orientation::kVertical));

  search_row_ = AddChildView(std::make_unique<views::View>());
  search_row_->SetLayoutManager(std::make_unique<views::BoxLayout>(
      views::BoxLayout::Orientation::kVertical,
      gfx::Insets::TLBR(12, kHorizontalInset, 8, kHorizontalInset)));
  // The field sits on a rounded fill of its own, as Origin's other fields do.
  search_field_ = search_row_->AddChildView(std::make_unique<views::View>());
  search_field_->SetLayoutManager(std::make_unique<views::BoxLayout>(
      views::BoxLayout::Orientation::kVertical, gfx::Insets::VH(6, 10)));
  search_ = search_field_->AddChildView(std::make_unique<views::Textfield>());
  search_->SetPlaceholderText(u"Search tags");
  search_->SetAccessibleName(u"Search tags");
  search_->SetFontList(
      origin_chrome_style::Font(13, gfx::Font::Weight::NORMAL));
  search_->SetBackgroundEnabled(false);
  search_->SetBorder(views::CreateEmptyBorder(gfx::Insets()));
  search_->set_controller(this);

  auto contents = std::make_unique<views::BoxLayoutView>();
  contents->SetOrientation(views::BoxLayout::Orientation::kVertical);
  contents->SetInsideBorderInsets(
      gfx::Insets::TLBR(0, kListInset, kListInset, kListInset));
  list_ = contents->AddChildView(std::make_unique<views::BoxLayoutView>());
  list_->SetOrientation(views::BoxLayout::Orientation::kVertical);
  no_match_ = contents->AddChildView(MakeLabel(u"No tag or page matches that.",
                                               12, gfx::Font::Weight::NORMAL));
  no_match_->SetBorder(views::CreateEmptyBorder(gfx::Insets::VH(16, 0)));
  no_match_->SetVisible(false);

  scroll_ = AddChildView(std::make_unique<views::ScrollView>());
  scroll_->SetBackgroundColor(std::nullopt);
  scroll_->SetHorizontalScrollBarMode(
      views::ScrollView::ScrollBarMode::kDisabled);
  scroll_->SetVerticalScrollBarMode(
      views::ScrollView::ScrollBarMode::kHiddenButEnabled);
  scroll_->SetDrawOverflowIndicator(false);
  scroll_->SetContents(std::move(contents));
  layout->SetFlexForView(scroll_, 1);

  auto empty_state = std::make_unique<views::BoxLayoutView>();
  empty_state->SetOrientation(views::BoxLayout::Orientation::kVertical);
  empty_state->SetMainAxisAlignment(
      views::BoxLayout::MainAxisAlignment::kCenter);
  empty_state->SetCrossAxisAlignment(
      views::BoxLayout::CrossAxisAlignment::kCenter);
  empty_state->SetBetweenChildSpacing(8);
  empty_state->SetInsideBorderInsets(gfx::Insets::VH(24, 28));
  empty_icon_ = empty_state->AddChildView(std::make_unique<views::ImageView>());
  empty_title_ = empty_state->AddChildView(
      MakeLabel(u"No tags yet", 14, gfx::Font::Weight::SEMIBOLD));
  empty_body_ = empty_state->AddChildView(
      MakeLabel(u"Press t on a page to tag it. Search tags in Quick Open "
                u"with #.",
                12, gfx::Font::Weight::NORMAL));
  empty_body_->SetMultiLine(true);
  empty_body_->SetMaximumWidth(260);
  empty_body_->SetHorizontalAlignment(gfx::ALIGN_CENTER);
  empty_state_ = AddChildView(std::move(empty_state));
  layout->SetFlexForView(empty_state_, 1);

  if (model_) {
    model_observation_.Observe(model_);
  }
  Rebuild();
  UpdateColors();
}

OriginSideViewTags::~OriginSideViewTags() = default;

void OriginSideViewTags::FocusSearch() {
  search_->RequestFocus();
  search_->SelectAll(false);
}

void OriginSideViewTags::ToggleTagForTesting(const std::u16string& tag) {
  const std::u16string key = Fold(tag);
  const std::u16string needle =
      Fold(base::TrimWhitespace(query_, base::TRIM_ALL));
  ToggleTag(key, !needle.empty() && !ContainsText(key, needle));
}

void OriginSideViewTags::OnThemeChanged() {
  views::View::OnThemeChanged();
  UpdateColors();
}

void OriginSideViewTags::ContentsChanged(views::Textfield* sender,
                                         const std::u16string& new_contents) {
  query_ = new_contents;
  collapsed_in_search_.clear();
  Rebuild();
  scroll_->ScrollToPosition(scroll_->vertical_scroll_bar(), 0);
}

bool OriginSideViewTags::HandleKeyEvent(views::Textfield* sender,
                                        const ui::KeyEvent& key_event) {
  // Escape clears the search first, so a second Escape can leave the field.
  if (key_event.type() == ui::EventType::kKeyPressed &&
      key_event.key_code() == ui::VKEY_ESCAPE && !sender->GetText().empty()) {
    sender->SetText(std::u16string());
    ContentsChanged(sender, std::u16string());
    return true;
  }
  return false;
}

void OriginSideViewTags::BookmarkModelChanged() {
  // An import or a sync cycle can change thousands of bookmarks; wait for the
  // end of it.
  if (model_ && model_->IsDoingExtensiveChanges()) {
    return;
  }
  ScheduleRebuild();
}

void OriginSideViewTags::BookmarkModelBeingDeleted() {
  model_observation_.Reset();
  model_ = nullptr;
  ScheduleRebuild();
}

void OriginSideViewTags::ExtensiveBookmarkChangesEnded() {
  ScheduleRebuild();
}

void OriginSideViewTags::OnTagPressed(const std::u16string& key,
                                      bool opened_by_search) {
  base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
      FROM_HERE,
      base::BindOnce(&OriginSideViewTags::ToggleTag, weak_factory_.GetWeakPtr(),
                     key, opened_by_search));
}

void OriginSideViewTags::OnDeleteTagPressed(const std::u16string& tag) {
  base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
      FROM_HERE, base::BindOnce(&OriginSideViewTags::DeleteTag,
                                weak_factory_.GetWeakPtr(), tag));
}

void OriginSideViewTags::OnPagePressed(const GURL& url) {
  base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
      FROM_HERE, base::BindOnce(&OriginSideViewTags::OpenUrl,
                                weak_factory_.GetWeakPtr(), url));
}

void OriginSideViewTags::OnRemovePagePressed(const std::u16string& tag,
                                             const GURL& url) {
  base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
      FROM_HERE, base::BindOnce(&OriginSideViewTags::RemovePage,
                                weak_factory_.GetWeakPtr(), tag, url));
}

void OriginSideViewTags::ToggleTag(const std::u16string& key,
                                   bool opened_by_search) {
  base::flat_set<std::u16string>& toggled =
      opened_by_search ? collapsed_in_search_ : expanded_;
  if (!toggled.erase(key)) {
    toggled.insert(key);
  }
  Rebuild();
}

void OriginSideViewTags::DeleteTag(const std::u16string& tag) {
  if (!model_) {
    return;
  }
  // A tag made again later with this name starts closed.
  expanded_.erase(Fold(tag));
  // The model reports the change, which rebuilds the list.
  origin_tags::DeleteTag(model_, tag);
}

void OriginSideViewTags::RemovePage(const std::u16string& tag,
                                    const GURL& url) {
  if (model_) {
    origin_tags::SetTagged(model_, tag, url, std::u16string(),
                           /*tagged=*/false);
  }
}

void OriginSideViewTags::OpenUrl(const GURL& url) {
  open_url_.Run(url);
}

void OriginSideViewTags::ScheduleRebuild() {
  if (rebuild_scheduled_) {
    return;
  }
  rebuild_scheduled_ = true;
  base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
      FROM_HERE,
      base::BindOnce(&OriginSideViewTags::Rebuild, weak_factory_.GetWeakPtr()));
}

void OriginSideViewTags::Rebuild() {
  rebuild_scheduled_ = false;
  tag_rows_.clear();
  page_rows_.clear();
  no_pages_labels_.clear();
  list_->RemoveAllChildViews();

  // Nothing at all until the model has loaded: an empty list may only mean
  // "not yet".
  const bool loaded = model_ && model_->loaded();
  const std::vector<origin_tags::Tag> tags =
      loaded ? origin_tags::GetTags(model_) : std::vector<origin_tags::Tag>();
  empty_state_->SetVisible(loaded && tags.empty());
  search_row_->SetVisible(!tags.empty());
  scroll_->SetVisible(!tags.empty());

  const bool dark = origin_chrome_style::IsDark(this);
  const std::u16string needle =
      Fold(base::TrimWhitespace(query_, base::TRIM_ALL));
  for (const origin_tags::Tag& tag : tags) {
    const std::u16string key = Fold(tag.name);
    // A tag whose name matches shows as usual. One that only has matching
    // pages opens onto just those, until the user closes it.
    const bool name_matches = needle.empty() || ContainsText(key, needle);
    bool expanded = false;
    std::vector<origin_tags::Page> pages;
    if (name_matches) {
      expanded = expanded_.contains(key);
      if (expanded) {
        pages = origin_tags::GetPages(model_, tag.name);
      }
    } else {
      pages = origin_tags::GetPages(model_, tag.name);
      std::erase_if(pages, [&needle](const origin_tags::Page& page) {
        return !PageMatches(page, needle);
      });
      if (pages.empty()) {
        continue;
      }
      expanded = !collapsed_in_search_.contains(key);
    }

    tag_rows_.push_back(list_->AddChildView(std::make_unique<TagRow>(
        tag, expanded,
        base::BindRepeating(&OriginSideViewTags::OnTagPressed,
                            base::Unretained(this), key, !name_matches),
        base::BindRepeating(&OriginSideViewTags::OnDeleteTagPressed,
                            base::Unretained(this), tag.name))));
    if (!expanded) {
      continue;
    }
    if (pages.empty()) {
      auto* label = list_->AddChildView(
          MakeLabel(u"No pages", 12, gfx::Font::Weight::NORMAL));
      label->SetHorizontalAlignment(gfx::ALIGN_LEFT);
      label->SetBorder(
          views::CreateEmptyBorder(gfx::Insets::TLBR(4, kPageIndent, 8, 0)));
      label->SetEnabledColor(origin_chrome_style::MutedText(dark));
      no_pages_labels_.push_back(label);
      continue;
    }
    for (const origin_tags::Page& page : pages) {
      page_rows_.push_back(list_->AddChildView(std::make_unique<PageRow>(
          page,
          base::BindRepeating(&OriginSideViewTags::OnPagePressed,
                              base::Unretained(this), page.url),
          base::BindRepeating(&OriginSideViewTags::OnRemovePagePressed,
                              base::Unretained(this), tag.name, page.url))));
    }
  }
  no_match_->SetVisible(!tags.empty() && tag_rows_.empty());
  InvalidateLayout();
}

void OriginSideViewTags::UpdateColors() {
  const bool dark = origin_chrome_style::IsDark(this);
  search_field_->SetBackground(views::CreateRoundedRectBackground(
      origin_chrome_style::HoverFill(dark), 8));
  search_->SetColor(origin_chrome_style::PrimaryText(dark));
  no_match_->SetEnabledColor(origin_chrome_style::SecondaryText(dark));
  for (views::Label* label : no_pages_labels_) {
    label->SetEnabledColor(origin_chrome_style::MutedText(dark));
  }
  empty_icon_->SetImage(ui::ImageModel::FromVectorIcon(
      kLeoLabelIcon, origin_chrome_style::MutedText(dark), 28));
  empty_title_->SetEnabledColor(origin_chrome_style::PrimaryText(dark));
  empty_body_->SetEnabledColor(origin_chrome_style::SecondaryText(dark));
}

BEGIN_METADATA(OriginSideViewTags)
END_METADATA
