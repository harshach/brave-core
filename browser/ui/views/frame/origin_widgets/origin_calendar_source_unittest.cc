// Copyright (c) 2026 The Brave Authors. All rights reserved.
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this file,
// You can obtain one at https://mozilla.org/MPL/2.0/.

#include "brave/browser/ui/views/frame/origin_widgets/origin_calendar_source.h"

#include "base/check.h"
#include "base/test/icu_test_util.h"
#include "base/test/task_environment.h"
#include "base/test/test_future.h"
#include "net/base/url_util.h"
#include "services/network/public/cpp/resource_request.h"
#include "services/network/public/cpp/weak_wrapper_shared_url_loader_factory.h"
#include "services/network/test/test_url_loader_factory.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace {

base::Time Time(const char* text) {
  base::Time value;
  CHECK(base::Time::FromUTCString(text, &value));
  return value;
}
constexpr char kEventsUrl[] =
    "https://www.googleapis.com/calendar/v3/calendars/primary/events";
constexpr char kEvent[] = R"({"id":"event", "summary":"Design review",
  "start":{"dateTime":"2026-09-08T09:00:00-07:00"},
  "end":{"dateTime":"2026-09-08T10:00:00-07:00"},
  "htmlLink":"https://calendar.google.com/calendar/event?eid=test",
  "hangoutLink":"https://meet.google.com/abc-defg-hij",
  "location":"Design studio"})";

class OriginCalendarSourceTest : public testing::Test {
 protected:
  void Reply(std::string_view body, net::HttpStatusCode status = net::HTTP_OK) {
    ASSERT_TRUE(factory_.SimulateResponseForPendingRequest(
        kEventsUrl, body, status,
        static_cast<network::TestURLLoaderFactory::ResponseMatchFlags>(
            network::TestURLLoaderFactory::kUrlMatchPrefix |
            network::TestURLLoaderFactory::kMostRecentMatch |
            network::TestURLLoaderFactory::kWaitForRequest)));
  }
  base::test::TaskEnvironment task_environment_;
  base::test::ScopedRestoreDefaultTimezone timezone_{"America/Los_Angeles"};
  network::TestURLLoaderFactory factory_;
  OriginCalendarSource source_{factory_.GetSafeWeakWrapper()};
  const base::Time day_ = Time("2026-09-08T20:00:00Z");
};

TEST_F(OriginCalendarSourceTest,
       DayNavigationRespectsDaylightSavingAndYearRollover) {
  const auto spring =
      OriginCalendarSource::StartOfDay(Time("2026-03-08T12:00:00Z"));
  EXPECT_EQ(spring, Time("2026-03-08T08:00:00Z"));
  EXPECT_EQ(OriginCalendarSource::OffsetDay(spring, 1) - spring,
            base::Hours(23));
  const auto fall =
      OriginCalendarSource::StartOfDay(Time("2026-11-01T12:00:00Z"));
  EXPECT_EQ(OriginCalendarSource::OffsetDay(fall, 1) - fall, base::Hours(25));
  const auto january =
      OriginCalendarSource::StartOfDay(Time("2027-01-01T12:00:00Z"));
  EXPECT_EQ(OriginCalendarSource::DayUrl(
                OriginCalendarSource::OffsetDay(january, -1)),
            GURL("https://calendar.google.com/calendar/u/0/r/day/2026/12/31"));
}

TEST_F(OriginCalendarSourceTest,
       IncludesPastEventsAndExpandedRecurringInstances) {
  const auto result = OriginCalendarSource::ParseGoogleEvents(
      R"({"items":[{"id":"recurring_instance", "summary":"Daily standup",
        "recurringEventId":"series",
        "start":{"dateTime":"2026-09-08T09:00:00-07:00"},
        "end":{"dateTime":"2026-09-08T09:30:00-07:00"}}]})",
      day_);
  ASSERT_TRUE(result.has_value());
  ASSERT_EQ(result->events.size(), 1u);
  EXPECT_EQ(result->events[0].title, u"Daily standup");
  EXPECT_EQ(result->events[0].start, Time("2026-09-08T16:00:00Z"));
}

TEST_F(OriginCalendarSourceTest,
       AllDayAndOvernightEventsUseExclusiveDayBounds) {
  const auto result = OriginCalendarSource::ParseGoogleEvents(R"({"items":[
    {"id":"all_day", "start":{"date":"2026-09-08"},
     "end":{"date":"2026-09-09"}},
    {"id":"yesterday", "start":{"date":"2026-09-07"},
     "end":{"date":"2026-09-08"}},
    {"id":"tomorrow", "start":{"date":"2026-09-09"},
     "end":{"date":"2026-09-10"}},
    {"id":"overnight", "start":{"dateTime":"2026-09-07T23:00:00-07:00"},
     "end":{"dateTime":"2026-09-08T01:00:00-07:00"}}
  ]})",
                                                              day_);
  ASSERT_TRUE(result.has_value());
  ASSERT_EQ(result->events.size(), 2u);
  EXPECT_TRUE(result->events[0].all_day);
  EXPECT_EQ(result->events[0].start, Time("2026-09-08T07:00:00Z"));
  EXPECT_EQ(result->events[1].id, "overnight");
}

TEST_F(OriginCalendarSourceTest, DropsCancelledDeclinedAndInvalidEvents) {
  const auto result = OriginCalendarSource::ParseGoogleEvents(R"({"items":[
    {"id":"cancelled", "status":"cancelled", "start":{"date":"2026-09-08"},
     "end":{"date":"2026-09-09"}},
    {"id":"declined", "attendees":[{"self":true,"responseStatus":"declined"}],
     "start":{"date":"2026-09-08"}, "end":{"date":"2026-09-09"}},
    {"id":"bad", "start":{"date":"2026-09-08"}, "end":{"date":"2026-09-07"}},
    {"id":"missing_start"}
  ]})",
                                                              day_);
  ASSERT_TRUE(result.has_value());
  EXPECT_TRUE(result->events.empty());
  EXPECT_FALSE(
      OriginCalendarSource::ParseGoogleEvents("not json", day_).has_value());
  EXPECT_FALSE(OriginCalendarSource::ParseGoogleEvents("{}", day_).has_value());
}

TEST_F(OriginCalendarSourceTest, EventDetailsUseSafeLinks) {
  const auto result = OriginCalendarSource::ParseGoogleEvents(
      std::string("{\"items\":[") + kEvent + "]}", day_);
  ASSERT_TRUE(result.has_value());
  ASSERT_EQ(result->events.size(), 1u);
  EXPECT_EQ(result->events[0].location, u"Design studio");
  EXPECT_EQ(result->events[0].join_url,
            GURL("https://meet.google.com/abc-defg-hij"));
  const auto unsafe = OriginCalendarSource::ParseGoogleEvents(R"JSON({"items":[
    {"id":"bad_urls", "start":{"date":"2026-09-08"}, "end":{"date":"2026-09-09"},
     "htmlLink":"https://google.com.evil.test/event", "hangoutLink":"javascript:alert(1)"}
  ]})JSON",
                                                              day_);
  ASSERT_TRUE(unsafe.has_value());
  ASSERT_EQ(unsafe->events.size(), 1u);
  EXPECT_TRUE(unsafe->events[0].event_url.is_empty());
  EXPECT_TRUE(unsafe->events[0].join_url.is_empty());
}

TEST_F(OriginCalendarSourceTest, FetchesEveryPageWithAReadOnlyDayQuery) {
  base::test::TestFuture<OriginCalendarSource::Result> future;
  source_.Fetch("test-access-token", day_, future.GetCallback());
  factory_.WaitForRequest(GURL(kEventsUrl),
                          network::TestURLLoaderFactory::kUrlMatchPrefix);
  const auto& request = factory_.GetPendingRequest(0)->request;
  std::string value;
  ASSERT_TRUE(net::GetValueForKeyInQuery(request.url, "timeMin", &value));
  EXPECT_EQ(value, "2026-09-08T07:00:00.000Z");
  ASSERT_TRUE(net::GetValueForKeyInQuery(request.url, "timeMax", &value));
  EXPECT_EQ(value, "2026-09-09T07:00:00.000Z");
  ASSERT_TRUE(net::GetValueForKeyInQuery(request.url, "singleEvents", &value));
  EXPECT_EQ(value, "true");
  EXPECT_EQ(request.credentials_mode, network::mojom::CredentialsMode::kOmit);
  EXPECT_EQ(request.redirect_mode, network::mojom::RedirectMode::kError);
  EXPECT_EQ(request.headers.GetHeader("Authorization"),
            "Bearer test-access-token");
  EXPECT_EQ(request.url.spec().find("test-access-token"), std::string::npos);

  Reply(std::string("{\"nextPageToken\":\"page2\",\"items\":[") + kEvent +
        "]}");
  factory_.WaitForRequest(GURL(kEventsUrl),
                          network::TestURLLoaderFactory::kUrlMatchPrefix);
  EXPECT_FALSE(future.IsReady());
  ASSERT_TRUE(net::GetValueForKeyInQuery(
      factory_.GetPendingRequest(0)->request.url, "pageToken", &value));
  EXPECT_EQ(value, "page2");
  Reply(R"({"items":[{"id":"all_day", "start":{"date":"2026-09-08"},
                         "end":{"date":"2026-09-09"}}]})");
  auto result = future.Take();
  ASSERT_TRUE(result.has_value());
  ASSERT_EQ(result->events.size(), 2u);
  EXPECT_TRUE(result->events.front().all_day);
  EXPECT_EQ(result->events.back().id, "event");
}

TEST_F(OriginCalendarSourceTest, NetworkErrorsDoNotLookLikeEmptyCalendars) {
  for (const auto& [status, expected] :
       {std::pair(net::HTTP_UNAUTHORIZED,
                  OriginCalendarSource::Error::kAuthentication),
        std::pair(net::HTTP_FORBIDDEN,
                  OriginCalendarSource::Error::kPermission),
        std::pair(net::HTTP_TOO_MANY_REQUESTS,
                  OriginCalendarSource::Error::kRateLimited),
        std::pair(net::HTTP_SERVICE_UNAVAILABLE,
                  OriginCalendarSource::Error::kNetwork)}) {
    base::test::TestFuture<OriginCalendarSource::Result> future;
    source_.Fetch("test-token", day_, future.GetCallback());
    Reply("{}", status);
    auto result = future.Take();
    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error(), expected);
  }
}

TEST_F(OriginCalendarSourceTest, RejectsRepeatingPaginationTokens) {
  base::test::TestFuture<OriginCalendarSource::Result> future;
  source_.Fetch("test-token", day_, future.GetCallback());
  Reply(R"({"items":[],"nextPageToken":"repeat"})");
  Reply(R"({"items":[],"nextPageToken":"repeat"})");
  auto result = future.Take();
  ASSERT_FALSE(result.has_value());
  EXPECT_EQ(result.error(), OriginCalendarSource::Error::kInvalidResponse);
}

TEST_F(OriginCalendarSourceTest, SelectingAnotherDayCancelsTheOldRequest) {
  base::test::TestFuture<OriginCalendarSource::Result> old_day;
  base::test::TestFuture<OriginCalendarSource::Result> next_day;
  source_.Fetch("test-token", day_, old_day.GetCallback());
  source_.Fetch("test-token", OriginCalendarSource::OffsetDay(day_, 1),
                next_day.GetCallback());
  Reply("{\"items\":[]}");
  EXPECT_TRUE(next_day.Get().has_value());
  EXPECT_FALSE(old_day.IsReady());
}

}  // namespace
