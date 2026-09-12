// Copyright (c) 2026 The Brave Authors. All rights reserved.
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this file,
// You can obtain one at https://mozilla.org/MPL/2.0/.

#include "brave/browser/ui/views/frame/origin_widgets/origin_calendar_widget.h"

#include <utility>

#include "base/functional/bind.h"
#include "base/i18n/case_conversion.h"
#include "base/i18n/time_formatting.h"
#include "base/strings/string_number_conversions.h"
#include "brave/browser/ui/views/frame/origin_widgets/origin_widget_controls.h"
#include "brave/browser/ui/views/frame/origin_widgets/origin_widget_style.h"
#include "brave/components/vector_icons/vector_icons.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/singleton_tabs.h"
#include "content/public/browser/storage_partition.h"
#include "ui/base/metadata/metadata_impl_macros.h"
#include "ui/views/background.h"
#include "ui/views/border.h"
#include "ui/views/controls/button/label_button.h"
#include "ui/views/controls/label.h"
#include "ui/views/layout/box_layout.h"

namespace {

constexpr base::TimeDelta kRefreshInterval = base::Minutes(1);
constexpr base::TimeDelta kRetryInterval = base::Minutes(3);
constexpr SkColor kGoogleBlue = SkColorSetRGB(0x1A, 0x73, 0xE8);

SkColor CalendarAccent(bool dark) {
  return dark ? SkColorSetRGB(0x84, 0xCA, 0xFF) : kGoogleBlue;
}

class CalendarButton : public views::LabelButton {
  METADATA_HEADER(CalendarButton, views::LabelButton)

 public:
  CalendarButton(PressedCallback callback, const std::u16string& text)
      : LabelButton(std::move(callback), text) {
    label()->SetAutoColorReadabilityEnabled(false);
    label()->SetFontList(
        origin_style::ChromeFont(11, gfx::Font::Weight::SEMIBOLD));
    SetBorder(views::CreateEmptyBorder(gfx::Insets::VH(5, 6)));
  }

  void SetEventTitleStyle() {
    label()->SetFontList(
        origin_style::ChromeFont(12, gfx::Font::Weight::MEDIUM));
    label()->SetMultiLine(true);
    label()->SetMaxLines(2);
    label()->SetHorizontalAlignment(gfx::ALIGN_LEFT);
    SetHorizontalAlignment(gfx::ALIGN_LEFT);
    SetBorder(views::CreateEmptyBorder(gfx::Insets()));
  }
};
BEGIN_METADATA(CalendarButton)
END_METADATA

std::u16string EventTime(const OriginCalendarEvent& event,
                         base::Time now,
                         bool today) {
  if (event.all_day) {
    return u"All day";
  }
  std::u16string result = base::TimeFormatTimeOfDay(event.start) + u" – " +
                          base::TimeFormatTimeOfDay(event.end);
  if (today && event.start <= now && event.end > now) {
    result += u" · Now";
  } else if (today && event.start > now && event.start - now < base::Hours(1)) {
    result += u" · in " +
              base::NumberToString16((event.start - now).InMinutes() + 1) +
              u" min";
  }
  return result;
}

std::u16string SyncError(OriginCalendarSource::Error error) {
  switch (error) {
    case OriginCalendarSource::Error::kAuthentication:
      return u"Google sign-in needs attention. Reconnect your calendar.";
    case OriginCalendarSource::Error::kPermission:
      return u"Calendar access wasn't granted. Reconnect and allow access.";
    case OriginCalendarSource::Error::kRateLimited:
      return u"Google Calendar is busy. We'll retry shortly.";
    case OriginCalendarSource::Error::kInvalidResponse:
      return u"Couldn't read this day's events. Try refreshing.";
    case OriginCalendarSource::Error::kNetwork:
      return u"Couldn't sync. Check your connection and retry.";
  }
}

}  // namespace

// static
std::unique_ptr<OriginWidgetView> OriginCalendarWidgetView::Create(
    const OriginWidgetContext& context) {
  return std::make_unique<OriginCalendarWidgetView>(context);
}

OriginCalendarWidgetView::OriginCalendarWidgetView(
    const OriginWidgetContext& context)
    : OriginWidgetView("calendar"),
      context_(context),
      selected_day_(OriginCalendarSource::StartOfDay(base::Time::Now())) {
  if (context_.browser) {
    auto* profile = context_.browser->GetProfile();
    service_ = OriginCalendarService::GetForProfile(profile);
    source_ = std::make_unique<OriginCalendarSource>(
        profile->GetDefaultStoragePartition()
            ->GetURLLoaderFactoryForBrowserProcess());
  }
  SetLayoutManager(std::make_unique<views::BoxLayout>(
      views::BoxLayout::Orientation::kVertical, gfx::Insets(10), 6));

  auto* header = AddChildView(std::make_unique<views::View>());
  auto* header_layout =
      header->SetLayoutManager(std::make_unique<views::BoxLayout>(
          views::BoxLayout::Orientation::kHorizontal, gfx::Insets(), 4));
  title_label_ =
      header->AddChildView(CreateOriginWidgetTitleLabel(u"Google Calendar"));
  header_layout->SetFlexForView(title_label_, 1);
  refresh_button_ =
      header->AddChildView(std::make_unique<OriginWidgetIconButton>(
          base::BindRepeating(&OriginCalendarWidgetView::Refresh,
                              base::Unretained(this)),
          kLeoReloadIcon, u"Refresh Google Calendar"));
  header->AddChildView(std::make_unique<OriginWidgetIconButton>(
      base::BindRepeating(&OriginCalendarWidgetView::OpenCalendar,
                          base::Unretained(this)),
      kLeoLaunchIcon, u"Open this day in Google Calendar"));

  auto* navigation = AddChildView(std::make_unique<views::View>());
  auto* nav_layout =
      navigation->SetLayoutManager(std::make_unique<views::BoxLayout>(
          views::BoxLayout::Orientation::kHorizontal, gfx::Insets(), 1));
  day_label_ = navigation->AddChildView(CreateOriginWidgetSectionLabel(u""));
  nav_layout->SetFlexForView(day_label_, 1);
  today_button_ = navigation->AddChildView(std::make_unique<CalendarButton>(
      base::BindRepeating(&OriginCalendarWidgetView::GoToToday,
                          base::Unretained(this)),
      u"Today"));
  navigation->AddChildView(std::make_unique<OriginWidgetIconButton>(
      base::BindRepeating(&OriginCalendarWidgetView::ChangeDay,
                          base::Unretained(this), -1),
      kLeoCaratLeftIcon, u"Previous day", 28, 14));
  navigation->AddChildView(std::make_unique<OriginWidgetIconButton>(
      base::BindRepeating(&OriginCalendarWidgetView::ChangeDay,
                          base::Unretained(this), 1),
      kLeoCaratRightIcon, u"Next day", 28, 14));

  connect_container_ = AddChildView(std::make_unique<views::View>());
  connect_container_->SetLayoutManager(std::make_unique<views::BoxLayout>(
      views::BoxLayout::Orientation::kVertical, gfx::Insets::VH(6, 0), 10));
  connect_label_ = connect_container_->AddChildView(
      CreateOriginWidgetSubtitleLabel(u"Your day, at a glance. Connect Google "
                                      u"Calendar to keep events in sync."));
  connect_label_->SetMultiLine(true);
  connect_button_ =
      connect_container_->AddChildView(std::make_unique<CalendarButton>(
          base::BindRepeating(&OriginCalendarWidgetView::Connect,
                              base::Unretained(this)),
          u"Connect Google Calendar"));
  connect_button_->SetBackground(
      views::CreateRoundedRectBackground(kGoogleBlue, 6));
  connect_button_->SetEnabledTextColors(SK_ColorWHITE);
  connect_button_->SetBorder(views::CreateEmptyBorder(gfx::Insets::VH(9, 10)));

  event_list_ = AddChildView(std::make_unique<views::View>());
  event_list_->SetLayoutManager(std::make_unique<views::BoxLayout>(
      views::BoxLayout::Orientation::kVertical, gfx::Insets(), 4));
  auto* footer = AddChildView(std::make_unique<views::View>());
  auto* footer_layout =
      footer->SetLayoutManager(std::make_unique<views::BoxLayout>(
          views::BoxLayout::Orientation::kHorizontal, gfx::Insets(), 6));
  footer_layout->set_cross_axis_alignment(
      views::BoxLayout::CrossAxisAlignment::kStart);
  status_label_ = footer->AddChildView(CreateOriginWidgetSubtitleLabel(u""));
  status_label_->SetMultiLine(true);
  status_label_->SetBorder(views::CreateEmptyBorder(gfx::Insets::VH(5, 0)));
  footer_layout->SetFlexForView(status_label_, 1);
  disconnect_button_ = footer->AddChildView(std::make_unique<CalendarButton>(
      base::BindRepeating(&OriginCalendarWidgetView::Disconnect,
                          base::Unretained(this)),
      u"Disconnect"));
  if (service_) {
    connection_subscription_ = service_->Subscribe(
        base::BindRepeating(&OriginCalendarWidgetView::OnConnectionChanged,
                            base::Unretained(this)));
  }
  OnConnectionChanged();
}

OriginCalendarWidgetView::~OriginCalendarWidgetView() = default;

void OriginCalendarWidgetView::AddedToWidget() {
  Refresh();
}

void OriginCalendarWidgetView::VisibilityChanged(views::View* starting_from,
                                                 bool is_visible) {
  if (IsDrawn()) {
    Refresh();
  } else {
    fetch_timer_.Stop();
    render_timer_.Stop();
  }
}

void OriginCalendarWidgetView::Refresh() {
  UpdateHeader();
  if (!connected_ || loading_ || !source_ || !GetWidget() || !IsDrawn()) {
    return;
  }
  if (follow_today_) {
    const auto today = OriginCalendarSource::StartOfDay(base::Time::Now());
    if (today != selected_day_) {
      selected_day_ = today;
      day_ = {};
      loaded_ = false;
      last_updated_ = base::Time();
      RebuildEventRows();
    }
  }
  loading_ = true;
  retried_auth_ = false;
  sync_error_.clear();
  ++generation_;
  UpdateHeader();
  service_->GetAccessToken(
      base::BindOnce(&OriginCalendarWidgetView::OnAccessToken,
                     weak_factory_.GetWeakPtr(), generation_));
  if (!render_timer_.IsRunning()) {
    render_timer_.Start(FROM_HERE, base::Seconds(30),
                        base::BindRepeating(&OriginCalendarWidgetView::Tick,
                                            base::Unretained(this)));
  }
}

void OriginCalendarWidgetView::Connect() {
  if (!service_) {
    return;
  }
  if (service_->is_connecting()) {
    service_->CancelConnection();
  } else {
    service_->Connect(base::BindOnce(&OriginCalendarWidgetView::OpenUrl,
                                     weak_factory_.GetWeakPtr()));
  }
}

void OriginCalendarWidgetView::Disconnect() {
  if (service_) {
    service_->Disconnect();
  }
}

void OriginCalendarWidgetView::OnConnectionChanged() {
  const bool connected = service_ && service_->is_connected();
  const bool changed = connected != connected_;
  connected_ = connected;
  if (!connected) {
    ++generation_;
    loading_ = false;
    loaded_ = false;
    day_ = {};
    sync_error_.clear();
    fetch_timer_.Stop();
    render_timer_.Stop();
    if (source_) {
      source_->Cancel();
    }
  }
  UpdateHeader();
  RebuildEventRows();
  if (changed && connected_) {
    Refresh();
  }
}

void OriginCalendarWidgetView::ChangeDay(int offset) {
  SelectDay(OriginCalendarSource::OffsetDay(selected_day_, offset));
}

void OriginCalendarWidgetView::GoToToday() {
  SelectDay(OriginCalendarSource::StartOfDay(base::Time::Now()));
}

void OriginCalendarWidgetView::SelectDay(base::Time day) {
  selected_day_ = day;
  follow_today_ = day == OriginCalendarSource::StartOfDay(base::Time::Now());
  ++generation_;
  if (source_) {
    source_->Cancel();
  }
  day_ = {};
  last_updated_ = base::Time();
  loading_ = false;
  loaded_ = false;
  sync_error_.clear();
  RebuildEventRows();
  Refresh();
}

void OriginCalendarWidgetView::Tick() {
  if (!IsDrawn()) {
    fetch_timer_.Stop();
    render_timer_.Stop();
    return;
  }
  if (follow_today_ &&
      selected_day_ != OriginCalendarSource::StartOfDay(base::Time::Now())) {
    GoToToday();
    return;
  }
  RebuildEventRows();
}

void OriginCalendarWidgetView::OnAccessToken(
    int generation,
    OriginCalendarService::TokenResult token) {
  if (generation != generation_) {
    return;
  }
  if (!token.has_value()) {
    sync_error_ = std::move(token.error());
    FinishRefresh();
    return;
  }
  source_->Fetch(std::move(*token), selected_day_,
                 base::BindOnce(&OriginCalendarWidgetView::OnEventsFetched,
                                weak_factory_.GetWeakPtr(), generation));
}

void OriginCalendarWidgetView::OnEventsFetched(
    int generation,
    OriginCalendarSource::Result result) {
  if (generation != generation_) {
    return;
  }
  if (!result.has_value()) {
    if (result.error() == OriginCalendarSource::Error::kAuthentication &&
        !retried_auth_) {
      retried_auth_ = true;
      service_->InvalidateAccessToken();
      service_->GetAccessToken(
          base::BindOnce(&OriginCalendarWidgetView::OnAccessToken,
                         weak_factory_.GetWeakPtr(), generation));
      return;
    }
    sync_error_ = SyncError(result.error());
  } else {
    day_ = std::move(*result);
    last_updated_ = base::Time::Now();
    loaded_ = true;
    sync_error_.clear();
  }
  FinishRefresh();
}

void OriginCalendarWidgetView::FinishRefresh() {
  loading_ = false;
  UpdateHeader();
  RebuildEventRows();
  if (connected_ && IsDrawn()) {
    fetch_timer_.Start(FROM_HERE,
                       sync_error_.empty() ? kRefreshInterval : kRetryInterval,
                       base::BindOnce(&OriginCalendarWidgetView::Refresh,
                                      base::Unretained(this)));
  }
}

void OriginCalendarWidgetView::UpdateHeader() {
  const bool today =
      selected_day_ == OriginCalendarSource::StartOfDay(base::Time::Now());
  day_label_->SetText(
      base::i18n::ToUpper((today ? u"Today · " : u"") +
                          base::LocalizedTimeFormatWithPattern(
                              selected_day_, today ? "MMM d" : "EEE, MMM d")));
  day_label_->SetTooltipText(base::LocalizedTimeFormatWithPattern(
      selected_day_, "EEEE, MMMM d, yyyy"));
  day_label_->SetEnabledColor(
      today ? CalendarAccent(origin_style::IsDark(this))
            : origin_style::PrimaryText(origin_style::IsDark(this)));
  today_button_->SetVisible(!today);
  refresh_button_->SetVisible(connected_);
  refresh_button_->SetEnabled(!loading_);
  disconnect_button_->SetVisible(connected_);
  connect_container_->SetVisible(!connected_);
  event_list_->SetVisible(connected_);
  if (!service_) {
    connect_label_->SetText(
        u"Open a regular window to connect Google Calendar.");
    connect_button_->SetEnabled(false);
  } else {
    connect_button_->SetEnabled(!service_->is_initializing());
    connect_button_->SetText(service_->is_connecting()
                                 ? u"Cancel sign-in"
                                 : u"Connect Google Calendar");
  }
  std::u16string status;
  if (service_ && !connected_) {
    if (service_->is_initializing()) {
      status = u"Checking calendar connection…";
    } else if (service_->is_connecting()) {
      status = u"Finish signing in with Google in the new tab.";
    } else {
      status = service_->error();
    }
  } else if (loading_) {
    status = u"Syncing…";
  } else if (!sync_error_.empty()) {
    status = sync_error_ + (loaded_ ? u" Showing the last update." : u"");
  } else if (loaded_) {
    status = u"Updated " + base::TimeFormatTimeOfDay(last_updated_);
  }
  status_label_->SetText(status);
  status_label_->SetVisible(!status.empty());
  status_label_->SetTooltipText(
      u"Refreshes every minute while this widget is visible.");
  InvalidateLayout();
}

void OriginCalendarWidgetView::RebuildEventRows() {
  event_list_->RemoveAllChildViews();
  const bool dark = origin_style::IsDark(this);
  const auto now = base::Time::Now();
  const bool today = selected_day_ == OriginCalendarSource::StartOfDay(now);
  const SkColor accent_color = CalendarAccent(dark);
  if (connected_ && loaded_ && day_.events.empty()) {
    auto* label = event_list_->AddChildView(
        CreateOriginWidgetSubtitleLabel(u"No events for this day"));
    label->SetEnabledColor(origin_style::SecondaryText(dark));
    label->SetBorder(views::CreateEmptyBorder(gfx::Insets::VH(14, 0)));
  }
  bool all_day_heading_added = false;
  for (const auto& event : day_.events) {
    if (event.all_day && !all_day_heading_added) {
      auto* heading =
          event_list_->AddChildView(CreateOriginWidgetSectionLabel(u"ALL DAY"));
      heading->SetEnabledColor(origin_style::MutedText(dark));
      heading->SetBorder(
          views::CreateEmptyBorder(gfx::Insets::TLBR(4, 0, 2, 0)));
      all_day_heading_added = true;
    }
    auto* row = event_list_->AddChildView(std::make_unique<views::View>());
    auto* row_layout = row->SetLayoutManager(std::make_unique<views::BoxLayout>(
        views::BoxLayout::Orientation::kHorizontal,
        gfx::Insets::VH(7, event.all_day ? 8 : 0), 8));
    row_layout->set_cross_axis_alignment(
        views::BoxLayout::CrossAxisAlignment::kStart);
    if (event.all_day) {
      row->SetBackground(views::CreateRoundedRectBackground(
          SkColorSetA(accent_color, dark ? 0x1A : 0x12), 7));
    }
    auto* marker = row->AddChildView(std::make_unique<views::View>());
    marker->SetLayoutManager(std::make_unique<views::BoxLayout>(
        views::BoxLayout::Orientation::kVertical,
        gfx::Insets::TLBR(4, 0, 0, 0)));
    auto* dot = marker->AddChildView(std::make_unique<views::View>());
    dot->SetPreferredSize(gfx::Size(8, 8));
    dot->SetBackground(views::CreateRoundedRectBackground(accent_color, 4));
    auto* text = row->AddChildView(std::make_unique<views::View>());
    text->SetLayoutManager(std::make_unique<views::BoxLayout>(
        views::BoxLayout::Orientation::kVertical, gfx::Insets(), 2));
    row_layout->SetFlexForView(text, 1);
    if (!event.all_day) {
      auto* when = text->AddChildView(
          CreateOriginWidgetSectionLabel(EventTime(event, now, today)));
      const bool happening_now = event.start <= now && event.end > now;
      when->SetEnabledColor(happening_now ? accent_color
                                          : origin_style::SecondaryText(dark));
    }
    auto* title = text->AddChildView(std::make_unique<CalendarButton>(
        base::BindRepeating(&OriginCalendarWidgetView::OpenUrl,
                            base::Unretained(this), event.event_url),
        event.title));
    title->SetEventTitleStyle();
    title->SetEnabled(event.event_url.is_valid());
    title->SetEnabledTextColors(origin_style::PrimaryText(dark));
    title->SetTextColor(views::Button::STATE_DISABLED,
                        origin_style::PrimaryText(dark));
    title->SetTooltipText(event.title + u"\n" + EventTime(event, now, today));
    const auto& detail =
        event.location.empty() ? event.organizer : event.location;
    if (!detail.empty()) {
      auto* location =
          text->AddChildView(CreateOriginWidgetSubtitleLabel(detail));
      location->SetEnabledColor(origin_style::MutedText(dark));
      location->SetTooltipText(detail);
    }
    if (!event.join_url.is_empty() && !event.all_day && event.end > now &&
        event.start <= now + base::Minutes(15)) {
      auto* join = row->AddChildView(std::make_unique<CalendarButton>(
          base::BindRepeating(&OriginCalendarWidgetView::OpenUrl,
                              base::Unretained(this), event.join_url),
          u"Join"));
      join->SetEnabledTextColors(accent_color);
      join->SetBackground(views::CreateRoundedRectBackground(
          SkColorSetARGB(0x24, 0x1A, 0x73, 0xE8), 5));
    }
  }
  InvalidateLayout();
}

void OriginCalendarWidgetView::OnThemeChanged() {
  OriginWidgetView::OnThemeChanged();
  const bool dark = origin_style::IsDark(this);
  title_label_->SetEnabledColor(origin_style::PrimaryText(dark));
  for (auto* label : {status_label_.get(), connect_label_.get()}) {
    label->SetEnabledColor(origin_style::MutedText(dark));
  }
  today_button_->SetEnabledTextColors(origin_style::PrimaryText(dark));
  disconnect_button_->SetEnabledTextColors(origin_style::MutedText(dark));
  UpdateHeader();
  RebuildEventRows();
}

void OriginCalendarWidgetView::OpenUrl(const GURL& url) {
  if (context_.browser && url.is_valid()) {
    ShowSingletonTab(context_.browser.get(), url);
  }
}

void OriginCalendarWidgetView::OpenCalendar() {
  OpenUrl(OriginCalendarSource::DayUrl(selected_day_));
}

BEGIN_METADATA(OriginCalendarWidgetView)
END_METADATA
