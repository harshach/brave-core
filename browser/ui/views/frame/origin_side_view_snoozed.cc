// Copyright (c) 2026 The Brave Authors. All rights reserved.
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this file,
// You can obtain one at https://mozilla.org/MPL/2.0/.

#include "brave/browser/ui/views/frame/origin_side_view_snoozed.h"

#include <algorithm>
#include <array>
#include <memory>
#include <string_view>
#include <utility>

#include "base/check.h"
#include "base/functional/bind.h"
#include "base/i18n/icubridge/date_time_formatter.h"
#include "base/i18n/icubridge/icu_bridge.h"
#include "base/i18n/time_formatting.h"
#include "base/location.h"
#include "base/strings/strcat.h"
#include "base/strings/string_util.h"
#include "base/strings/utf_string_conversions.h"
#include "base/task/sequenced_task_runner.h"
#include "brave/browser/ui/views/frame/origin_chrome_style.h"
#include "brave/components/vector_icons/vector_icons.h"
#include "cc/paint/paint_flags.h"
#include "ui/base/metadata/metadata_impl_macros.h"
#include "ui/base/models/image_model.h"
#include "ui/events/event.h"
#include "ui/gfx/canvas.h"
#include "ui/gfx/geometry/insets.h"
#include "ui/gfx/geometry/rect.h"
#include "ui/gfx/geometry/rect_f.h"
#include "ui/gfx/geometry/size.h"
#include "ui/gfx/image/image_skia.h"
#include "ui/gfx/paint_vector_icon.h"
#include "ui/views/animation/ink_drop.h"
#include "ui/views/controls/button/image_button.h"
#include "ui/views/controls/highlight_path_generator.h"
#include "ui/views/controls/image_view.h"
#include "ui/views/controls/label.h"
#include "ui/views/controls/scroll_view.h"
#include "ui/views/focus/focus_manager.h"
#include "ui/views/layout/box_layout.h"
#include "ui/views/layout/box_layout_view.h"
#include "url/gurl.h"

namespace {

constexpr int kListInset = 8;
constexpr int kRowHeight = 48;
constexpr int kRowCornerRadius = 8;
constexpr int kTileSize = 24;
constexpr int kTileCornerRadius = 6;
constexpr int kTileIconSize = 14;
constexpr int kActionButtonSize = 26;
constexpr int kActionIconSize = 16;
constexpr int kActionCornerRadius = 6;
constexpr base::TimeDelta kRefreshInterval = base::Minutes(1);

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

bool IsSameDay(const base::Time::Exploded& a, const base::Time::Exploded& b) {
  return a.year == b.year && a.month == b.month &&
         a.day_of_month == b.day_of_month;
}

std::u16string GetPageTitle(const OriginLaterService::SnoozedPage& page) {
  if (!page.title.empty()) {
    return page.title;
  }
  const std::string_view host = page.url.host();
  return base::UTF8ToUTF16(host.empty() ? page.url.spec() : host);
}

// The first letter of the page's site, or nothing for a page without one.
std::u16string GetSiteInitial(const GURL& url) {
  std::string_view host = url.host();
  if (host.starts_with("www.")) {
    host.remove_prefix(4);
  }
  if (host.empty() || !base::IsAsciiAlphaNumeric(host.front())) {
    return std::u16string();
  }
  return std::u16string(
      1, base::ToUpperASCII(static_cast<char16_t>(host.front())));
}

// A row's icon button. The row is already highlighted while the pointer is
// over it, so this hover takes the stronger selected fill. Until revealed it
// paints nothing, but keeps its place and can still take focus.
class RowActionButton : public views::ImageButton {
  METADATA_HEADER(RowActionButton, views::ImageButton)

 public:
  // `focus_changed` runs whenever the button gains or loses focus.
  RowActionButton(PressedCallback callback,
                  base::RepeatingClosure focus_changed)
      : views::ImageButton(std::move(callback)),
        focus_changed_(std::move(focus_changed)) {
    SetPreferredSize(gfx::Size(kActionButtonSize, kActionButtonSize));
    SetImageHorizontalAlignment(ALIGN_CENTER);
    SetImageVerticalAlignment(ALIGN_MIDDLE);
    views::InkDrop::Get(this)->SetMode(views::InkDropHost::InkDropMode::OFF);
    views::InstallRoundRectHighlightPathGenerator(this, gfx::Insets(),
                                                  kActionCornerRadius);
  }

  // `subject` names the row for assistive tech, which can move from button
  // to button without passing through the row's text.
  void SetAction(const gfx::VectorIcon& icon,
                 const std::u16string& name,
                 const std::u16string& subject) {
    icon_ = &icon;
    SetTooltipText(name);
    SetAccessibleName(base::StrCat({name, u", ", subject}));
    UpdateImage();
  }

  void SetRevealed(bool revealed) {
    if (revealed_ == revealed) {
      return;
    }
    revealed_ = revealed;
    SchedulePaint();
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
    if (!revealed_ || !IsHot()) {
      return;
    }
    cc::PaintFlags fill;
    fill.setAntiAlias(true);
    fill.setColor(
        origin_chrome_style::SelectedFill(origin_chrome_style::IsDark(this)));
    canvas->DrawRoundRect(gfx::RectF(GetLocalBounds()), kActionCornerRadius,
                          fill);
  }

  void PaintButtonContents(gfx::Canvas* canvas) override {
    if (revealed_) {
      views::ImageButton::PaintButtonContents(canvas);
    }
  }

 private:
  bool IsHot() const {
    return GetState() == STATE_HOVERED || GetState() == STATE_PRESSED;
  }

  void UpdateImage() {
    if (!icon_) {
      return;
    }
    const bool dark = origin_chrome_style::IsDark(this);
    SetImageModel(STATE_NORMAL,
                  ui::ImageModel::FromVectorIcon(
                      *icon_,
                      IsHot() ? origin_chrome_style::PrimaryText(dark)
                              : origin_chrome_style::SecondaryText(dark),
                      kActionIconSize));
  }

  const base::RepeatingClosure focus_changed_;
  raw_ptr<const gfx::VectorIcon> icon_ = nullptr;
  bool revealed_ = true;
};

BEGIN_METADATA(RowActionButton)
END_METADATA

// A snoozed page's mark: the first letter of its site, or a sleeping clock.
class PageTile : public views::View {
  METADATA_HEADER(PageTile, views::View)

 public:
  explicit PageTile(std::u16string initial) : initial_(std::move(initial)) {
    SetPreferredSize(gfx::Size(kTileSize, kTileSize));
  }

  // views::View:
  void OnPaint(gfx::Canvas* canvas) override {
    const bool dark = origin_chrome_style::IsDark(this);
    cc::PaintFlags fill;
    fill.setAntiAlias(true);
    fill.setColor(origin_chrome_style::SelectedFill(dark));
    canvas->DrawRoundRect(gfx::RectF(GetLocalBounds()), kTileCornerRadius,
                          fill);

    const SkColor color = origin_chrome_style::SecondaryText(dark);
    if (initial_.empty()) {
      const gfx::ImageSkia icon =
          gfx::CreateVectorIcon(kLeoSleepTimerIcon, kTileIconSize, color);
      canvas->DrawImageInt(icon, (width() - icon.width()) / 2,
                           (height() - icon.height()) / 2);
      return;
    }
    canvas->DrawStringRectWithFlags(
        initial_, origin_chrome_style::Font(12, gfx::Font::Weight::SEMIBOLD),
        color, GetLocalBounds(), gfx::Canvas::TEXT_ALIGN_CENTER);
  }

 private:
  const std::u16string initial_;
};

BEGIN_METADATA(PageTile)
END_METADATA

}  // namespace

std::u16string FormatOriginLaterTime(base::Time time, base::Time now) {
  base::Time::Exploded local;
  time.LocalExplode(&local);
  base::Time::Exploded today;
  now.LocalExplode(&today);
  // A day is 23 to 25 hours long, so these land on the neighboring dates
  // whatever the clocks do.
  const base::Time midnight = now.LocalMidnight();
  base::Time::Exploded tomorrow;
  (midnight + base::Hours(36)).LocalExplode(&tomorrow);
  base::Time::Exploded yesterday;
  (midnight - base::Hours(12)).LocalExplode(&yesterday);

  if (IsSameDay(local, today)) {
    return u"Today " + base::TimeFormatTimeOfDay(time);
  }
  if (IsSameDay(local, tomorrow)) {
    return u"Tomorrow " + base::TimeFormatTimeOfDay(time);
  }
  if (IsSameDay(local, yesterday)) {
    return u"Yesterday " + base::TimeFormatTimeOfDay(time);
  }
  // Worded as origin_time_parser words the time when it is picked.
  namespace options = base::i18n::datetime_options;
  constexpr auto kMinutes =
      base::i18n::DateTimeFormatterOptions::TimePrecision::kMinute;
  const auto& formatter =
      base::i18n::IcuBridge::GetInstance().date_time_formatter();
  if (local.year == today.year) {
    return formatter.Format(
        time, options::MDET::Medium().with_time_precision(kMinutes));
  }
  return formatter.Format(
      time, options::YMDET::Medium().with_time_precision(kMinutes));
}

// One snoozed page. Its buttons show while the pointer is over the row, and
// while one of them has keyboard focus.
class OriginSideViewSnoozed::Row : public views::View {
  METADATA_HEADER(Row, views::View)

 public:
  Row(const OriginLaterService::SnoozedPage& page,
      base::RepeatingClosure wake_now,
      base::RepeatingClosure remove)
      : id_(page.id), space_id_(page.space_id), wake_time_(page.wake_time) {
    SetPreferredSize(gfx::Size(0, kRowHeight));
    // Stay highlighted while the pointer is over a button.
    SetNotifyEnterExitOnChild(true);

    auto* layout = SetLayoutManager(std::make_unique<views::BoxLayout>(
        views::BoxLayout::Orientation::kHorizontal,
        gfx::Insets::TLBR(0, 8, 0, 6), 8));
    layout->set_cross_axis_alignment(
        views::BoxLayout::CrossAxisAlignment::kCenter);

    AddChildView(std::make_unique<PageTile>(GetSiteInitial(page.url)));

    auto* text = AddChildView(std::make_unique<views::BoxLayoutView>());
    text->SetOrientation(views::BoxLayout::Orientation::kVertical);
    text->SetMainAxisAlignment(views::BoxLayout::MainAxisAlignment::kCenter);
    text->SetBetweenChildSpacing(3);
    layout->SetFlexForView(text, 1);

    const std::u16string title = GetPageTitle(page);
    title_ =
        text->AddChildView(MakeLabel(title, 13, gfx::Font::Weight::NORMAL));
    title_->SetHorizontalAlignment(gfx::ALIGN_LEFT);
    title_->SetElideBehavior(gfx::ELIDE_TAIL);

    status_ = text->AddChildView(
        MakeLabel(std::u16string(), 11, gfx::Font::Weight::NORMAL));
    status_->SetHorizontalAlignment(gfx::ALIGN_LEFT);
    status_->SetElideBehavior(gfx::ELIDE_TAIL);

    auto* actions = AddChildView(std::make_unique<views::BoxLayoutView>());
    actions->SetBetweenChildSpacing(2);
    const base::RepeatingClosure focus_changed =
        base::BindRepeating(&Row::UpdateActions, base::Unretained(this));
    wake_button_ = actions->AddChildView(
        std::make_unique<RowActionButton>(std::move(wake_now), focus_changed));
    wake_button_->SetAction(kLeoLaunchIcon, u"Open now", title);
    remove_button_ = actions->AddChildView(
        std::make_unique<RowActionButton>(std::move(remove), focus_changed));
    remove_button_->SetAction(kLeoCloseIcon, u"Remove", title);

    UpdateActions();
    UpdateColors();
  }

  const std::string& id() const { return id_; }
  const std::string& space_id() const { return space_id_; }

  void Update(base::Time now, const std::u16string& space_name) {
    std::u16string status = u"Wakes " + FormatOriginLaterTime(wake_time_, now);
    if (!space_name.empty()) {
      base::StrAppend(&status, {u" · ", space_name});
    }
    status_->SetText(status);
  }

  // The row's buttons, in focus order.
  std::array<views::View*, 2> GetActions() const {
    return {wake_button_.get(), remove_button_.get()};
  }

  void FocusAction(size_t index) {
    const std::array<views::View*, 2> actions = GetActions();
    actions[std::min(index, actions.size() - 1)]->RequestFocus();
  }

  // views::View:
  void OnMouseEntered(const ui::MouseEvent& event) override {
    SetHovered(true);
  }

  void OnMouseExited(const ui::MouseEvent& event) override {
    SetHovered(false);
  }

  // A row laid out, hidden or shown under a still pointer gets no enter or
  // exit, as when the row above it goes away.
  void OnBoundsChanged(const gfx::Rect& previous_bounds) override {
    views::View::OnBoundsChanged(previous_bounds);
    if (previous_bounds.IsEmpty()) {
      SetHovered(IsDrawn() && IsMouseHovered());
    }
  }

  void VisibilityChanged(views::View* starting_from, bool is_visible) override {
    views::View::VisibilityChanged(starting_from, is_visible);
    SetHovered(IsDrawn() && IsMouseHovered());
  }

  void OnThemeChanged() override {
    views::View::OnThemeChanged();
    UpdateColors();
  }

  void OnPaintBackground(gfx::Canvas* canvas) override {
    if (!hovered_) {
      return;
    }
    cc::PaintFlags fill;
    fill.setAntiAlias(true);
    fill.setColor(
        origin_chrome_style::HoverFill(origin_chrome_style::IsDark(this)));
    canvas->DrawRoundRect(gfx::RectF(GetLocalBounds()), kRowCornerRadius, fill);
  }

 private:
  void SetHovered(bool hovered) {
    if (hovered_ == hovered) {
      return;
    }
    hovered_ = hovered;
    UpdateActions();
    SchedulePaint();
  }

  void UpdateActions() {
    const bool revealed =
        hovered_ || wake_button_->HasFocus() || remove_button_->HasFocus();
    wake_button_->SetRevealed(revealed);
    remove_button_->SetRevealed(revealed);
  }

  void UpdateColors() {
    const bool dark = origin_chrome_style::IsDark(this);
    title_->SetEnabledColor(origin_chrome_style::PrimaryText(dark));
    status_->SetEnabledColor(origin_chrome_style::SecondaryText(dark));
  }

  const std::string id_;
  const std::string space_id_;
  const base::Time wake_time_;
  bool hovered_ = false;

  raw_ptr<views::Label> title_ = nullptr;
  raw_ptr<views::Label> status_ = nullptr;
  raw_ptr<RowActionButton> wake_button_ = nullptr;
  raw_ptr<RowActionButton> remove_button_ = nullptr;
};

BEGIN_METADATA(OriginSideViewSnoozed, Row)
END_METADATA

OriginSideViewSnoozed::OriginSideViewSnoozed(OriginLaterService* service,
                                             SpaceNameCallback space_name)
    : service_(service), space_name_(std::move(space_name)) {
  CHECK(service_);
  auto* layout = SetLayoutManager(std::make_unique<views::BoxLayout>(
      views::BoxLayout::Orientation::kVertical));

  auto list = std::make_unique<views::BoxLayoutView>();
  list->SetOrientation(views::BoxLayout::Orientation::kVertical);
  list->SetInsideBorderInsets(
      gfx::Insets::TLBR(6, kListInset, kListInset, kListInset));
  scroll_ = AddChildView(std::make_unique<views::ScrollView>());
  scroll_->SetBackgroundColor(std::nullopt);
  scroll_->SetHorizontalScrollBarMode(
      views::ScrollView::ScrollBarMode::kDisabled);
  scroll_->SetVerticalScrollBarMode(
      views::ScrollView::ScrollBarMode::kHiddenButEnabled);
  scroll_->SetDrawOverflowIndicator(false);
  list_ = scroll_->SetContents(std::move(list));
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
      MakeLabel(u"Nothing snoozed", 14, gfx::Font::Weight::SEMIBOLD));
  empty_body_ = empty_state->AddChildView(
      MakeLabel(u"Press l on a page to put it away until later.", 12,
                gfx::Font::Weight::NORMAL));
  empty_body_->SetMultiLine(true);
  empty_body_->SetMaximumWidth(260);
  empty_body_->SetHorizontalAlignment(gfx::ALIGN_CENTER);
  empty_state_ = AddChildView(std::move(empty_state));
  layout->SetFlexForView(empty_state_, 1);

  later_observation_.Observe(service_);
  Rebuild();
  UpdateColors();
}

OriginSideViewSnoozed::~OriginSideViewSnoozed() = default;

void OriginSideViewSnoozed::OnThemeChanged() {
  views::View::OnThemeChanged();
  UpdateColors();
}

void OriginSideViewSnoozed::AddedToWidget() {
  views::View::AddedToWidget();
  UpdateRefreshTimer();
}

void OriginSideViewSnoozed::RemovedFromWidget() {
  views::View::RemovedFromWidget();
  refresh_timer_.Stop();
}

void OriginSideViewSnoozed::VisibilityChanged(views::View* starting_from,
                                              bool is_visible) {
  views::View::VisibilityChanged(starting_from, is_visible);
  UpdateRefreshTimer();
}

void OriginSideViewSnoozed::OnOriginLaterChanged() {
  needs_rebuild_ = true;
  ScheduleUpdate();
}

base::Time OriginSideViewSnoozed::Now() const {
  return now_for_testing_.value_or(base::Time::Now());
}

std::u16string OriginSideViewSnoozed::GetSpaceName(
    const std::string& space_id) const {
  if (space_id.empty() || space_name_.is_null()) {
    return std::u16string();
  }
  return space_name_.Run(space_id);
}

void OriginSideViewSnoozed::WakeNow(const std::string& id) {
  service_->WakeNow(id);
}

void OriginSideViewSnoozed::Remove(const std::string& id) {
  service_->RemoveSnoozedPage(id);
}

void OriginSideViewSnoozed::ScheduleUpdate() {
  if (update_scheduled_) {
    return;
  }
  update_scheduled_ = true;
  base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
      FROM_HERE, base::BindOnce(&OriginSideViewSnoozed::ApplyUpdates,
                                weak_factory_.GetWeakPtr()));
}

void OriginSideViewSnoozed::ApplyUpdates() {
  update_scheduled_ = false;
  if (needs_rebuild_) {
    Rebuild();
    return;
  }
  if (!needs_refresh_) {
    return;
  }
  needs_refresh_ = false;
  const base::Time now = Now();
  for (Row* row : rows_) {
    row->Update(now, GetSpaceName(row->space_id()));
  }
}

void OriginSideViewSnoozed::Rebuild() {
  needs_rebuild_ = false;
  needs_refresh_ = false;
  const std::optional<FocusedAction> focus = GetFocusedAction();
  rows_.clear();
  list_->RemoveAllChildViews();

  const base::Time now = Now();
  for (const OriginLaterService::SnoozedPage& page :
       service_->snoozed_pages()) {
    auto* row = list_->AddChildView(std::make_unique<Row>(
        page,
        base::BindRepeating(&OriginSideViewSnoozed::WakeNow,
                            base::Unretained(this), page.id),
        base::BindRepeating(&OriginSideViewSnoozed::Remove,
                            base::Unretained(this), page.id)));
    row->Update(now, GetSpaceName(page.space_id));
    rows_.push_back(row);
  }

  const bool empty = rows_.empty();
  empty_state_->SetVisible(empty);
  scroll_->SetVisible(!empty);
  if (focus) {
    RestoreFocus(*focus);
  }
}

void OriginSideViewSnoozed::RefreshRows() {
  needs_refresh_ = true;
  ScheduleUpdate();
}

void OriginSideViewSnoozed::UpdateRefreshTimer() {
  const bool drawn = IsDrawn();
  if (drawn == refresh_timer_.IsRunning()) {
    return;
  }
  if (!drawn) {
    refresh_timer_.Stop();
    return;
  }
  // The times went stale while the list was hidden.
  RefreshRows();
  refresh_timer_.Start(FROM_HERE, kRefreshInterval,
                       base::BindRepeating(&OriginSideViewSnoozed::RefreshRows,
                                           base::Unretained(this)));
}

void OriginSideViewSnoozed::UpdateColors() {
  const bool dark = origin_chrome_style::IsDark(this);
  empty_icon_->SetImage(ui::ImageModel::FromVectorIcon(
      kLeoSleepTimerIcon, origin_chrome_style::MutedText(dark), 28));
  empty_title_->SetEnabledColor(origin_chrome_style::PrimaryText(dark));
  empty_body_->SetEnabledColor(origin_chrome_style::SecondaryText(dark));
}

std::optional<OriginSideViewSnoozed::FocusedAction>
OriginSideViewSnoozed::GetFocusedAction() {
  views::FocusManager* focus_manager = GetFocusManager();
  views::View* focused =
      focus_manager ? focus_manager->GetFocusedView() : nullptr;
  if (!focused || !list_->Contains(focused)) {
    return std::nullopt;
  }
  for (size_t row = 0; row < rows_.size(); ++row) {
    const std::array<views::View*, 2> actions = rows_[row]->GetActions();
    for (size_t action = 0; action < actions.size(); ++action) {
      if (actions[action] == focused) {
        return FocusedAction{rows_[row]->id(), row, action};
      }
    }
  }
  return std::nullopt;
}

void OriginSideViewSnoozed::RestoreFocus(const FocusedAction& focus) {
  if (rows_.empty()) {
    return;
  }
  // The same page if it is still listed, otherwise the one now in its place.
  auto it = std::ranges::find_if(
      rows_, [&](const Row* row) { return row->id() == focus.id; });
  Row* row = it != rows_.end()
                 ? it->get()
                 : rows_[std::min(focus.row, rows_.size() - 1)].get();
  row->FocusAction(focus.action);
}

BEGIN_METADATA(OriginSideViewSnoozed)
END_METADATA
