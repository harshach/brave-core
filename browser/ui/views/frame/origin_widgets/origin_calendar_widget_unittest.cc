// Copyright (c) 2026 The Brave Authors. All rights reserved.
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this file,
// You can obtain one at https://mozilla.org/MPL/2.0/.

#include "brave/browser/ui/views/frame/origin_widgets/origin_calendar_widget.h"

#include <memory>
#include <string_view>
#include <utility>

#include "chrome/test/views/chrome_views_test_base.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "ui/views/controls/button/label_button.h"
#include "ui/views/test/button_test_api.h"
#include "ui/views/view_utils.h"

namespace {

views::Button* FindButton(views::View* view, std::u16string_view name) {
  if (auto* button = views::AsViewClass<views::Button>(view);
      button && button->GetTooltipText() == name) {
    return button;
  }
  if (auto* button = views::AsViewClass<views::LabelButton>(view);
      button && button->GetText() == name) {
    return button;
  }
  for (views::View* child : view->children()) {
    if (auto* button = FindButton(child, name)) {
      return button;
    }
  }
  return nullptr;
}

}  // namespace

class OriginCalendarWidgetViewTest : public ChromeViewsTestBase {
 protected:
  void SetUp() override {
    ChromeViewsTestBase::SetUp();
    view_ = std::make_unique<OriginCalendarWidgetView>(OriginWidgetContext{});
    view_->connected_ = true;
  }

  void TearDown() override {
    view_.reset();
    ChromeViewsTestBase::TearDown();
  }

  void DeliverDay(int generation, std::u16string_view title) {
    OriginCalendarDay day;
    OriginCalendarEvent event;
    event.title = title;
    event.start = selected_day();
    event.end = OriginCalendarSource::OffsetDay(event.start, 1);
    event.all_day = true;
    event.event_url =
        GURL("https://calendar.google.com/calendar/event?eid=test");
    day.events.push_back(std::move(event));
    view_->OnEventsFetched(generation, std::move(day));
  }

  void Click(std::u16string_view name) {
    auto* button = FindButton(view_.get(), name);
    ASSERT_TRUE(button);
    ASSERT_TRUE(button->GetVisible());
    views::test::ButtonTestApi(button).NotifyDefaultMouseClick();
  }

  base::Time selected_day() const { return view_->selected_day_; }
  int generation() const { return view_->generation_; }

  std::unique_ptr<OriginCalendarWidgetView> view_;
};

TEST_F(OriginCalendarWidgetViewTest,
       NavigationReplacesTheDayAndIgnoresLateEvents) {
  const auto today = selected_day();
  const int today_generation = generation();
  DeliverDay(today_generation, u"Today's meeting");
  ASSERT_TRUE(FindButton(view_.get(), u"Today's meeting"));

  Click(u"Next day");
  EXPECT_EQ(selected_day(), OriginCalendarSource::OffsetDay(today, 1));
  EXPECT_FALSE(FindButton(view_.get(), u"Today's meeting"));

  DeliverDay(today_generation, u"Late result from today");
  EXPECT_FALSE(FindButton(view_.get(), u"Late result from today"));
  DeliverDay(generation(), u"Tomorrow's meeting");
  EXPECT_TRUE(FindButton(view_.get(), u"Tomorrow's meeting"));

  Click(u"Previous day");
  EXPECT_EQ(selected_day(), today);
  EXPECT_FALSE(FindButton(view_.get(), u"Tomorrow's meeting"));

  Click(u"Previous day");
  EXPECT_EQ(selected_day(), OriginCalendarSource::OffsetDay(today, -1));
  DeliverDay(generation(), u"Yesterday's meeting");
  Click(u"Today");
  EXPECT_EQ(selected_day(), today);
  EXPECT_FALSE(FindButton(view_.get(), u"Yesterday's meeting"));
  EXPECT_FALSE(FindButton(view_.get(), u"Today")->GetVisible());
}
