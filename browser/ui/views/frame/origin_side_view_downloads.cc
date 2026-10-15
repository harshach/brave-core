// Copyright (c) 2026 The Brave Authors. All rights reserved.
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this file,
// You can obtain one at https://mozilla.org/MPL/2.0/.

#include "brave/browser/ui/views/frame/origin_side_view_downloads.h"

#include <algorithm>
#include <optional>
#include <string>
#include <utility>

#include "base/check.h"
#include "base/functional/bind.h"
#include "base/location.h"
#include "base/task/sequenced_task_runner.h"
#include "base/time/time.h"
#include "brave/browser/ui/views/frame/origin_chrome_style.h"
#include "brave/components/vector_icons/vector_icons.h"
#include "build/build_config.h"
#include "cc/paint/paint_flags.h"
#include "chrome/browser/download/download_item_model.h"
#include "chrome/browser/download/download_ui_model.h"
#include "chrome/browser/profiles/profile.h"
#include "components/download/public/common/download_item.h"
#include "content/public/browser/download_manager.h"
#include "ui/base/metadata/metadata_impl_macros.h"
#include "ui/base/models/image_model.h"
#include "ui/gfx/canvas.h"
#include "ui/gfx/geometry/insets.h"
#include "ui/gfx/geometry/rect_f.h"
#include "ui/gfx/geometry/rounded_corners_f.h"
#include "ui/gfx/geometry/size.h"
#include "ui/views/animation/ink_drop.h"
#include "ui/views/border.h"
#include "ui/views/controls/button/button.h"
#include "ui/views/controls/button/image_button.h"
#include "ui/views/controls/button/label_button.h"
#include "ui/views/controls/highlight_path_generator.h"
#include "ui/views/controls/image_view.h"
#include "ui/views/controls/label.h"
#include "ui/views/controls/progress_bar.h"
#include "ui/views/controls/scroll_view.h"
#include "ui/views/layout/box_layout.h"
#include "ui/views/layout/box_layout_view.h"

namespace {

constexpr int kListInset = 8;
constexpr int kRowHeight = 52;
constexpr int kRowCornerRadius = 8;
constexpr int kFileIconSize = 20;
constexpr int kActionButtonSize = 26;
constexpr int kActionIconSize = 16;
constexpr int kActionCornerRadius = 6;
constexpr int kFooterHeight = 36;
constexpr base::TimeDelta kRefreshInterval = base::Minutes(1);

#if BUILDFLAG(IS_MAC)
constexpr char16_t kShowInFolderName[] = u"Show in Finder";
#else
constexpr char16_t kShowInFolderName[] = u"Show in folder";
#endif

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

// The downloads chrome://downloads lists: nothing hidden, nothing the user
// removed from the list there, and nothing still waiting for its name.
bool ShouldShow(download::DownloadItem* item) {
  if (item->IsTransient() || item->IsTemporary() ||
      item->GetFileNameToReportUser().empty() ||
      item->GetTargetFilePath().empty()) {
    return false;
  }
  return DownloadItemModel(item).ShouldShowInUi();
}

// Waiting for the user to keep or discard it. That choice, and the warning
// that explains it, belong to chrome://downloads.
bool NeedsReview(const download::DownloadItem* item) {
  return item->GetState() == download::DownloadItem::IN_PROGRESS &&
         (item->IsDangerous() || item->IsInsecure());
}

// A row's trailing icon button. The row is already highlighted while the
// pointer is over it, so this hover takes the stronger selected fill.
class RowActionButton : public views::ImageButton {
  METADATA_HEADER(RowActionButton, views::ImageButton)

 public:
  explicit RowActionButton(PressedCallback callback)
      : views::ImageButton(std::move(callback)) {
    SetPreferredSize(gfx::Size(kActionButtonSize, kActionButtonSize));
    SetImageHorizontalAlignment(ALIGN_CENTER);
    SetImageVerticalAlignment(ALIGN_MIDDLE);
    views::InkDrop::Get(this)->SetMode(views::InkDropHost::InkDropMode::OFF);
    views::InstallRoundRectHighlightPathGenerator(this, gfx::Insets(),
                                                  kActionCornerRadius);
  }

  void SetAction(const gfx::VectorIcon& icon, const std::u16string& name) {
    icon_ = &icon;
    SetAccessibleName(name);
    SetTooltipText(name);
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

  raw_ptr<const gfx::VectorIcon> icon_ = nullptr;
};

BEGIN_METADATA(RowActionButton)
END_METADATA

// The footer's text button, in the side view's type.
class FooterButton : public views::LabelButton {
  METADATA_HEADER(FooterButton, views::LabelButton)

 public:
  FooterButton(PressedCallback callback, const std::u16string& text)
      : views::LabelButton(std::move(callback), text) {
    label()->SetFontList(
        origin_chrome_style::Font(12, gfx::Font::Weight::SEMIBOLD));
    SetBorder(views::CreateEmptyBorder(gfx::Insets::VH(4, 10)));
    views::InkDrop::Get(this)->SetMode(views::InkDropHost::InkDropMode::OFF);
  }
};

BEGIN_METADATA(FooterButton)
END_METADATA

}  // namespace

// One download. The whole row is a button for its main action: open a
// finished file, or review one that is waiting on a safety decision.
class OriginSideViewDownloads::Row : public views::Button {
  METADATA_HEADER(Row, views::Button)

 public:
  Row(download::DownloadItem* item, base::RepeatingClosure open_all_downloads)
      : views::Button(
            base::BindRepeating(&Row::OnPressed, base::Unretained(this))),
        item_(item),
        start_time_(item->GetStartTime()),
        open_all_downloads_(std::move(open_all_downloads)) {
    SetPreferredSize(gfx::Size(0, kRowHeight));
    // Stay highlighted while the pointer is over a trailing button.
    SetNotifyEnterExitOnChild(true);
    views::InkDrop::Get(this)->SetMode(views::InkDropHost::InkDropMode::OFF);
    views::InstallRoundRectHighlightPathGenerator(this, gfx::Insets(),
                                                  kRowCornerRadius);

    auto* layout = SetLayoutManager(std::make_unique<views::BoxLayout>(
        views::BoxLayout::Orientation::kHorizontal,
        gfx::Insets::TLBR(0, 10, 0, 6), 10));
    layout->set_cross_axis_alignment(
        views::BoxLayout::CrossAxisAlignment::kCenter);

    icon_ = AddChildView(std::make_unique<views::ImageView>());
    icon_->SetImageSize(gfx::Size(kFileIconSize, kFileIconSize));
    icon_->SetCanProcessEventsWithinSubtree(false);

    auto* text = AddChildView(std::make_unique<views::BoxLayoutView>());
    text->SetOrientation(views::BoxLayout::Orientation::kVertical);
    text->SetMainAxisAlignment(views::BoxLayout::MainAxisAlignment::kCenter);
    text->SetBetweenChildSpacing(3);
    text->SetCanProcessEventsWithinSubtree(false);
    layout->SetFlexForView(text, 1);

    name_ = text->AddChildView(
        MakeLabel(std::u16string(), 13, gfx::Font::Weight::NORMAL));
    name_->SetHorizontalAlignment(gfx::ALIGN_LEFT);
    // The extension says more than the middle of a long name.
    name_->SetElideBehavior(gfx::ELIDE_MIDDLE);

    status_ = text->AddChildView(
        MakeLabel(std::u16string(), 11, gfx::Font::Weight::NORMAL));
    status_->SetHorizontalAlignment(gfx::ALIGN_LEFT);
    status_->SetElideBehavior(gfx::ELIDE_TAIL);

    progress_ = text->AddChildView(std::make_unique<views::ProgressBar>());
    progress_->SetPreferredHeight(2);
    progress_->SetPreferredCornerRadii(gfx::RoundedCornersF(1));

    auto* actions = AddChildView(std::make_unique<views::BoxLayoutView>());
    actions->SetBetweenChildSpacing(2);
    primary_button_ = actions->AddChildView(std::make_unique<RowActionButton>(
        base::BindRepeating(&Row::OnPrimaryAction, base::Unretained(this))));
    cancel_button_ = actions->AddChildView(std::make_unique<RowActionButton>(
        base::BindRepeating(&Row::OnCancel, base::Unretained(this))));
    cancel_button_->SetAction(kLeoCloseIcon, u"Cancel");

    Update();
  }

  download::DownloadItem* item() const { return item_; }
  base::Time start_time() const { return start_time_; }
  bool needs_update() const { return needs_update_; }
  void set_needs_update() { needs_update_ = true; }

  // The download is gone; the row only waits for the list to be rebuilt.
  void ClearItem() { item_ = nullptr; }

  void Update() {
    needs_update_ = false;
    if (!item_) {
      return;
    }
    // The download bubble's wording: sizes, time left, and why a download
    // stopped, all localized.
    DownloadItemModel model(
        item_.get(),
        std::make_unique<DownloadUIModel::BubbleStatusTextBuilder>());
    const bool dark = origin_chrome_style::IsDark(this);
    const download::DownloadItem::DownloadState state = item_->GetState();
    const bool review = NeedsReview(item_);
    const bool running =
        state == download::DownloadItem::IN_PROGRESS && !review;
    const bool failed = state == download::DownloadItem::INTERRUPTED;
    const bool gone = state == download::DownloadItem::CANCELLED ||
                      (state == download::DownloadItem::COMPLETE &&
                       item_->GetFileExternallyRemoved());

    const std::u16string name =
        item_->GetFileNameToReportUser().LossyDisplayName();
    const std::u16string status = model.GetStatusText();
    name_->SetText(name);
    name_->SetEnabledColor(gone ? origin_chrome_style::SecondaryText(dark)
                                : origin_chrome_style::PrimaryText(dark));
    status_->SetText(status);
    status_->SetEnabledColor(origin_chrome_style::SecondaryText(dark));
    status_->SetVisible(!status.empty());
    SetAccessibleName(status.empty() ? name : name + u", " + status);
    SetTooltipText(model.GetTooltipText());

    const bool attention = review || failed;
    SkColor icon_color = origin_chrome_style::SecondaryText(dark);
    if (attention) {
      icon_color = origin_chrome_style::kAccent;
    } else if (gone) {
      icon_color = origin_chrome_style::MutedText(dark);
    }
    icon_->SetImage(ui::ImageModel::FromVectorIcon(
        attention ? kLeoWarningTriangleOutlineIcon : kLeoFileDownloadIcon,
        icon_color, kFileIconSize));

    progress_->SetVisible(running);
    if (running) {
      const int percent = item_->PercentComplete();
      const bool paused = item_->IsPaused();
      double value = percent / 100.0;
      if (percent < 0) {
        // An unknown size animates, but only on screen (the animation never
        // stops by itself) and only while bytes are moving.
        value = !paused && IsDrawn() ? -1.0 : 0.0;
      }
      progress_->SetValue(value);
      progress_->SetForegroundColor(paused
                                        ? origin_chrome_style::MutedText(dark)
                                        : origin_chrome_style::kAccent);
      progress_->SetBackgroundColor(origin_chrome_style::SelectedFill(dark));
    }

    action_ = GetAction(item_);
    primary_button_->SetVisible(action_ != Action::kNone);
    switch (action_) {
      case Action::kPause:
        primary_button_->SetAction(kLeoPauseFilledIcon, u"Pause");
        break;
      case Action::kResume:
        primary_button_->SetAction(kLeoPlayFilledIcon, u"Resume");
        break;
      case Action::kRetry:
        primary_button_->SetAction(kLeoReloadIcon, u"Retry");
        break;
      case Action::kShowInFolder:
        primary_button_->SetAction(kLeoFolderIcon, kShowInFolderName);
        break;
      case Action::kNone:
        break;
    }
    cancel_button_->SetVisible(running);
  }

  // views::Button:
  void OnThemeChanged() override {
    views::Button::OnThemeChanged();
    Update();
  }

  void OnPaintBackground(gfx::Canvas* canvas) override {
    if (GetState() != STATE_HOVERED && GetState() != STATE_PRESSED) {
      return;
    }
    cc::PaintFlags fill;
    fill.setAntiAlias(true);
    fill.setColor(
        origin_chrome_style::HoverFill(origin_chrome_style::IsDark(this)));
    canvas->DrawRoundRect(gfx::RectF(GetLocalBounds()), kRowCornerRadius, fill);
  }

 private:
  enum class Action {
    kNone,
    kPause,
    kResume,
    kRetry,
    kShowInFolder,
  };

  static Action GetAction(download::DownloadItem* item) {
    if (NeedsReview(item)) {
      return Action::kNone;
    }
    const download::DownloadItem::DownloadState state = item->GetState();
    if (state == download::DownloadItem::IN_PROGRESS) {
      return item->IsPaused() ? Action::kResume : Action::kPause;
    }
    if (state == download::DownloadItem::COMPLETE && item->CanShowInFolder()) {
      return Action::kShowInFolder;
    }
    if (state == download::DownloadItem::INTERRUPTED && item->CanResume()) {
      return Action::kRetry;
    }
    return Action::kNone;
  }

  void OnPressed() {
    if (!item_) {
      return;
    }
    if (NeedsReview(item_)) {
      open_all_downloads_.Run();
      return;
    }
    if (item_->GetState() == download::DownloadItem::COMPLETE &&
        item_->CanOpenDownload()) {
      item_->OpenDownload();
    }
  }

  void OnPrimaryAction() {
    // Act only on what the button showed: the download may have moved on,
    // with this row's refresh still queued.
    if (!item_ || GetAction(item_) != action_) {
      return;
    }
    switch (action_) {
      case Action::kPause:
        item_->Pause();
        break;
      case Action::kResume:
      case Action::kRetry:
        item_->Resume(/*user_resume=*/true);
        break;
      case Action::kShowInFolder:
        item_->ShowDownloadInShell();
        break;
      case Action::kNone:
        break;
    }
  }

  void OnCancel() {
    if (item_ && item_->GetState() == download::DownloadItem::IN_PROGRESS) {
      item_->Cancel(/*user_cancel=*/true);
    }
  }

  raw_ptr<download::DownloadItem> item_;
  const base::Time start_time_;
  const base::RepeatingClosure open_all_downloads_;
  Action action_ = Action::kNone;
  bool needs_update_ = false;

  raw_ptr<views::ImageView> icon_ = nullptr;
  raw_ptr<views::Label> name_ = nullptr;
  raw_ptr<views::Label> status_ = nullptr;
  raw_ptr<views::ProgressBar> progress_ = nullptr;
  raw_ptr<RowActionButton> primary_button_ = nullptr;
  raw_ptr<RowActionButton> cancel_button_ = nullptr;
};

BEGIN_METADATA(OriginSideViewDownloads, Row)
END_METADATA

OriginSideViewDownloads::OriginSideViewDownloads(
    Profile* profile,
    base::RepeatingClosure open_all_downloads)
    : open_all_downloads_(std::move(open_all_downloads)) {
  CHECK(profile);
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
      MakeLabel(u"No downloads yet", 14, gfx::Font::Weight::SEMIBOLD));
  empty_body_ = empty_state->AddChildView(MakeLabel(
      u"Files you download show up here.", 12, gfx::Font::Weight::NORMAL));
  empty_body_->SetMultiLine(true);
  empty_body_->SetMaximumWidth(260);
  empty_body_->SetHorizontalAlignment(gfx::ALIGN_CENTER);
  empty_state_ = AddChildView(std::move(empty_state));
  layout->SetFlexForView(empty_state_, 1);

  auto footer = std::make_unique<views::BoxLayoutView>();
  footer->SetMainAxisAlignment(views::BoxLayout::MainAxisAlignment::kCenter);
  footer->SetCrossAxisAlignment(views::BoxLayout::CrossAxisAlignment::kCenter);
  footer->SetPreferredSize(gfx::Size(0, kFooterHeight));
  open_all_button_ = footer->AddChildView(std::make_unique<FooterButton>(
      open_all_downloads_, u"Open all downloads"));
  footer_ = AddChildView(std::move(footer));

  // Only this profile's manager: an incognito window lists its own downloads,
  // as the download bubble does.
  notifier_ = std::make_unique<download::AllDownloadItemNotifier>(
      profile->GetDownloadManager(), this);
  Rebuild();
  UpdateColors();
}

OriginSideViewDownloads::~OriginSideViewDownloads() = default;

void OriginSideViewDownloads::OnThemeChanged() {
  views::View::OnThemeChanged();
  UpdateColors();
}

void OriginSideViewDownloads::AddedToWidget() {
  views::View::AddedToWidget();
  UpdateRefreshTimer();
}

void OriginSideViewDownloads::RemovedFromWidget() {
  views::View::RemovedFromWidget();
  refresh_timer_.Stop();
  RefreshRows();
}

void OriginSideViewDownloads::VisibilityChanged(views::View* starting_from,
                                                bool is_visible) {
  views::View::VisibilityChanged(starting_from, is_visible);
  UpdateRefreshTimer();
}

void OriginSideViewDownloads::OnManagerInitialized(
    content::DownloadManager* manager) {
  // History has loaded, so an empty list now really means no downloads.
  needs_rebuild_ = true;
  ScheduleUpdate();
}

void OriginSideViewDownloads::OnManagerGoingDown(
    content::DownloadManager* manager) {
  for (Row* row : rows_) {
    row->ClearItem();
  }
  needs_rebuild_ = true;
  ScheduleUpdate();
}

void OriginSideViewDownloads::OnDownloadCreated(
    content::DownloadManager* manager,
    download::DownloadItem* item) {
  OnItemChanged(item);
}

void OriginSideViewDownloads::OnDownloadUpdated(
    content::DownloadManager* manager,
    download::DownloadItem* item) {
  OnItemChanged(item);
}

void OriginSideViewDownloads::OnDownloadRemoved(
    content::DownloadManager* manager,
    download::DownloadItem* item) {
  if (FindRow(item)) {
    needs_rebuild_ = true;
    ScheduleUpdate();
  }
}

void OriginSideViewDownloads::OnDownloadDestroyed(
    content::DownloadManager* manager,
    download::DownloadItem* item) {
  if (Row* row = FindRow(item)) {
    row->ClearItem();
    needs_rebuild_ = true;
    ScheduleUpdate();
  }
}

void OriginSideViewDownloads::OnItemChanged(download::DownloadItem* item) {
  // Shutdown cancels whatever is left; the rows are already let go.
  if (!notifier_ || !notifier_->GetManager()) {
    return;
  }
  Row* row = FindRow(item);
  const bool show = ShouldShow(item);
  if (row && show) {
    row->set_needs_update();
  } else if (row || (show && FitsInList(item))) {
    // Joining or leaving the list; positions shift, so start over.
    needs_rebuild_ = true;
  } else {
    return;
  }
  ScheduleUpdate();
}

OriginSideViewDownloads::Row* OriginSideViewDownloads::FindRow(
    const download::DownloadItem* item) const {
  for (Row* row : rows_) {
    if (row->item() == item) {
      return row;
    }
  }
  return nullptr;
}

bool OriginSideViewDownloads::FitsInList(
    const download::DownloadItem* item) const {
  return rows_.size() < kMaxRows ||
         item->GetStartTime() > rows_.back()->start_time();
}

void OriginSideViewDownloads::ScheduleUpdate() {
  if (update_scheduled_) {
    return;
  }
  update_scheduled_ = true;
  base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
      FROM_HERE, base::BindOnce(&OriginSideViewDownloads::ApplyUpdates,
                                weak_factory_.GetWeakPtr()));
}

void OriginSideViewDownloads::ApplyUpdates() {
  update_scheduled_ = false;
  if (needs_rebuild_) {
    Rebuild();
    return;
  }
  for (Row* row : rows_) {
    if (row->needs_update()) {
      row->Update();
    }
  }
}

void OriginSideViewDownloads::Rebuild() {
  needs_rebuild_ = false;
  rows_.clear();
  list_->RemoveAllChildViews();

  content::DownloadManager* manager =
      notifier_ ? notifier_->GetManager() : nullptr;
  if (manager) {
    content::DownloadManager::DownloadVector all;
    manager->GetAllDownloads(&all);
    std::vector<download::DownloadItem*> items(all.begin(), all.end());
    std::ranges::sort(items, [](const download::DownloadItem* a,
                                const download::DownloadItem* b) {
      return a->GetStartTime() > b->GetStartTime();
    });
    for (download::DownloadItem* item : items) {
      if (rows_.size() == kMaxRows) {
        break;
      }
      if (ShouldShow(item)) {
        rows_.push_back(list_->AddChildView(
            std::make_unique<Row>(item, open_all_downloads_)));
      }
    }
  }

  // Until history has loaded, an empty list may only mean "not yet".
  const bool empty =
      rows_.empty() && (!manager || manager->IsManagerInitialized());
  empty_state_->SetVisible(empty);
  scroll_->SetVisible(!empty);
}

void OriginSideViewDownloads::RefreshRows() {
  for (Row* row : rows_) {
    row->set_needs_update();
  }
  ScheduleUpdate();
}

void OriginSideViewDownloads::UpdateRefreshTimer() {
  const bool drawn = IsDrawn();
  if (drawn == refresh_timer_.IsRunning()) {
    return;
  }
  // Shown: relative times went stale while the list was hidden. Hidden:
  // progress of unknown size stops animating.
  RefreshRows();
  if (drawn) {
    refresh_timer_.Start(
        FROM_HERE, kRefreshInterval,
        base::BindRepeating(&OriginSideViewDownloads::RefreshRows,
                            base::Unretained(this)));
  } else {
    refresh_timer_.Stop();
  }
}

void OriginSideViewDownloads::UpdateColors() {
  const bool dark = origin_chrome_style::IsDark(this);
  empty_icon_->SetImage(ui::ImageModel::FromVectorIcon(
      kLeoDownloadIcon, origin_chrome_style::MutedText(dark), 28));
  empty_title_->SetEnabledColor(origin_chrome_style::PrimaryText(dark));
  empty_body_->SetEnabledColor(origin_chrome_style::SecondaryText(dark));
  footer_->SetBorder(views::CreateSolidSidedBorder(
      gfx::Insets::TLBR(1, 0, 0, 0), origin_chrome_style::Separator(dark)));
  open_all_button_->SetTextColor(views::Button::STATE_NORMAL,
                                 origin_chrome_style::SecondaryText(dark));
  open_all_button_->SetTextColor(views::Button::STATE_HOVERED,
                                 origin_chrome_style::PrimaryText(dark));
  open_all_button_->SetTextColor(views::Button::STATE_PRESSED,
                                 origin_chrome_style::PrimaryText(dark));
}

BEGIN_METADATA(OriginSideViewDownloads)
END_METADATA
