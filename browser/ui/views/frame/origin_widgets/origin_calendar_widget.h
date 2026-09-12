// Copyright (c) 2026 The Brave Authors. All rights reserved.
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this file,
// You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef BRAVE_BROWSER_UI_VIEWS_FRAME_ORIGIN_WIDGETS_ORIGIN_CALENDAR_WIDGET_H_
#define BRAVE_BROWSER_UI_VIEWS_FRAME_ORIGIN_WIDGETS_ORIGIN_CALENDAR_WIDGET_H_

#include <memory>

#include "base/callback_list.h"
#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "base/time/time.h"
#include "base/timer/timer.h"
#include "brave/browser/ui/views/frame/origin_widgets/origin_calendar_service.h"
#include "brave/browser/ui/views/frame/origin_widgets/origin_calendar_source.h"
#include "brave/browser/ui/views/frame/origin_widgets/origin_widget.h"
#include "ui/base/metadata/metadata_header_macros.h"

class OriginWidgetIconButton;
namespace views {
class Label;
class LabelButton;
}  // namespace views

// A single-day Google Calendar agenda, with local-day navigation and automatic
// refresh while visible. Each window selects its own day and shares sign-in.
class OriginCalendarWidgetView : public OriginWidgetView {
  METADATA_HEADER(OriginCalendarWidgetView, OriginWidgetView)

 public:
  static std::unique_ptr<OriginWidgetView> Create(
      const OriginWidgetContext& context);
  explicit OriginCalendarWidgetView(const OriginWidgetContext& context);
  ~OriginCalendarWidgetView() override;

  void Refresh() override;
  void OnThemeChanged() override;
  void AddedToWidget() override;
  void VisibilityChanged(views::View* starting_from, bool is_visible) override;

 private:
  friend class OriginCalendarWidgetViewTest;

  void Connect();
  void Disconnect();
  void OnConnectionChanged();
  void ChangeDay(int offset);
  void GoToToday();
  void SelectDay(base::Time day);
  void Tick();
  void OnAccessToken(int generation, OriginCalendarService::TokenResult token);
  void OnEventsFetched(int generation, OriginCalendarSource::Result result);
  void FinishRefresh();
  void RebuildEventRows();
  void UpdateHeader();
  void OpenUrl(const GURL& url);
  void OpenCalendar();

  OriginWidgetContext context_;
  raw_ptr<OriginCalendarService> service_ = nullptr;
  std::unique_ptr<OriginCalendarSource> source_;
  base::CallbackListSubscription connection_subscription_;
  OriginCalendarDay day_;
  base::Time selected_day_;
  base::Time last_updated_;
  bool follow_today_ = true;
  bool connected_ = false;
  bool loading_ = false;
  bool loaded_ = false;
  bool retried_auth_ = false;
  int generation_ = 0;
  std::u16string sync_error_;
  base::OneShotTimer fetch_timer_;
  base::RepeatingTimer render_timer_;

  raw_ptr<views::Label> title_label_ = nullptr;
  raw_ptr<views::Label> day_label_ = nullptr;
  raw_ptr<views::Label> status_label_ = nullptr;
  raw_ptr<views::Label> connect_label_ = nullptr;
  raw_ptr<views::View> connect_container_ = nullptr;
  raw_ptr<views::View> event_list_ = nullptr;
  raw_ptr<views::LabelButton> connect_button_ = nullptr;
  raw_ptr<views::LabelButton> today_button_ = nullptr;
  raw_ptr<views::LabelButton> disconnect_button_ = nullptr;
  raw_ptr<OriginWidgetIconButton> refresh_button_ = nullptr;
  base::WeakPtrFactory<OriginCalendarWidgetView> weak_factory_{this};
};

#endif  // BRAVE_BROWSER_UI_VIEWS_FRAME_ORIGIN_WIDGETS_ORIGIN_CALENDAR_WIDGET_H_
