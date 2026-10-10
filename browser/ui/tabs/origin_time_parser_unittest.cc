// Copyright (c) 2026 The Brave Authors. All rights reserved.
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this file,
// You can obtain one at https://mozilla.org/MPL/2.0/.

#include "brave/browser/ui/tabs/origin_time_parser.h"

#include <array>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "base/check.h"
#include "base/i18n/time_formatting.h"
#include "base/strings/string_util.h"
#include "base/strings/utf_string_conversions.h"
#include "base/test/icu_test_util.h"
#include "base/time/time.h"
#include "build/build_config.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "third_party/abseil-cpp/absl/strings/str_format.h"

#if BUILDFLAG(IS_POSIX)
#include "base/test/scoped_libc_timezone_override.h"
#endif

// Unless a test pins a zone, it builds `now` from local fields and checks only
// the local fields of each result, so it holds in whatever zone the machine
// uses.

namespace origin_time_parser {

namespace {

base::Time LocalTime(int year,
                     int month,
                     int day,
                     int hour,
                     int minute,
                     int second = 0) {
  base::Time time;
  CHECK(base::Time::FromLocalExploded({.year = year,
                                       .month = month,
                                       .day_of_week = 0,
                                       .day_of_month = day,
                                       .hour = hour,
                                       .minute = minute,
                                       .second = second,
                                       .millisecond = 0},
                                      &time));
  return time;
}

// Wednesday, October 15, 2025, 10:15 AM.
base::Time Wednesday() {
  return LocalTime(2025, 10, 15, 10, 15);
}

// The local fields of `time`, like "Thu 2025-10-16 09:00".
std::string Fields(base::Time time) {
  static constexpr std::array<std::string_view, 7> kWeekdays = {
      "Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};
  base::Time::Exploded local;
  time.LocalExplode(&local);
  return absl::StrFormat("%s %04d-%02d-%02d %02d:%02d",
                         kWeekdays[local.day_of_week], local.year, local.month,
                         local.day_of_month, local.hour, local.minute);
}

// The local fields `input` resolves to at `now`, or "none".
std::string ParseFields(std::string_view input, base::Time now) {
  const std::optional<ParsedTime> parsed = Parse(base::UTF8ToUTF16(input), now);
  return parsed ? Fields(parsed->time) : "none";
}

std::u16string Describe(std::string_view input, base::Time now) {
  const std::optional<ParsedTime> parsed = Parse(base::UTF8ToUTF16(input), now);
  return parsed ? parsed->description : u"none";
}

// "Label: fields" for each preset.
std::vector<std::string> PresetFields(base::Time now) {
  std::vector<std::string> fields;
  for (const Preset& preset : GetPresets(now)) {
    fields.push_back(base::UTF16ToUTF8(preset.label) + ": " +
                     Fields(preset.when.time));
  }
  return fields;
}

}  // namespace

TEST(OriginTimeParserTest, Durations) {
  const base::Time now = Wednesday();
  EXPECT_EQ(ParseFields("in 20 minutes", now), "Wed 2025-10-15 10:35");
  EXPECT_EQ(ParseFields("in 20 min", now), "Wed 2025-10-15 10:35");
  EXPECT_EQ(ParseFields("30m", now), "Wed 2025-10-15 10:45");
  EXPECT_EQ(ParseFields("90 min", now), "Wed 2025-10-15 11:45");
  EXPECT_EQ(ParseFields("in 2 hours", now), "Wed 2025-10-15 12:15");
  EXPECT_EQ(ParseFields("in 2h", now), "Wed 2025-10-15 12:15");
  EXPECT_EQ(ParseFields("2h", now), "Wed 2025-10-15 12:15");
  EXPECT_EQ(ParseFields("an hour", now), "Wed 2025-10-15 11:15");
  EXPECT_EQ(ParseFields("in an hour", now), "Wed 2025-10-15 11:15");
  EXPECT_EQ(ParseFields("half an hour", now), "Wed 2025-10-15 10:45");
  EXPECT_EQ(ParseFields("in half an hour", now), "Wed 2025-10-15 10:45");
  EXPECT_EQ(ParseFields("1h30m", now), "Wed 2025-10-15 11:45");
  EXPECT_EQ(ParseFields("1 hour and 30 minutes", now), "Wed 2025-10-15 11:45");
  EXPECT_EQ(ParseFields("2 hours from now", now), "Wed 2025-10-15 12:15");

  // Days and weeks keep the time of day, or take one.
  EXPECT_EQ(ParseFields("in 3 days", now), "Sat 2025-10-18 10:15");
  EXPECT_EQ(ParseFields("in 1 week", now), "Wed 2025-10-22 10:15");
  EXPECT_EQ(ParseFields("a day and 2 hours", now), "Thu 2025-10-16 12:15");
  EXPECT_EQ(ParseFields("in 2 days at 9am", now), "Fri 2025-10-17 09:00");
}

TEST(OriginTimeParserTest, NamedTimes) {
  const base::Time now = Wednesday();
  EXPECT_EQ(ParseFields("later", now), "Wed 2025-10-15 13:15");
  EXPECT_EQ(ParseFields("later today", now), "Wed 2025-10-15 13:15");
  EXPECT_EQ(ParseFields("tonight", now), "Wed 2025-10-15 18:00");
  EXPECT_EQ(ParseFields("this evening", now), "Wed 2025-10-15 18:00");
  EXPECT_EQ(ParseFields("this afternoon", now), "Wed 2025-10-15 13:00");
  EXPECT_EQ(ParseFields("tomorrow", now), "Thu 2025-10-16 09:00");
  EXPECT_EQ(ParseFields("tomorrow morning", now), "Thu 2025-10-16 09:00");
  EXPECT_EQ(ParseFields("tomorrow afternoon", now), "Thu 2025-10-16 13:00");
  EXPECT_EQ(ParseFields("tomorrow evening", now), "Thu 2025-10-16 18:00");
  EXPECT_EQ(ParseFields("tomorrow night", now), "Thu 2025-10-16 20:00");
  EXPECT_EQ(ParseFields("this weekend", now), "Sat 2025-10-18 09:00");
  EXPECT_EQ(ParseFields("weekend", now), "Sat 2025-10-18 09:00");
  EXPECT_EQ(ParseFields("next weekend", now), "Sat 2025-10-25 09:00");
  EXPECT_EQ(ParseFields("next week", now), "Mon 2025-10-20 09:00");

  // A part of day alone is its next occurrence.
  EXPECT_EQ(ParseFields("evening", now), "Wed 2025-10-15 18:00");
  EXPECT_EQ(ParseFields("morning", now), "Thu 2025-10-16 09:00");
}

TEST(OriginTimeParserTest, TonightMovesToEightThenEnds) {
  EXPECT_EQ(ParseFields("tonight", LocalTime(2025, 10, 15, 17, 59)),
            "Wed 2025-10-15 18:00");
  EXPECT_EQ(ParseFields("tonight", LocalTime(2025, 10, 15, 19, 0)),
            "Wed 2025-10-15 20:00");
  EXPECT_EQ(ParseFields("this evening", LocalTime(2025, 10, 15, 19, 0)),
            "Wed 2025-10-15 20:00");
  EXPECT_EQ(ParseFields("tonight", LocalTime(2025, 10, 15, 20, 30)), "none");
  // A time still works.
  EXPECT_EQ(ParseFields("tonight at 11", LocalTime(2025, 10, 15, 20, 30)),
            "Wed 2025-10-15 23:00");
}

TEST(OriginTimeParserTest, WeekdaysAreTheFirstOneAfterToday) {
  const base::Time now = Wednesday();
  for (std::string_view input :
       {"friday", "fri", "Fri.", "next friday", "on friday"}) {
    EXPECT_EQ(ParseFields(input, now), "Fri 2025-10-17 09:00") << input;
  }
  EXPECT_EQ(ParseFields("thursday", now), "Thu 2025-10-16 09:00");
  EXPECT_EQ(ParseFields("thurs", now), "Thu 2025-10-16 09:00");
  EXPECT_EQ(ParseFields("sat", now), "Sat 2025-10-18 09:00");
  EXPECT_EQ(ParseFields("sunday", now), "Sun 2025-10-19 09:00");
  EXPECT_EQ(ParseFields("mon", now), "Mon 2025-10-20 09:00");
  EXPECT_EQ(ParseFields("tues", now), "Tue 2025-10-21 09:00");
  // Today's own weekday is a week out.
  EXPECT_EQ(ParseFields("wednesday", now), "Wed 2025-10-22 09:00");
  EXPECT_EQ(ParseFields("next wed", now), "Wed 2025-10-22 09:00");
}

TEST(OriginTimeParserTest, TimesWithoutADayAreTheNextOccurrence) {
  const base::Time now = Wednesday();
  for (std::string_view input :
       {"5pm", "5 pm", "at 5pm", "5 p.m.", "5p", "17:00", "at 5"}) {
    EXPECT_EQ(ParseFields(input, now), "Wed 2025-10-15 17:00") << input;
  }
  EXPECT_EQ(ParseFields("9:30pm", now), "Wed 2025-10-15 21:30");
  EXPECT_EQ(ParseFields("9.30pm", now), "Wed 2025-10-15 21:30");
  EXPECT_EQ(ParseFields("930pm", now), "Wed 2025-10-15 21:30");
  EXPECT_EQ(ParseFields("21:00", now), "Wed 2025-10-15 21:00");
  EXPECT_EQ(ParseFields("noon", now), "Wed 2025-10-15 12:00");
  EXPECT_EQ(ParseFields("12pm", now), "Wed 2025-10-15 12:00");
  EXPECT_EQ(ParseFields("10:16am", now), "Wed 2025-10-15 10:16");

  // Already past today.
  EXPECT_EQ(ParseFields("9am", now), "Thu 2025-10-16 09:00");
  EXPECT_EQ(ParseFields("9 am", now), "Thu 2025-10-16 09:00");
  EXPECT_EQ(ParseFields("10:15am", now), "Thu 2025-10-16 10:15");

  // Midnight ends today; 12am has already started it.
  EXPECT_EQ(ParseFields("midnight", now), "Thu 2025-10-16 00:00");
  EXPECT_EQ(ParseFields("12am", now), "Thu 2025-10-16 00:00");
}

TEST(OriginTimeParserTest, HoursWithoutAmPm) {
  const base::Time now = Wednesday();
  // 1-7 are PM, 8-11 AM, and 12 is noon.
  EXPECT_EQ(ParseFields("at 7", now), "Wed 2025-10-15 19:00");
  EXPECT_EQ(ParseFields("at 3:30", now), "Wed 2025-10-15 15:30");
  EXPECT_EQ(ParseFields("at 11", now), "Wed 2025-10-15 11:00");
  EXPECT_EQ(ParseFields("at 12:30", now), "Wed 2025-10-15 12:30");
  EXPECT_EQ(ParseFields("at 8", now), "Thu 2025-10-16 08:00");
  // A leading zero means 24-hour.
  EXPECT_EQ(ParseFields("at 07:00", now), "Thu 2025-10-16 07:00");
  // A part of day decides.
  EXPECT_EQ(ParseFields("tomorrow morning at 7", now), "Thu 2025-10-16 07:00");
  EXPECT_EQ(ParseFields("tomorrow evening at 8", now), "Thu 2025-10-16 20:00");
  EXPECT_EQ(ParseFields("friday afternoon 2", now), "Fri 2025-10-17 14:00");
  EXPECT_EQ(ParseFields("tonight at 9", now), "Wed 2025-10-15 21:00");
  // A lone number needs a day.
  EXPECT_EQ(ParseFields("tomorrow 9", now), "Thu 2025-10-16 09:00");
  EXPECT_EQ(ParseFields("9", now), "none");
}

TEST(OriginTimeParserTest, DaysWithTimes) {
  const base::Time now = Wednesday();
  EXPECT_EQ(ParseFields("tomorrow 9am", now), "Thu 2025-10-16 09:00");
  EXPECT_EQ(ParseFields("3pm tomorrow", now), "Thu 2025-10-16 15:00");
  EXPECT_EQ(ParseFields("tomorrow at noon", now), "Thu 2025-10-16 12:00");
  EXPECT_EQ(ParseFields("tomorrow at 5", now), "Thu 2025-10-16 17:00");
  EXPECT_EQ(ParseFields("fri 3pm", now), "Fri 2025-10-17 15:00");
  EXPECT_EQ(ParseFields("on friday at 3pm", now), "Fri 2025-10-17 15:00");
  EXPECT_EQ(ParseFields("monday at 10:30", now), "Mon 2025-10-20 10:30");
  EXPECT_EQ(ParseFields("next week 2pm", now), "Mon 2025-10-20 14:00");
  EXPECT_EQ(ParseFields("this weekend at 10am", now), "Sat 2025-10-18 10:00");
  EXPECT_EQ(ParseFields("today 5pm", now), "Wed 2025-10-15 17:00");
  EXPECT_EQ(ParseFields("tomorrow midnight", now), "Fri 2025-10-17 00:00");
  EXPECT_EQ(ParseFields("tomorrow 12am", now), "Thu 2025-10-16 00:00");
}

TEST(OriginTimeParserTest, Dates) {
  const base::Time now = Wednesday();
  for (std::string_view input : {"oct 20", "20 oct", "October 20th",
                                 "20th of october", "10/20", "on oct 20"}) {
    EXPECT_EQ(ParseFields(input, now), "Mon 2025-10-20 09:00") << input;
  }
  EXPECT_EQ(ParseFields("oct 20 3pm", now), "Mon 2025-10-20 15:00");
  EXPECT_EQ(ParseFields("10/20 at 15:00", now), "Mon 2025-10-20 15:00");
  EXPECT_EQ(ParseFields("oct 20 1030am", now), "Mon 2025-10-20 10:30");
  EXPECT_EQ(ParseFields("dec 25", now), "Thu 2025-12-25 09:00");
  EXPECT_EQ(ParseFields("oct 15 3pm", now), "Wed 2025-10-15 15:00");

  // Already past this year.
  EXPECT_EQ(ParseFields("jan 5", now), "Mon 2026-01-05 09:00");
  EXPECT_EQ(ParseFields("oct 14", now), "Wed 2026-10-14 09:00");

  EXPECT_EQ(ParseFields("10/20/2026", now), "Tue 2026-10-20 09:00");
  EXPECT_EQ(ParseFields("10/20/26", now), "Tue 2026-10-20 09:00");
  EXPECT_EQ(ParseFields("Oct 20, 2026", now), "Tue 2026-10-20 09:00");

  EXPECT_EQ(ParseFields("feb 30", now), "none");
  EXPECT_EQ(ParseFields("13/1", now), "none");
  EXPECT_EQ(ParseFields("10/0", now), "none");
}

TEST(OriginTimeParserTest, IgnoresCaseAndSpacing) {
  const base::Time now = Wednesday();
  EXPECT_EQ(ParseFields("  Tomorrow   9AM ", now), "Thu 2025-10-16 09:00");
  EXPECT_EQ(ParseFields("IN 2 HOURS", now), "Wed 2025-10-15 12:15");
  EXPECT_EQ(ParseFields("Next Friday", now), "Fri 2025-10-17 09:00");
  EXPECT_EQ(ParseFields("fri, 3 PM", now), "Fri 2025-10-17 15:00");
  EXPECT_EQ(ParseFields("\tlater\n", now), "Wed 2025-10-15 13:15");
  EXPECT_EQ(ParseFields("9 A.M. tomorrow", now), "Thu 2025-10-16 09:00");
}

TEST(OriginTimeParserTest, RejectsGarbage) {
  const base::Time now = Wednesday();
  for (std::string_view input : {"",
                                 "   ",
                                 "banana",
                                 "in",
                                 "at",
                                 "next",
                                 "this",
                                 "half",
                                 "5",
                                 "in 20",
                                 "yesterday",
                                 "tomorrow tomorrow",
                                 "fri fri",
                                 "monday 3pm tuesday",
                                 "5pm 6pm",
                                 "9:75",
                                 "25:00",
                                 "13pm",
                                 "0am",
                                 "9:5pm",
                                 "in 1.5 hours",
                                 "2 hours ago",
                                 "tomorrow!",
                                 "99999 minutes",
                                 "in 9999 weeks"}) {
    EXPECT_EQ(ParseFields(input, now), "none") << input;
  }
  EXPECT_FALSE(Parse(u"mañana", now));
}

TEST(OriginTimeParserTest, RejectsPastTimes) {
  const base::Time now = Wednesday();
  for (std::string_view input : {"this morning", "today", "today 9am", "oct 15",
                                 "10/14/2025", "oct 1 2024", "in 0 minutes"}) {
    EXPECT_EQ(ParseFields(input, now), "none") << input;
  }
}

TEST(OriginTimeParserTest, Descriptions) {
  const base::test::ScopedRestoreICUDefaultLocale locale("en_US");
  const base::Time now = Wednesday();

  // Durations under a day count from now.
  EXPECT_EQ(Describe("in 1 min", now), u"In 1 minute");
  EXPECT_EQ(Describe("2h", now), u"In 2 hours");
  EXPECT_EQ(Describe("an hour", now), u"In 1 hour");
  EXPECT_EQ(Describe("90 min", now), u"In 1 hour 30 minutes");

  // Anything else names the day.
  EXPECT_EQ(
      Describe("5pm", now),
      u"Today " + base::TimeFormatTimeOfDay(LocalTime(2025, 10, 15, 17, 0)));
  EXPECT_EQ(
      Describe("later", now),
      u"Today " + base::TimeFormatTimeOfDay(LocalTime(2025, 10, 15, 13, 15)));
  EXPECT_EQ(
      Describe("tomorrow", now),
      u"Tomorrow " + base::TimeFormatTimeOfDay(LocalTime(2025, 10, 16, 9, 0)));
  EXPECT_EQ(Describe("in 24 hours", now),
            u"Tomorrow " + base::TimeFormatTimeOfDay(now + base::Hours(24)));

  const std::u16string next_week = Describe("next week", now);
  EXPECT_TRUE(base::StartsWith(next_week, u"Mon, Oct 20"))
      << base::UTF16ToUTF8(next_week);
  EXPECT_NE(next_week.find(u"9:00"), std::u16string::npos)
      << base::UTF16ToUTF8(next_week);
  EXPECT_EQ(next_week.find(u"2025"), std::u16string::npos)
      << base::UTF16ToUTF8(next_week);
  // The year shows once it differs.
  EXPECT_NE(Describe("jan 5", now).find(u"2026"), std::u16string::npos);
}

TEST(OriginTimeParserTest, Presets) {
  EXPECT_EQ(PresetFields(Wednesday()), (std::vector<std::string>{
                                           "Later today: Wed 2025-10-15 13:15",
                                           "This evening: Wed 2025-10-15 18:00",
                                           "Tomorrow: Thu 2025-10-16 09:00",
                                           "This weekend: Sat 2025-10-18 09:00",
                                           "Next week: Mon 2025-10-20 09:00",
                                       }));
}

TEST(OriginTimeParserTest, PresetsDropWhatNoLongerFits) {
  // No "This evening" from 5 PM on.
  EXPECT_EQ(PresetFields(LocalTime(2025, 10, 15, 17, 30)),
            (std::vector<std::string>{
                "Later today: Wed 2025-10-15 20:30",
                "Tomorrow: Thu 2025-10-16 09:00",
                "This weekend: Sat 2025-10-18 09:00",
                "Next week: Mon 2025-10-20 09:00",
            }));
  // No "Later today" once that is tomorrow.
  EXPECT_EQ(PresetFields(LocalTime(2025, 10, 15, 22, 0)),
            (std::vector<std::string>{
                "Tomorrow: Thu 2025-10-16 09:00",
                "This weekend: Sat 2025-10-18 09:00",
                "Next week: Mon 2025-10-20 09:00",
            }));
}

TEST(OriginTimeParserTest, PresetsOnAWeekendOfferTheNextOne) {
  EXPECT_EQ(PresetFields(LocalTime(2025, 10, 18, 10, 0)),
            (std::vector<std::string>{
                "Later today: Sat 2025-10-18 13:00",
                "This evening: Sat 2025-10-18 18:00",
                "Tomorrow: Sun 2025-10-19 09:00",
                "Next weekend: Sat 2025-10-25 09:00",
                "Next week: Mon 2025-10-20 09:00",
            }));
  EXPECT_EQ(PresetFields(LocalTime(2025, 10, 19, 10, 0)),
            (std::vector<std::string>{
                "Later today: Sun 2025-10-19 13:00",
                "This evening: Sun 2025-10-19 18:00",
                "Tomorrow: Mon 2025-10-20 09:00",
                "Next weekend: Sat 2025-10-25 09:00",
                "Next week: Mon 2025-10-20 09:00",
            }));
}

TEST(OriginTimeParserTest, PresetLabelsParseToTheirTimes) {
  for (base::Time now : {Wednesday(), LocalTime(2025, 10, 18, 10, 0)}) {
    for (const Preset& preset : GetPresets(now)) {
      const std::optional<ParsedTime> parsed = Parse(preset.label, now);
      ASSERT_TRUE(parsed) << base::UTF16ToUTF8(preset.label);
      EXPECT_EQ(parsed->time, preset.when.time)
          << base::UTF16ToUTF8(preset.label);
      EXPECT_EQ(parsed->description, preset.when.description);
    }
  }
}

TEST(OriginTimeParserTest, LaterRoundsUpToTheQuarterHour) {
  EXPECT_EQ(ParseFields("later", LocalTime(2025, 10, 15, 10, 0)),
            "Wed 2025-10-15 13:00");
  EXPECT_EQ(ParseFields("later", LocalTime(2025, 10, 15, 10, 1)),
            "Wed 2025-10-15 13:15");
  EXPECT_EQ(ParseFields("later", LocalTime(2025, 10, 15, 10, 14, 59)),
            "Wed 2025-10-15 13:15");
  EXPECT_EQ(ParseFields("later", LocalTime(2025, 10, 15, 10, 15, 30)),
            "Wed 2025-10-15 13:30");
  // Typed late at night, it can be tomorrow.
  EXPECT_EQ(ParseFields("later", LocalTime(2025, 10, 15, 22, 50)),
            "Thu 2025-10-16 02:00");
}

// US clocks change on 2025-11-02 and 2026-03-08. In other zones these are
// ordinary days, and the checks still hold since they only read local fields.
TEST(OriginTimeParserTest, DayOffsetsKeepTheTimeOfDayAcrossDst) {
  const base::Time fall = LocalTime(2025, 11, 1, 10, 15);
  EXPECT_EQ(ParseFields("tomorrow", fall), "Sun 2025-11-02 09:00");
  EXPECT_EQ(ParseFields("in 1 day", fall), "Sun 2025-11-02 10:15");
  EXPECT_EQ(ParseFields("in 2 days", fall), "Mon 2025-11-03 10:15");
  EXPECT_EQ(ParseFields("next week", fall), "Mon 2025-11-03 09:00");

  const base::Time spring = LocalTime(2026, 3, 7, 10, 15);
  EXPECT_EQ(ParseFields("in 1 day", spring), "Sun 2026-03-08 10:15");
  EXPECT_EQ(ParseFields("tomorrow 9am", spring), "Sun 2026-03-08 09:00");
  EXPECT_EQ(ParseFields("mon", spring), "Mon 2026-03-09 09:00");
}

#if BUILDFLAG(IS_POSIX)
// Pins New York, where 2025-11-02 is 25 hours long and 2026-03-08 is 23.
// Windows reads its zone from the system rather than TZ, so it is skipped.
TEST(OriginTimeParserTest, DstInNewYork) {
  const base::test::ScopedLibcTimezoneOverride libc_zone("America/New_York");
  const base::test::ScopedRestoreDefaultTimezone icu_zone("America/New_York");

  const base::Time fall = LocalTime(2025, 11, 1, 10, 15);
  const std::optional<ParsedTime> fall_day = Parse(u"in 1 day", fall);
  ASSERT_TRUE(fall_day);
  EXPECT_EQ(Fields(fall_day->time), "Sun 2025-11-02 10:15");
  EXPECT_EQ(fall_day->time - fall, base::Hours(25));
  // Hours are elapsed time, so the clock reads an hour less.
  EXPECT_EQ(ParseFields("in 24 hours", fall), "Sun 2025-11-02 09:15");

  const base::Time spring = LocalTime(2026, 3, 7, 10, 15);
  const std::optional<ParsedTime> spring_day = Parse(u"in 1 day", spring);
  ASSERT_TRUE(spring_day);
  EXPECT_EQ(Fields(spring_day->time), "Sun 2026-03-08 10:15");
  EXPECT_EQ(spring_day->time - spring, base::Hours(23));
  // 2:30 AM does not exist that night.
  EXPECT_EQ(ParseFields("tomorrow 2:30am", spring), "Sun 2026-03-08 03:30");
}
#endif  // BUILDFLAG(IS_POSIX)

}  // namespace origin_time_parser
