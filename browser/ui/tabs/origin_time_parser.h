// Copyright (c) 2026 The Brave Authors. All rights reserved.
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this file,
// You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef BRAVE_BROWSER_UI_TABS_ORIGIN_TIME_PARSER_H_
#define BRAVE_BROWSER_UI_TABS_ORIGIN_TIME_PARSER_H_

#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "base/time/time.h"

// Reads times typed in plain English, such as "in 2h", "fri 3pm" or
// "tomorrow evening", for snoozing pages and reminders. Calendar math is done
// in local time, so day offsets keep the time of day across DST changes.
//
// Input is case-insensitive. Extra spaces, commas and dots are ignored, except
// that "9.30" reads as "9:30".
//  - Durations: "in 20 min", "2h", "90 minutes", "an hour", "half an hour",
//    "1h30m", "in 3 days", "1 week from now". Minutes and hours are elapsed
//    time; days and weeks keep the current time of day, or take a time:
//    "in 2 days at 9am".
//  - "later", "later today": three hours from now, rounded up to the next
//    quarter hour.
//  - Days: "today", "tomorrow"; weekdays ("friday", "fri" and "next friday"
//    all mean the first Friday after today, so "wed" on a Wednesday is a week
//    out); "this weekend" (the first Saturday after today); "next weekend"
//    (the Saturday after that, or the first one if today is a weekend day);
//    "next week" (the first Monday after today); dates ("oct 14", "14 oct",
//    "october 14th", "10/14", "10/14/2026"). A date without a year that has
//    already passed this year means next year.
//  - Parts of day, alone or with a day: morning 9 AM, afternoon 1 PM,
//    evening 6 PM, night 8 PM. "this morning" and "this afternoon" are today.
//    "tonight" and "this evening" are 6 PM, or 8 PM once 6 PM has passed.
//  - Times, alone or with a day: "9am", "9:30 p.m.", "21:00", "noon",
//    "midnight" (the end of the day), optionally after "at". Without am/pm,
//    1-7 is PM and 8-11 is AM unless a part of day says otherwise
//    ("tomorrow morning at 7"); 12 is noon; "07:30" and "17:30" are 24-hour.
//    A lone number such as "9" is a time only after "at" or with a day.
//  - A day alone means 9 AM. A time without a day is today if still ahead,
//    otherwise tomorrow.
namespace origin_time_parser {

struct ParsedTime {
  base::Time time;
  // Short, human description of the resolved time relative to `now`, e.g.
  // u"Today 6:00 PM", u"Tomorrow 9:00 AM", u"Mon, Oct 13, 9:00 AM",
  // u"In 2 hours". Durations under a day read "In ..."; anything else names
  // the day and a locale-formatted time, with the year when it differs.
  std::u16string description;
};

// Parses `input` relative to `now` in the local time zone. Returns nullopt for
// empty, unrecognized, or past times.
std::optional<ParsedTime> Parse(std::u16string_view input, base::Time now);

struct Preset {
  std::u16string label;
  ParsedTime when;
};

// The fixed choices offered before the user types, in order:
// "Later today" (as "later"; omitted once that falls on a later day),
// "This evening" (today 6 PM; omitted from 5 PM on), "Tomorrow" (9 AM),
// "This weekend" (the first Saturday after today, 9 AM; labeled
// "Next weekend" on Saturday and Sunday) and "Next week" (the first Monday
// after today, 9 AM). Each label parses back to its own time.
std::vector<Preset> GetPresets(base::Time now);

}  // namespace origin_time_parser

#endif  // BRAVE_BROWSER_UI_TABS_ORIGIN_TIME_PARSER_H_
