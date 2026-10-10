// Copyright (c) 2026 The Brave Authors. All rights reserved.
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this file,
// You can obtain one at https://mozilla.org/MPL/2.0/.

#include "brave/browser/ui/views/frame/origin_side_view_reminders.h"

#include <algorithm>
#include <array>
#include <memory>
#include <utility>

#include "base/check.h"
#include "base/functional/bind.h"
#include "base/i18n/case_conversion.h"
#include "base/location.h"
#include "base/strings/strcat.h"
#include "base/strings/utf_string_conversions.h"
#include "base/task/sequenced_task_runner.h"
#include "brave/browser/ui/views/frame/origin_chrome_style.h"
#include "brave/browser/ui/views/frame/origin_side_view_snoozed.h"
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
#include "ui/views/animation/ink_drop.h"
#include "ui/views/border.h"
#include "ui/views/controls/button/image_button.h"
#include "ui/views/controls/highlight_path_generator.h"
#include "ui/views/controls/image_view.h"
#include "ui/views/controls/label.h"
#include "ui/views/controls/scroll_view.h"
#include "ui/views/focus/focus_manager.h"
#include "ui/views/layout/box_layout.h"
#include "ui/views/layout/box_layout_view.h"

namespace {

using Reminder = OriginLaterService::Reminder;
using Section = OriginSideViewReminders::Section;

constexpr int kListInset = 8;
// Lines section titles up with the check marks below them.
constexpr int kSectionInset = 10;
constexpr int kRowHeight = 48;
constexpr int kRowCornerRadius = 8;
constexpr int kActionButtonSize = 26;
constexpr int kActionIconSize = 16;
constexpr int kCheckIconSize = 18;
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

Section GetSection(bool done, base::Time due_time, base::Time now) {
  if (done) {
    return Section::kDone;
  }
  return due_time <= now ? Section::kDue : Section::kUpcoming;
}

std::u16string GetReminderTitle(const Reminder& reminder) {
  if (!reminder.title.empty()) {
    return reminder.title;
  }
  const std::string_view host = reminder.url.host();
  return host.empty() ? u"Reminder" : base::UTF8ToUTF16(host);
}

// A row's icon button. The row is already highlighted while the pointer is
// over it, so this hover takes the stronger selected fill. Until revealed it
// paints nothing, but keeps its place and can still take focus.
class RowActionButton : public views::ImageButton {
  METADATA_HEADER(RowActionButton, views::ImageButton)

 public:
  // `focus_changed` runs whenever the button gains or loses focus.
  RowActionButton(PressedCallback callback,
                  base::RepeatingClosure focus_changed,
                  int icon_size = kActionIconSize)
      : views::ImageButton(std::move(callback)),
        focus_changed_(std::move(focus_changed)),
        icon_size_(icon_size) {
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
                      icon_size_));
  }

  const base::RepeatingClosure focus_changed_;
  const int icon_size_;
  raw_ptr<const gfx::VectorIcon> icon_ = nullptr;
  bool revealed_ = true;
};

BEGIN_METADATA(RowActionButton)
END_METADATA

}  // namespace

// One reminder. The check and page buttons always show; Delete shows while
// the pointer is over the row, and while one of its buttons has keyboard
// focus.
class OriginSideViewReminders::Row : public views::View {
  METADATA_HEADER(Row, views::View)

 public:
  Row(const Reminder& reminder,
      Section section,
      base::RepeatingClosure toggle_done,
      base::RepeatingClosure open_page,
      base::RepeatingClosure remove)
      : id_(reminder.id),
        section_(section),
        due_time_(reminder.due_time),
        done_(reminder.done) {
    SetPreferredSize(gfx::Size(0, kRowHeight));
    // Stay highlighted while the pointer is over a button.
    SetNotifyEnterExitOnChild(true);

    auto* layout = SetLayoutManager(std::make_unique<views::BoxLayout>(
        views::BoxLayout::Orientation::kHorizontal,
        gfx::Insets::TLBR(0, 6, 0, 6), 8));
    layout->set_cross_axis_alignment(
        views::BoxLayout::CrossAxisAlignment::kCenter);

    const std::u16string title = GetReminderTitle(reminder);
    const base::RepeatingClosure focus_changed =
        base::BindRepeating(&Row::UpdateActions, base::Unretained(this));
    check_button_ = AddChildView(std::make_unique<RowActionButton>(
        std::move(toggle_done), focus_changed, kCheckIconSize));
    check_button_->SetAction(
        done_ ? kLeoCheckCircleFilledIcon : kLeoCheckCircleOutlineIcon,
        done_ ? u"Mark as not done" : u"Mark as done", title);

    auto* text = AddChildView(std::make_unique<views::BoxLayoutView>());
    text->SetOrientation(views::BoxLayout::Orientation::kVertical);
    text->SetMainAxisAlignment(views::BoxLayout::MainAxisAlignment::kCenter);
    text->SetBetweenChildSpacing(3);
    layout->SetFlexForView(text, 1);

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
    open_button_ = actions->AddChildView(
        std::make_unique<RowActionButton>(std::move(open_page), focus_changed));
    open_button_->SetAction(kLeoLaunchIcon, u"Open page", title);
    open_button_->SetVisible(reminder.url.is_valid());
    remove_button_ = actions->AddChildView(
        std::make_unique<RowActionButton>(std::move(remove), focus_changed));
    remove_button_->SetAction(kLeoCloseIcon, u"Delete", title);

    UpdateActions();
    UpdateColors();
  }

  const std::string& id() const { return id_; }
  Section section() const { return section_; }

  // Whether the reminder now belongs in another section.
  bool IsStale(base::Time now) const {
    return GetSection(done_, due_time_, now) != section_;
  }

  void Update(base::Time now) {
    status_->SetText(FormatOriginLaterTime(due_time_, now));
  }

  // The row's buttons, in focus order.
  std::array<views::View*, 3> GetActions() const {
    return {check_button_.get(), open_button_.get(), remove_button_.get()};
  }

  void FocusAction(size_t index) {
    const std::array<views::View*, 3> actions = GetActions();
    views::View* action = actions[std::min(index, actions.size() - 1)];
    // Not every reminder has a page; every one has a check button.
    (action->GetVisible() ? action : actions[0])->RequestFocus();
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
    remove_button_->SetRevealed(hovered_ || check_button_->HasFocus() ||
                                open_button_->HasFocus() ||
                                remove_button_->HasFocus());
  }

  void UpdateColors() {
    const bool dark = origin_chrome_style::IsDark(this);
    title_->SetEnabledColor(done_ ? origin_chrome_style::MutedText(dark)
                                  : origin_chrome_style::PrimaryText(dark));
    SkColor status_color = origin_chrome_style::SecondaryText(dark);
    if (section_ == Section::kDue) {
      status_color = origin_chrome_style::kAccent;
    } else if (done_) {
      status_color = origin_chrome_style::MutedText(dark);
    }
    status_->SetEnabledColor(status_color);
  }

  const std::string id_;
  const Section section_;
  const base::Time due_time_;
  const bool done_;
  bool hovered_ = false;

  raw_ptr<RowActionButton> check_button_ = nullptr;
  raw_ptr<views::Label> title_ = nullptr;
  raw_ptr<views::Label> status_ = nullptr;
  raw_ptr<RowActionButton> open_button_ = nullptr;
  raw_ptr<RowActionButton> remove_button_ = nullptr;
};

BEGIN_METADATA(OriginSideViewReminders, Row)
END_METADATA

OriginSideViewReminders::OriginSideViewReminders(OriginLaterService* service,
                                                 OpenUrlCallback open_url)
    : service_(service), open_url_(std::move(open_url)) {
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
      MakeLabel(u"No reminders", 14, gfx::Font::Weight::SEMIBOLD));
  empty_body_ = empty_state->AddChildView(
      MakeLabel(u"Press a on a page to be reminded about it.", 12,
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

OriginSideViewReminders::~OriginSideViewReminders() = default;

size_t OriginSideViewReminders::GetSectionRowCountForTesting(
    Section section) const {
  return static_cast<size_t>(std::ranges::count_if(
      rows_, [&](const Row* row) { return row->section() == section; }));
}

void OriginSideViewReminders::OnThemeChanged() {
  views::View::OnThemeChanged();
  UpdateColors();
}

void OriginSideViewReminders::AddedToWidget() {
  views::View::AddedToWidget();
  UpdateRefreshTimer();
}

void OriginSideViewReminders::RemovedFromWidget() {
  views::View::RemovedFromWidget();
  refresh_timer_.Stop();
}

void OriginSideViewReminders::VisibilityChanged(views::View* starting_from,
                                                bool is_visible) {
  views::View::VisibilityChanged(starting_from, is_visible);
  UpdateRefreshTimer();
}

void OriginSideViewReminders::OnOriginLaterChanged() {
  needs_rebuild_ = true;
  ScheduleUpdate();
}

base::Time OriginSideViewReminders::Now() const {
  return now_for_testing_.value_or(base::Time::Now());
}

void OriginSideViewReminders::SetDone(const std::string& id, bool done) {
  service_->SetReminderDone(id, done);
}

void OriginSideViewReminders::OpenPage(const GURL& url) {
  if (!open_url_.is_null()) {
    open_url_.Run(url);
  }
}

void OriginSideViewReminders::Remove(const std::string& id) {
  service_->RemoveReminder(id);
}

void OriginSideViewReminders::ScheduleUpdate() {
  if (update_scheduled_) {
    return;
  }
  update_scheduled_ = true;
  base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
      FROM_HERE, base::BindOnce(&OriginSideViewReminders::ApplyUpdates,
                                weak_factory_.GetWeakPtr()));
}

void OriginSideViewReminders::ApplyUpdates() {
  update_scheduled_ = false;
  const base::Time now = Now();
  // A reminder that came due moves up to Due, even before the service's own
  // timer says so.
  if (needs_refresh_ && std::ranges::any_of(rows_, [&](const Row* row) {
        return row->IsStale(now);
      })) {
    needs_rebuild_ = true;
  }
  if (needs_rebuild_) {
    Rebuild();
    return;
  }
  if (!needs_refresh_) {
    return;
  }
  needs_refresh_ = false;
  for (Row* row : rows_) {
    row->Update(now);
  }
}

void OriginSideViewReminders::Rebuild() {
  needs_rebuild_ = false;
  needs_refresh_ = false;
  const std::optional<FocusedAction> focus = GetFocusedAction();
  rows_.clear();
  section_titles_.clear();
  list_->RemoveAllChildViews();

  const base::Time now = Now();
  std::vector<const Reminder*> due;
  std::vector<const Reminder*> upcoming;
  std::vector<const Reminder*> done;
  for (const Reminder& reminder : service_->reminders()) {
    switch (GetSection(reminder.done, reminder.due_time, now)) {
      case Section::kDue:
        due.push_back(&reminder);
        break;
      case Section::kUpcoming:
        upcoming.push_back(&reminder);
        break;
      case Section::kDone:
        done.push_back(&reminder);
        break;
    }
  }
  // Open reminders soonest first; done ones most recently due first, and only
  // the last few of those.
  const auto soonest_first = [](const Reminder* a, const Reminder* b) {
    return a->due_time < b->due_time;
  };
  std::ranges::stable_sort(due, soonest_first);
  std::ranges::stable_sort(upcoming, soonest_first);
  std::ranges::stable_sort(done, [](const Reminder* a, const Reminder* b) {
    return a->due_time > b->due_time;
  });
  if (done.size() > kMaxDoneRows) {
    done.resize(kMaxDoneRows);
  }

  AddSection(u"Due", Section::kDue, due, now);
  AddSection(u"Upcoming", Section::kUpcoming, upcoming, now);
  AddSection(u"Done", Section::kDone, done, now);

  const bool empty = rows_.empty();
  empty_state_->SetVisible(empty);
  scroll_->SetVisible(!empty);
  if (focus) {
    RestoreFocus(*focus);
  }
}

void OriginSideViewReminders::AddSection(
    std::u16string_view title,
    Section section,
    const std::vector<const Reminder*>& reminders,
    base::Time now) {
  if (reminders.empty()) {
    return;
  }
  auto* section_title = list_->AddChildView(
      MakeLabel(base::i18n::ToUpper(title), 10, gfx::Font::Weight::BOLD));
  section_title->SetHorizontalAlignment(gfx::ALIGN_LEFT);
  section_title->SetBorder(views::CreateEmptyBorder(gfx::Insets::TLBR(
      section_titles_.empty() ? 8 : 14, kSectionInset, 4, kSectionInset)));
  section_title->SetEnabledColor(
      origin_chrome_style::MutedText(origin_chrome_style::IsDark(this)));
  section_titles_.push_back(section_title);

  for (const Reminder* reminder : reminders) {
    auto* row = list_->AddChildView(std::make_unique<Row>(
        *reminder, section,
        base::BindRepeating(&OriginSideViewReminders::SetDone,
                            base::Unretained(this), reminder->id,
                            !reminder->done),
        base::BindRepeating(&OriginSideViewReminders::OpenPage,
                            base::Unretained(this), reminder->url),
        base::BindRepeating(&OriginSideViewReminders::Remove,
                            base::Unretained(this), reminder->id)));
    row->Update(now);
    rows_.push_back(row);
  }
}

void OriginSideViewReminders::RefreshRows() {
  needs_refresh_ = true;
  ScheduleUpdate();
}

void OriginSideViewReminders::UpdateRefreshTimer() {
  const bool drawn = IsDrawn();
  if (drawn == refresh_timer_.IsRunning()) {
    return;
  }
  if (!drawn) {
    refresh_timer_.Stop();
    return;
  }
  // The times, and maybe the sections, went stale while the list was hidden.
  RefreshRows();
  refresh_timer_.Start(
      FROM_HERE, kRefreshInterval,
      base::BindRepeating(&OriginSideViewReminders::RefreshRows,
                          base::Unretained(this)));
}

void OriginSideViewReminders::UpdateColors() {
  const bool dark = origin_chrome_style::IsDark(this);
  empty_icon_->SetImage(ui::ImageModel::FromVectorIcon(
      kLeoAlarmIcon, origin_chrome_style::MutedText(dark), 28));
  empty_title_->SetEnabledColor(origin_chrome_style::PrimaryText(dark));
  empty_body_->SetEnabledColor(origin_chrome_style::SecondaryText(dark));
  for (views::Label* section_title : section_titles_) {
    section_title->SetEnabledColor(origin_chrome_style::MutedText(dark));
  }
}

std::optional<OriginSideViewReminders::FocusedAction>
OriginSideViewReminders::GetFocusedAction() {
  views::FocusManager* focus_manager = GetFocusManager();
  views::View* focused =
      focus_manager ? focus_manager->GetFocusedView() : nullptr;
  if (!focused || !list_->Contains(focused)) {
    return std::nullopt;
  }
  for (size_t row = 0; row < rows_.size(); ++row) {
    const std::array<views::View*, 3> actions = rows_[row]->GetActions();
    for (size_t action = 0; action < actions.size(); ++action) {
      if (actions[action] == focused) {
        return FocusedAction{rows_[row]->id(), row, action};
      }
    }
  }
  return std::nullopt;
}

void OriginSideViewReminders::RestoreFocus(const FocusedAction& focus) {
  if (rows_.empty()) {
    return;
  }
  // The same reminder if it is still listed, say after moving to Done;
  // otherwise the one now in its place.
  auto it = std::ranges::find_if(
      rows_, [&](const Row* row) { return row->id() == focus.id; });
  Row* row = it != rows_.end()
                 ? it->get()
                 : rows_[std::min(focus.row, rows_.size() - 1)].get();
  row->FocusAction(focus.action);
}

BEGIN_METADATA(OriginSideViewReminders)
END_METADATA
