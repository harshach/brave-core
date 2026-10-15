// Copyright (c) 2026 The Brave Authors. All rights reserved.
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this file,
// You can obtain one at https://mozilla.org/MPL/2.0/.

#include "brave/browser/ui/views/frame/vertical_tabs/origin_sidebar_strips.h"

#include <algorithm>
#include <utility>

#include "base/functional/bind.h"
#include "base/i18n/number_formatting.h"
#include "base/strings/strcat.h"
#include "base/task/sequenced_task_runner.h"
#include "brave/browser/ui/views/frame/origin_chrome_style.h"
#include "brave/components/vector_icons/vector_icons.h"
#include "cc/paint/paint_flags.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "components/favicon/content/content_favicon_driver.h"
#include "content/public/browser/web_contents.h"
#include "ui/base/metadata/metadata_impl_macros.h"
#include "ui/base/models/image_model.h"
#include "ui/gfx/canvas.h"
#include "ui/views/animation/ink_drop.h"
#include "ui/views/background.h"
#include "ui/views/border.h"
#include "ui/views/controls/button/button.h"
#include "ui/views/controls/button/image_button.h"
#include "ui/views/controls/image_view.h"
#include "ui/views/controls/label.h"
#include "ui/views/layout/box_layout.h"

namespace {

constexpr int kRowInset = 8;
constexpr int kNoticeRowHeight = 30;
constexpr int kDownloadRowHeight = 30;
constexpr int kTwoLineRowHeight = 44;
constexpr int kIconSize = 16;
constexpr int kButtonSize = 24;
constexpr base::TimeDelta kNoticeDuration = base::Seconds(4);
constexpr base::TimeDelta kPausedMediaLinger = base::Minutes(2);

std::unique_ptr<views::ImageButton> MakeIconButton(
    views::Button::PressedCallback callback,
    const std::u16string& name) {
  auto button = std::make_unique<views::ImageButton>(std::move(callback));
  button->SetPreferredSize(gfx::Size(kButtonSize, kButtonSize));
  button->SetImageHorizontalAlignment(views::ImageButton::ALIGN_CENTER);
  button->SetImageVerticalAlignment(views::ImageButton::ALIGN_MIDDLE);
  button->SetAccessibleName(name);
  button->SetTooltipText(name);
  views::InkDrop::Get(button.get())
      ->SetMode(views::InkDropHost::InkDropMode::OFF);
  return button;
}

void SetIconButtonImage(views::ImageButton* button,
                        const gfx::VectorIcon& icon,
                        bool dark) {
  button->SetImageModel(
      views::Button::STATE_NORMAL,
      ui::ImageModel::FromVectorIcon(
          icon, origin_chrome_style::SecondaryText(dark), kIconSize));
  button->SetImageModel(
      views::Button::STATE_HOVERED,
      ui::ImageModel::FromVectorIcon(
          icon, origin_chrome_style::PrimaryText(dark), kIconSize));
}

// A row that highlights on hover and can be clicked as a whole.
class StripRow : public views::Button {
  METADATA_HEADER(StripRow, views::Button)

 public:
  explicit StripRow(PressedCallback callback)
      : views::Button(std::move(callback)) {
    views::InkDrop::Get(this)->SetMode(views::InkDropHost::InkDropMode::OFF);
  }

  void StateChanged(ButtonState old_state) override {
    views::Button::StateChanged(old_state);
    SchedulePaint();
  }

  void OnPaintBackground(gfx::Canvas* canvas) override {
    if (GetState() != STATE_HOVERED && GetState() != STATE_PRESSED) {
      return;
    }
    cc::PaintFlags fill;
    fill.setAntiAlias(true);
    fill.setColor(
        origin_chrome_style::HoverFill(origin_chrome_style::IsDark(this)));
    gfx::RectF bounds(GetLocalBounds());
    bounds.Inset(gfx::InsetsF::VH(1, kRowInset));
    canvas->DrawRoundRect(bounds, 8, fill);
  }
};

BEGIN_METADATA(StripRow)
END_METADATA

}  // namespace

// "Snoozed until Tomorrow 9:00 AM", "Back from snooze: …".
class OriginSidebarStrips::NoticeRow : public StripRow {
  METADATA_HEADER(NoticeRow, StripRow)

 public:
  NoticeRow()
      : StripRow(base::BindRepeating(&NoticeRow::OnPressed,
                                     base::Unretained(this))) {
    SetPreferredSize(gfx::Size(0, kNoticeRowHeight));
    auto* layout = SetLayoutManager(std::make_unique<views::BoxLayout>(
        views::BoxLayout::Orientation::kHorizontal,
        gfx::Insets::VH(0, kRowInset + 8), 8));
    layout->set_cross_axis_alignment(
        views::BoxLayout::CrossAxisAlignment::kCenter);
    icon_ = AddChildView(std::make_unique<views::ImageView>());
    label_ = AddChildView(std::make_unique<views::Label>());
    label_->SetFontList(
        origin_chrome_style::Font(12, gfx::Font::Weight::NORMAL));
    label_->SetHorizontalAlignment(gfx::ALIGN_LEFT);
    label_->SetElideBehavior(gfx::ELIDE_TAIL);
    layout->SetFlexForView(label_, 1);
  }

  void Set(std::u16string text, base::RepeatingClosure on_click) {
    label_->SetText(text);
    SetAccessibleName(text);
    on_click_ = std::move(on_click);
    Refresh();
  }

  void OnThemeChanged() override {
    StripRow::OnThemeChanged();
    Refresh();
  }

 private:
  void OnPressed() {
    if (on_click_) {
      on_click_.Run();
    }
  }

  void Refresh() {
    const bool dark = origin_chrome_style::IsDark(this);
    icon_->SetImage(ui::ImageModel::FromVectorIcon(
        kLeoCheckCircleFilledIcon, origin_chrome_style::kAccent, 14));
    label_->SetEnabledColor(origin_chrome_style::SecondaryText(dark));
  }

  raw_ptr<views::ImageView> icon_ = nullptr;
  raw_ptr<views::Label> label_ = nullptr;
  base::RepeatingClosure on_click_;
};

BEGIN_METADATA(OriginSidebarStrips, NoticeRow)
END_METADATA

// The longest-waiting due reminder, until it is done or dismissed.
class OriginSidebarStrips::ReminderRow : public StripRow {
  METADATA_HEADER(ReminderRow, StripRow)

 public:
  ReminderRow(PressedCallback open,
              base::RepeatingClosure done,
              base::RepeatingClosure dismiss)
      : StripRow(std::move(open)) {
    SetPreferredSize(gfx::Size(0, kTwoLineRowHeight));
    auto* layout = SetLayoutManager(std::make_unique<views::BoxLayout>(
        views::BoxLayout::Orientation::kHorizontal,
        gfx::Insets::VH(0, kRowInset + 8), 8));
    layout->set_cross_axis_alignment(
        views::BoxLayout::CrossAxisAlignment::kCenter);
    icon_ = AddChildView(std::make_unique<views::ImageView>());
    auto* text = AddChildView(std::make_unique<views::View>());
    text->SetLayoutManager(std::make_unique<views::BoxLayout>(
        views::BoxLayout::Orientation::kVertical));
    layout->SetFlexForView(text, 1);
    title_ = text->AddChildView(std::make_unique<views::Label>());
    title_->SetFontList(
        origin_chrome_style::Font(13, gfx::Font::Weight::SEMIBOLD));
    title_->SetHorizontalAlignment(gfx::ALIGN_LEFT);
    title_->SetElideBehavior(gfx::ELIDE_TAIL);
    subtitle_ = text->AddChildView(std::make_unique<views::Label>(u"Reminder"));
    subtitle_->SetFontList(
        origin_chrome_style::Font(11, gfx::Font::Weight::NORMAL));
    subtitle_->SetHorizontalAlignment(gfx::ALIGN_LEFT);
    done_ = AddChildView(MakeIconButton(
        base::BindRepeating(
            [](base::RepeatingClosure done) {
              // The row may go away with the reminder; finish the click.
              base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
                  FROM_HERE, done);
            },
            std::move(done)),
        u"Done"));
    dismiss_ = AddChildView(MakeIconButton(
        base::BindRepeating(
            [](base::RepeatingClosure dismiss) {
              base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
                  FROM_HERE, dismiss);
            },
            std::move(dismiss)),
        u"Hide"));
  }

  void SetTitle(const std::u16string& title) {
    title_->SetText(title);
    SetAccessibleName(u"Reminder: " + title);
  }

  void OnThemeChanged() override {
    StripRow::OnThemeChanged();
    const bool dark = origin_chrome_style::IsDark(this);
    icon_->SetImage(ui::ImageModel::FromVectorIcon(
        kLeoAlarmIcon, origin_chrome_style::kAccent, kIconSize));
    title_->SetEnabledColor(origin_chrome_style::PrimaryText(dark));
    subtitle_->SetEnabledColor(origin_chrome_style::kAccent);
    SetIconButtonImage(done_, kLeoCheckCircleOutlineIcon, dark);
    SetIconButtonImage(dismiss_, kLeoCloseIcon, dark);
  }

 private:
  raw_ptr<views::ImageView> icon_ = nullptr;
  raw_ptr<views::Label> title_ = nullptr;
  raw_ptr<views::Label> subtitle_ = nullptr;
  raw_ptr<views::ImageButton> done_ = nullptr;
  raw_ptr<views::ImageButton> dismiss_ = nullptr;
};

BEGIN_METADATA(OriginSidebarStrips, ReminderRow)
END_METADATA

// "Downloading report.pdf  45%" with a thin bar, then "Downloaded …".
class OriginSidebarStrips::DownloadRow : public StripRow {
  METADATA_HEADER(DownloadRow, StripRow)

 public:
  explicit DownloadRow(PressedCallback callback)
      : StripRow(std::move(callback)) {
    SetPreferredSize(gfx::Size(0, kDownloadRowHeight));
    auto* layout = SetLayoutManager(std::make_unique<views::BoxLayout>(
        views::BoxLayout::Orientation::kHorizontal,
        gfx::Insets::VH(0, kRowInset + 8), 8));
    layout->set_cross_axis_alignment(
        views::BoxLayout::CrossAxisAlignment::kCenter);
    icon_ = AddChildView(std::make_unique<views::ImageView>());
    label_ = AddChildView(std::make_unique<views::Label>());
    label_->SetFontList(
        origin_chrome_style::Font(12, gfx::Font::Weight::NORMAL));
    label_->SetHorizontalAlignment(gfx::ALIGN_LEFT);
    label_->SetElideBehavior(gfx::ELIDE_MIDDLE);
    layout->SetFlexForView(label_, 1);
    percent_ = AddChildView(std::make_unique<views::Label>());
    percent_->SetFontList(
        origin_chrome_style::Font(11, gfx::Font::Weight::SEMIBOLD));
  }

  void Set(const OriginDownloadSummary::Summary& summary) {
    summary_ = summary;
    if (summary.just_completed) {
      label_->SetText(u"Downloaded " + summary.current_file_name);
      percent_->SetText(std::u16string());
    } else {
      std::u16string name = summary.current_file_name;
      if (summary.in_progress_count > 1) {
        name = base::StrCat({name, u" and ",
                             base::FormatNumber(summary.in_progress_count - 1),
                             u" more"});
      }
      label_->SetText(name.empty() ? u"Downloading" : name);
      percent_->SetText(summary.percent ? base::FormatPercent(*summary.percent)
                                        : std::u16string());
    }
    SetAccessibleName(std::u16string(label_->GetText()));
    Refresh();
    SchedulePaint();
  }

  void OnThemeChanged() override {
    StripRow::OnThemeChanged();
    Refresh();
  }

  void PaintButtonContents(gfx::Canvas* canvas) override {
    if (summary_.just_completed || !summary_.percent) {
      return;
    }
    // A hairline of progress along the row's foot.
    const bool dark = origin_chrome_style::IsDark(this);
    const int track_width = width() - 2 * (kRowInset + 8);
    const gfx::Rect track(kRowInset + 8, height() - 3, track_width, 2);
    cc::PaintFlags flags;
    flags.setAntiAlias(true);
    flags.setColor(origin_chrome_style::Separator(dark));
    canvas->DrawRoundRect(gfx::RectF(track), 1, flags);
    gfx::Rect done = track;
    done.set_width(track.width() * std::clamp(*summary_.percent, 0, 100) / 100);
    flags.setColor(origin_chrome_style::kAccent);
    canvas->DrawRoundRect(gfx::RectF(done), 1, flags);
  }

 private:
  void Refresh() {
    const bool dark = origin_chrome_style::IsDark(this);
    icon_->SetImage(ui::ImageModel::FromVectorIcon(
        summary_.just_completed ? kLeoCheckCircleFilledIcon : kLeoDownloadIcon,
        summary_.just_completed ? origin_chrome_style::kAccent
                                : origin_chrome_style::SecondaryText(dark),
        14));
    label_->SetEnabledColor(origin_chrome_style::SecondaryText(dark));
    percent_->SetEnabledColor(origin_chrome_style::MutedText(dark));
  }

  OriginDownloadSummary::Summary summary_;
  raw_ptr<views::ImageView> icon_ = nullptr;
  raw_ptr<views::Label> label_ = nullptr;
  raw_ptr<views::Label> percent_ = nullptr;
};

BEGIN_METADATA(OriginSidebarStrips, DownloadRow)
END_METADATA

// What is playing in the window, with play/pause and skip.
class OriginSidebarStrips::MediaRow : public StripRow {
  METADATA_HEADER(MediaRow, StripRow)

 public:
  explicit MediaRow(OriginMediaMonitor* monitor)
      : StripRow(
            base::BindRepeating(&MediaRow::OnPressed, base::Unretained(this))),
        monitor_(monitor) {
    SetPreferredSize(gfx::Size(0, kTwoLineRowHeight));
    auto* layout = SetLayoutManager(std::make_unique<views::BoxLayout>(
        views::BoxLayout::Orientation::kHorizontal,
        gfx::Insets::VH(0, kRowInset + 8), 8));
    layout->set_cross_axis_alignment(
        views::BoxLayout::CrossAxisAlignment::kCenter);
    favicon_ = AddChildView(std::make_unique<views::ImageView>());
    favicon_->SetImageSize(gfx::Size(kIconSize, kIconSize));
    auto* text = AddChildView(std::make_unique<views::View>());
    text->SetLayoutManager(std::make_unique<views::BoxLayout>(
        views::BoxLayout::Orientation::kVertical));
    layout->SetFlexForView(text, 1);
    title_ = text->AddChildView(std::make_unique<views::Label>());
    title_->SetFontList(
        origin_chrome_style::Font(13, gfx::Font::Weight::NORMAL));
    title_->SetHorizontalAlignment(gfx::ALIGN_LEFT);
    title_->SetElideBehavior(gfx::ELIDE_TAIL);
    subtitle_ = text->AddChildView(std::make_unique<views::Label>());
    subtitle_->SetFontList(
        origin_chrome_style::Font(11, gfx::Font::Weight::NORMAL));
    subtitle_->SetHorizontalAlignment(gfx::ALIGN_LEFT);
    subtitle_->SetElideBehavior(gfx::ELIDE_TAIL);
    previous_ = AddChildView(MakeIconButton(
        base::BindRepeating(&MediaRow::Previous, base::Unretained(this)),
        u"Previous"));
    play_pause_ = AddChildView(MakeIconButton(
        base::BindRepeating(&MediaRow::PlayPause, base::Unretained(this)),
        u"Play or pause"));
    next_ = AddChildView(MakeIconButton(
        base::BindRepeating(&MediaRow::Next, base::Unretained(this)), u"Next"));
  }

  void Set(const OriginMediaMonitor::MediaItem& item) {
    item_ = item;
    title_->SetText(item.title);
    subtitle_->SetText(item.artist.empty() ? item.space_name : item.artist);
    SetAccessibleName(u"Playing: " + item.title);
    previous_->SetVisible(item.can_skip_to_previous);
    next_->SetVisible(item.can_skip_to_next);
    play_pause_->SetTooltipText(item.playing ? u"Pause" : u"Play");
    ui::ImageModel favicon;
    if (auto* contents = monitor_->GetMediaContents(item.tab_id)) {
      if (auto* driver =
              favicon::ContentFaviconDriver::FromWebContents(contents);
          driver && driver->FaviconIsValid()) {
        favicon = ui::ImageModel::FromImage(driver->GetFavicon());
      }
    }
    favicon_->SetImage(favicon);
    Refresh();
  }

  void OnThemeChanged() override {
    StripRow::OnThemeChanged();
    Refresh();
  }

 private:
  void OnPressed() { monitor_->ActivateTab(item_.tab_id); }
  void PlayPause() { monitor_->TogglePlayPause(item_.tab_id); }
  void Next() { monitor_->SkipToNextTrack(item_.tab_id); }
  void Previous() { monitor_->SkipToPreviousTrack(item_.tab_id); }

  void Refresh() {
    const bool dark = origin_chrome_style::IsDark(this);
    title_->SetEnabledColor(origin_chrome_style::PrimaryText(dark));
    subtitle_->SetEnabledColor(origin_chrome_style::SecondaryText(dark));
    SetIconButtonImage(previous_, kLeoPreviousOutlineIcon, dark);
    SetIconButtonImage(play_pause_,
                       item_.playing ? kLeoPauseFilledIcon : kLeoPlayFilledIcon,
                       dark);
    SetIconButtonImage(next_, kLeoNextOutlineIcon, dark);
  }

  raw_ptr<OriginMediaMonitor> monitor_ = nullptr;
  OriginMediaMonitor::MediaItem item_;
  raw_ptr<views::ImageView> favicon_ = nullptr;
  raw_ptr<views::Label> title_ = nullptr;
  raw_ptr<views::Label> subtitle_ = nullptr;
  raw_ptr<views::ImageButton> previous_ = nullptr;
  raw_ptr<views::ImageButton> play_pause_ = nullptr;
  raw_ptr<views::ImageButton> next_ = nullptr;
};

BEGIN_METADATA(OriginSidebarStrips, MediaRow)
END_METADATA

OriginSidebarStrips::OriginSidebarStrips(BrowserWindowInterface* browser,
                                         OriginMediaMonitor* media_monitor,
                                         OriginLaterService* later_service,
                                         Callbacks callbacks)
    : browser_(browser),
      media_monitor_(media_monitor),
      later_service_(later_service),
      download_summary_(
          std::make_unique<OriginDownloadSummary>(browser->GetProfile())),
      callbacks_(std::move(callbacks)) {
  SetLayoutManager(std::make_unique<views::BoxLayout>(
      views::BoxLayout::Orientation::kVertical, gfx::Insets::TLBR(2, 0, 2, 0)));

  notice_row_ = AddChildView(std::make_unique<NoticeRow>());
  notice_row_->SetVisible(false);

  reminder_row_ = AddChildView(std::make_unique<ReminderRow>(
      base::BindRepeating(
          [](OriginSidebarStrips* strips) {
            if (strips->callbacks_.show_reminders) {
              strips->callbacks_.show_reminders.Run();
            }
          },
          base::Unretained(this)),
      base::BindRepeating(
          [](base::WeakPtr<OriginSidebarStrips> strips) {
            if (!strips || !strips->later_service_) {
              return;
            }
            if (auto reminder = strips->later_service_->GetReminderToShow()) {
              strips->later_service_->SetReminderDone(reminder->id, true);
            }
          },
          weak_factory_.GetWeakPtr()),
      base::BindRepeating(
          [](base::WeakPtr<OriginSidebarStrips> strips) {
            if (!strips || !strips->later_service_) {
              return;
            }
            if (auto reminder = strips->later_service_->GetReminderToShow()) {
              strips->later_service_->DismissReminder(reminder->id);
            }
          },
          weak_factory_.GetWeakPtr())));
  reminder_row_->SetVisible(false);

  download_row_ =
      AddChildView(std::make_unique<DownloadRow>(base::BindRepeating(
          [](OriginSidebarStrips* strips) {
            if (strips->callbacks_.show_downloads) {
              strips->callbacks_.show_downloads.Run();
            }
          },
          base::Unretained(this))));
  download_row_->SetVisible(false);

  if (media_monitor_) {
    media_row_ = AddChildView(std::make_unique<MediaRow>(media_monitor_));
    media_observation_.Observe(media_monitor_);
  }
  if (media_row_) {
    media_row_->SetVisible(false);
  }

  download_observation_.Observe(download_summary_.get());
  if (later_service_) {
    later_observation_.Observe(later_service_);
  }

  UpdateMedia();
  UpdateDownload();
  UpdateReminder();
}

OriginSidebarStrips::~OriginSidebarStrips() = default;

void OriginSidebarStrips::ShowNotice(std::u16string text,
                                     base::RepeatingClosure on_click) {
  notice_row_->Set(std::move(text), std::move(on_click));
  SetRowVisible(notice_row_, true);
  notice_timer_.Start(FROM_HERE, kNoticeDuration,
                      base::BindOnce(
                          [](OriginSidebarStrips* strips) {
                            strips->SetRowVisible(strips->notice_row_, false);
                          },
                          base::Unretained(this)));
}

views::View* OriginSidebarStrips::notice_row_for_testing() {
  return notice_row_;
}

views::View* OriginSidebarStrips::reminder_row_for_testing() {
  return reminder_row_;
}

views::View* OriginSidebarStrips::download_row_for_testing() {
  return download_row_;
}

views::View* OriginSidebarStrips::media_row_for_testing() {
  return media_row_;
}

void OriginSidebarStrips::OnThemeChanged() {
  views::View::OnThemeChanged();
  SchedulePaint();
}

void OriginSidebarStrips::OnOriginMediaChanged() {
  UpdateMedia();
}

void OriginSidebarStrips::OnOriginDownloadSummaryChanged() {
  UpdateDownload();
}

void OriginSidebarStrips::OnOriginLaterChanged() {
  UpdateReminder();
}

void OriginSidebarStrips::OnSnoozedPageReturned(
    content::WebContents* contents) {
  TabStripModel* model = browser_->GetTabStripModel();
  if (model->GetIndexOfWebContents(contents) == TabStripModel::kNoTab) {
    return;
  }
  ShowNotice(u"Back from snooze: " + contents->GetTitle(),
             base::BindRepeating(
                 [](base::WeakPtr<content::WebContents> contents,
                    TabStripModel* model) {
                   if (!contents) {
                     return;
                   }
                   const int index =
                       model->GetIndexOfWebContents(contents.get());
                   if (index != TabStripModel::kNoTab) {
                     model->ActivateTabAt(index);
                   }
                 },
                 contents->GetWeakPtr(), base::Unretained(model)));
}

void OriginSidebarStrips::UpdateMedia() {
  if (!media_row_) {
    return;
  }
  std::optional<OriginMediaMonitor::MediaItem> item =
      media_monitor_->GetActiveMedia(OriginMediaMonitor::MediaSource::kAny);
  const base::TimeTicks now = base::TimeTicks::Now();
  bool show = false;
  if (item) {
    if (item->playing) {
      last_heard_playing_[item->tab_id] = now;
    }
    // Something playing in plain sight needs no reminder here; something that
    // just paused stays a moment so it can be resumed from the sidebar.
    const auto heard = last_heard_playing_.find(item->tab_id);
    const bool recently_playing = heard != last_heard_playing_.end() &&
                                  now - heard->second < kPausedMediaLinger;
    show = !item->page_visible && (item->playing || recently_playing);
    if (show) {
      media_row_->Set(*item);
    }
    if (!item->playing && recently_playing) {
      media_linger_timer_.Start(
          FROM_HERE, kPausedMediaLinger - (now - heard->second),
          base::BindOnce(&OriginSidebarStrips::UpdateMedia,
                         base::Unretained(this)));
    }
  }
  SetRowVisible(media_row_, show);
}

void OriginSidebarStrips::UpdateDownload() {
  const OriginDownloadSummary::Summary summary =
      download_summary_->GetSummary();
  const bool show = summary.in_progress_count > 0 || summary.just_completed;
  if (show) {
    download_row_->Set(summary);
  }
  SetRowVisible(download_row_, show);
}

void OriginSidebarStrips::UpdateReminder() {
  std::optional<OriginLaterService::Reminder> reminder =
      later_service_ ? later_service_->GetReminderToShow() : std::nullopt;
  if (reminder) {
    reminder_row_->SetTitle(reminder->title);
  }
  SetRowVisible(reminder_row_, reminder.has_value());
}

void OriginSidebarStrips::SetRowVisible(views::View* row, bool visible) {
  if (row->GetVisible() == visible) {
    return;
  }
  row->SetVisible(visible);
  PreferredSizeChanged();
}

BEGIN_METADATA(OriginSidebarStrips)
END_METADATA
