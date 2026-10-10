// Copyright (c) 2026 The Brave Authors. All rights reserved.
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this file,
// You can obtain one at https://mozilla.org/MPL/2.0/.

#include "brave/browser/ui/tabs/origin_time_parser.h"

#include <algorithm>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "base/containers/fixed_flat_map.h"
#include "base/containers/map_util.h"
#include "base/i18n/icubridge/date_time_formatter.h"
#include "base/i18n/icubridge/icu_bridge.h"
#include "base/i18n/time_formatting.h"
#include "base/notreached.h"
#include "base/strings/strcat.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/string_util.h"
#include "base/strings/utf_string_conversions.h"

namespace origin_time_parser {

namespace {

// Nothing resolves further out than this.
constexpr int kMaxDays = 3650;
constexpr int kMinutesPerDay = 24 * 60;

// The hour a day means when no time is given.
constexpr int kDefaultHour = 9;

// base::Time::Exploded::day_of_week values.
constexpr int kSunday = 0;
constexpr int kMonday = 1;
constexpr int kSaturday = 6;

enum class Unit { kMinute, kHour, kDay, kWeek };

// kTonight is "tonight" and "this evening": 6 PM, or 8 PM once that passes.
enum class Part { kMorning, kAfternoon, kEvening, kNight, kTonight };

constexpr auto kUnits = base::MakeFixedFlatMap<std::string_view, Unit>({
    {"m", Unit::kMinute},
    {"min", Unit::kMinute},
    {"mins", Unit::kMinute},
    {"minute", Unit::kMinute},
    {"minutes", Unit::kMinute},
    {"h", Unit::kHour},
    {"hr", Unit::kHour},
    {"hrs", Unit::kHour},
    {"hour", Unit::kHour},
    {"hours", Unit::kHour},
    {"d", Unit::kDay},
    {"day", Unit::kDay},
    {"days", Unit::kDay},
    {"w", Unit::kWeek},
    {"wk", Unit::kWeek},
    {"wks", Unit::kWeek},
    {"week", Unit::kWeek},
    {"weeks", Unit::kWeek},
});

constexpr auto kParts = base::MakeFixedFlatMap<std::string_view, Part>({
    {"morning", Part::kMorning},
    {"afternoon", Part::kAfternoon},
    {"evening", Part::kEvening},
    {"night", Part::kNight},
});

constexpr auto kWeekdays = base::MakeFixedFlatMap<std::string_view, int>({
    {"sunday", 0},
    {"sun", 0},
    {"monday", 1},
    {"mon", 1},
    {"tuesday", 2},
    {"tue", 2},
    {"tues", 2},
    {"wednesday", 3},
    {"wed", 3},
    {"weds", 3},
    {"thursday", 4},
    {"thu", 4},
    {"thur", 4},
    {"thurs", 4},
    {"friday", 5},
    {"fri", 5},
    {"saturday", 6},
    {"sat", 6},
});

constexpr auto kMonths = base::MakeFixedFlatMap<std::string_view, int>({
    {"january", 1},   {"jan", 1},    {"february", 2},  {"feb", 2},
    {"march", 3},     {"mar", 3},    {"april", 4},     {"apr", 4},
    {"may", 5},       {"june", 6},   {"jun", 6},       {"july", 7},
    {"jul", 7},       {"august", 8}, {"aug", 8},       {"september", 9},
    {"sep", 9},       {"sept", 9},   {"october", 10},  {"oct", 10},
    {"november", 11}, {"nov", 11},   {"december", 12}, {"dec", 12},
});

struct Date {
  int year;
  int month;  // 1-12
  int day;    // 1-31
};

// Local days are numbered from 1970-01-01. Day offsets are done on these
// numbers instead of by adding 24 hours, which would drift by an hour across a
// DST change. From http://howardhinnant.github.io/date_algorithms.html.
int DaysFromCivil(const Date& date) {
  const int year = date.year - (date.month <= 2 ? 1 : 0);
  const int era = (year >= 0 ? year : year - 399) / 400;
  const int year_of_era = year - era * 400;
  const int day_of_year =
      (153 * (date.month > 2 ? date.month - 3 : date.month + 9) + 2) / 5 +
      date.day - 1;
  const int day_of_era =
      year_of_era * 365 + year_of_era / 4 - year_of_era / 100 + day_of_year;
  return era * 146097 + day_of_era - 719468;
}

Date CivilFromDays(int days) {
  days += 719468;
  const int era = (days >= 0 ? days : days - 146096) / 146097;
  const int day_of_era = days - era * 146097;
  const int year_of_era = (day_of_era - day_of_era / 1460 + day_of_era / 36524 -
                           day_of_era / 146096) /
                          365;
  const int day_of_year =
      day_of_era - (365 * year_of_era + year_of_era / 4 - year_of_era / 100);
  // Counting months from March puts the leap day last.
  const int march_month = (5 * day_of_year + 2) / 153;
  const int month = march_month < 10 ? march_month + 3 : march_month - 9;
  return {.year = year_of_era + era * 400 + (month <= 2 ? 1 : 0),
          .month = month,
          .day = day_of_year - (153 * march_month + 2) / 5 + 1};
}

int Weekday(int day) {
  // 1970-01-01 was a Thursday.
  return ((day + 4) % 7 + 7) % 7;
}

bool IsWeekend(int day) {
  const int weekday = Weekday(day);
  return weekday == kSaturday || weekday == kSunday;
}

// The first `weekday` after `day`, a week out if `day` is one.
int NextWeekday(int day, int weekday) {
  return day + (weekday - Weekday(day) + 6) % 7 + 1;
}

int DaysInMonth(int year, int month) {
  if (month == 2) {
    const bool leap = (year % 4 == 0 && year % 100 != 0) || year % 400 == 0;
    return leap ? 29 : 28;
  }
  return month == 4 || month == 6 || month == 9 || month == 11 ? 30 : 31;
}

int LocalDay(const base::Time::Exploded& local) {
  return DaysFromCivil(
      {.year = local.year, .month = local.month, .day = local.day_of_month});
}

int LocalDay(base::Time time) {
  base::Time::Exploded local;
  time.LocalExplode(&local);
  return LocalDay(local);
}

struct TimeOfDay {
  int hour = 0;
  int minute = 0;
  int second = 0;
  int millisecond = 0;
};

// `time` on local `day`. A time that a DST change skips, such as 2:30 AM on
// the night clocks spring forward, moves to the next hour that exists.
std::optional<base::Time> AtLocalTime(int day, const TimeOfDay& time) {
  const Date date = CivilFromDays(day);
  base::Time::Exploded exploded = {.year = date.year,
                                   .month = date.month,
                                   .day_of_week = Weekday(day),
                                   .day_of_month = date.day,
                                   .hour = time.hour,
                                   .minute = time.minute,
                                   .second = time.second,
                                   .millisecond = time.millisecond};
  // Clocks never skip more than two hours.
  for (; exploded.hour <= std::min(time.hour + 2, 23); ++exploded.hour) {
    base::Time result;
    if (base::Time::FromLocalExploded(exploded, &result)) {
      return result;
    }
  }
  return std::nullopt;
}

// Three hours from `now`, rounded up to the next local quarter hour.
base::Time Later(base::Time now) {
  const base::Time later = now + base::Hours(3);
  base::Time::Exploded local;
  later.LocalExplode(&local);
  const base::TimeDelta past_quarter = base::Minutes(local.minute % 15) +
                                       base::Seconds(local.second) +
                                       base::Milliseconds(local.millisecond);
  return past_quarter.is_zero() ? later
                                : later - past_quarter + base::Minutes(15);
}

// `now` with its local calendar fields.
struct Clock {
  base::Time now;
  base::Time::Exploded local;
  int today = 0;
};

std::optional<Clock> MakeClock(base::Time now) {
  Clock clock = {.now = now};
  now.LocalExplode(&clock.local);
  if (!clock.local.HasValidValues()) {
    return std::nullopt;
  }
  clock.today = LocalDay(clock.local);
  return clock;
}

// A time as typed, before am/pm is settled.
struct ClockTime {
  int hour = 0;  // 0-23, or 1-12 when not `exact`.
  int minute = 0;
  // Has am/pm or reads as 24-hour, like "07:30" or "17:30".
  bool exact = true;
  // A lone number like "9", which is too vague to stand alone.
  bool bare = false;
  // "midnight": the end of the day.
  bool end_of_day = false;
};

// Settles am/pm for a time typed without it, like "at 5" or "mon at 10:30".
int SettleHour(const ClockTime& time, std::optional<Part> part) {
  if (time.exact || time.hour == 12) {
    return time.hour;
  }
  const bool pm = part ? *part != Part::kMorning : time.hour <= 7;
  return pm ? time.hour + 12 : time.hour;
}

int PartHour(Part part) {
  switch (part) {
    case Part::kMorning:
      return 9;
    case Part::kAfternoon:
      return 13;
    case Part::kEvening:
    case Part::kTonight:
      return 18;
    case Part::kNight:
      return 20;
  }
  NOTREACHED();
}

// What the input names; anything unset takes a default.
struct Expression {
  std::optional<int> day;
  std::optional<Part> part;
  std::optional<ClockTime> time;
};

struct MonthDay {
  int month;
  int day;
  std::optional<int> year;
};

struct Offset {
  int days = 0;
  int64_t minutes = 0;
};

struct Resolved {
  base::Time time;
  // Set for durations of minutes and hours, which read as "In 2 hours".
  std::optional<base::TimeDelta> duration;
};

struct Token {
  enum class Kind { kWord, kNumber, kColon, kSlash };

  Kind kind;
  // Lowercase letters or digits; empty for punctuation.
  std::string text;
  int value = 0;  // Numbers only.
};

// Splits `input` into words, numbers, ':' and '/'. Returns nullopt for
// characters other than those, spaces, commas and dots.
std::optional<std::vector<Token>> Tokenize(std::u16string_view input) {
  if (!base::IsStringASCII(input)) {
    return std::nullopt;
  }
  const std::string lower = base::ToLowerASCII(base::UTF16ToASCII(input));
  // "9.30" is a time; other dots go, so "a.m." reads as "am".
  std::string text;
  for (size_t i = 0; i < lower.size(); ++i) {
    if (lower[i] != '.') {
      text += lower[i];
    } else if (i > 0 && i + 1 < lower.size() &&
               base::IsAsciiDigit(lower[i - 1]) &&
               base::IsAsciiDigit(lower[i + 1])) {
      text += ':';
    }
  }

  std::vector<Token> tokens;
  size_t pos = 0;
  while (pos < text.size()) {
    const char c = text[pos];
    if (base::IsAsciiWhitespace(c) || c == ',') {
      ++pos;
    } else if (c == ':' || c == '/') {
      tokens.push_back(
          {.kind = c == ':' ? Token::Kind::kColon : Token::Kind::kSlash});
      ++pos;
    } else if (base::IsAsciiDigit(c) || base::IsAsciiAlpha(c)) {
      const bool digits = base::IsAsciiDigit(c);
      size_t end = pos + 1;
      while (end < text.size() && (digits ? base::IsAsciiDigit(text[end])
                                          : base::IsAsciiAlpha(text[end]))) {
        ++end;
      }
      Token token = {.kind = digits ? Token::Kind::kNumber : Token::Kind::kWord,
                     .text = text.substr(pos, end - pos)};
      // Four digits cover years and "1030pm"; longer numbers are not times.
      if (digits && (token.text.size() > 4 ||
                     !base::StringToInt(token.text, &token.value))) {
        return std::nullopt;
      }
      tokens.push_back(std::move(token));
      pos = end;
    } else {
      return std::nullopt;
    }
  }
  return tokens;
}

// Recursive descent over the tokens. Each Take*() consumes what it matches
// and leaves the position alone when it does not match.
class Parser {
 public:
  Parser(std::vector<Token> tokens, const Clock& clock)
      : tokens_(std::move(tokens)), clock_(clock) {}

  std::optional<Resolved> Parse() {
    for (auto parse : {&Parser::ParseLater, &Parser::ParseDuration,
                       &Parser::ParseExpression}) {
      pos_ = 0;
      std::optional<Resolved> resolved = (this->*parse)();
      if (resolved && AtEnd()) {
        return resolved;
      }
    }
    return std::nullopt;
  }

 private:
  bool AtEnd() const { return pos_ == tokens_.size(); }

  bool TakeWord(std::string_view word) {
    if (AtEnd() || tokens_[pos_].kind != Token::Kind::kWord ||
        tokens_[pos_].text != word) {
      return false;
    }
    ++pos_;
    return true;
  }

  // Takes the next word if it is a key of `words`.
  template <typename Map>
  std::optional<typename Map::mapped_type> TakeWordIn(const Map& words) {
    if (AtEnd() || tokens_[pos_].kind != Token::Kind::kWord) {
      return std::nullopt;
    }
    const auto* value =
        base::FindOrNull(words, std::string_view(tokens_[pos_].text));
    if (!value) {
      return std::nullopt;
    }
    ++pos_;
    return *value;
  }

  bool TakePunctuation(Token::Kind kind) {
    if (AtEnd() || tokens_[pos_].kind != kind) {
      return false;
    }
    ++pos_;
    return true;
  }

  const Token* TakeNumber() {
    if (AtEnd() || tokens_[pos_].kind != Token::Kind::kNumber) {
      return nullptr;
    }
    return &tokens_[pos_++];
  }

  // True for pm.
  std::optional<bool> TakeMeridiem() {
    if (TakeWord("am") || TakeWord("a")) {
      return false;
    }
    if (TakeWord("pm") || TakeWord("p")) {
      return true;
    }
    return std::nullopt;
  }

  // "later", "later today".
  std::optional<Resolved> ParseLater() {
    if (!TakeWord("later")) {
      return std::nullopt;
    }
    TakeWord("today");
    return Resolved{.time = Later(clock_.now)};
  }

  // "in 2h", "90 min", "1 hour and 30 minutes", "in 3 days",
  // "a week from now", "in 2 days at 9am".
  std::optional<Resolved> ParseDuration() {
    TakeWord("in");
    Offset offset;
    bool has_term = false;
    while (!AtEnd()) {
      const size_t start = pos_;
      if (has_term) {
        TakeWord("and");
      }
      const std::optional<Offset> term = TakeDurationTerm();
      if (!term) {
        pos_ = start;
        break;
      }
      offset.days += term->days;
      offset.minutes += term->minutes;
      if (offset.days + offset.minutes / kMinutesPerDay > kMaxDays) {
        return std::nullopt;
      }
      has_term = true;
    }
    if (!has_term || (TakeWord("from") && !TakeWord("now"))) {
      return std::nullopt;
    }

    if (!AtEnd()) {
      // Only whole days take a time.
      const std::optional<ClockTime> time = TakeTime();
      if (offset.days == 0 || offset.minutes != 0 || !time || !AtEnd()) {
        return std::nullopt;
      }
      return Resolve({.day = clock_.today + offset.days, .time = time});
    }

    const base::TimeDelta minutes = base::Minutes(offset.minutes);
    if (offset.days == 0) {
      return Resolved{.time = clock_.now + minutes, .duration = minutes};
    }
    // Whole days keep the time of day.
    const base::Time::Exploded& local = clock_.local;
    const std::optional<base::Time> same_time = AtLocalTime(
        clock_.today + offset.days, {.hour = local.hour,
                                     .minute = local.minute,
                                     .second = local.second,
                                     .millisecond = local.millisecond});
    if (!same_time) {
      return std::nullopt;
    }
    return Resolved{.time = *same_time + minutes};
  }

  // "20 min", "2h", "an hour", "half an hour".
  std::optional<Offset> TakeDurationTerm() {
    const size_t start = pos_;
    const bool article = TakeWord("a") || TakeWord("an");
    if (TakeWord("half")) {
      if (!TakeWord("an")) {
        TakeWord("a");
      }
      if (TakeWordIn(kUnits) == Unit::kHour) {
        return Offset{.minutes = 30};
      }
      pos_ = start;
      return std::nullopt;
    }
    int count = 1;
    if (!article) {
      const Token* number = TakeNumber();
      if (!number) {
        pos_ = start;
        return std::nullopt;
      }
      count = number->value;
    }
    const std::optional<Unit> unit = TakeWordIn(kUnits);
    if (!unit) {
      pos_ = start;
      return std::nullopt;
    }
    switch (*unit) {
      case Unit::kMinute:
        return Offset{.minutes = count};
      case Unit::kHour:
        return Offset{.minutes = int64_t{count} * 60};
      case Unit::kDay:
        return Offset{.days = count};
      case Unit::kWeek:
        return Offset{.days = count * 7};
    }
    NOTREACHED();
  }

  // At most one each of a day, a part of day and a time, in any order:
  // "fri 3pm", "tomorrow evening", "at 5", "9am oct 20".
  std::optional<Resolved> ParseExpression() {
    Expression expression;
    while (!AtEnd()) {
      if (!expression.day && TakeDay(expression)) {
        continue;
      }
      if (!expression.part) {
        if (const std::optional<Part> part = TakeWordIn(kParts)) {
          expression.part = part;
          continue;
        }
      }
      if (!expression.time) {
        if (const std::optional<ClockTime> time = TakeTime()) {
          expression.time = time;
          continue;
        }
      }
      return std::nullopt;
    }
    if (expression.time && expression.time->bare && !expression.day &&
        !expression.part) {
      return std::nullopt;
    }
    return Resolve(expression);
  }

  // Takes a day into `expression`, with its part of day for "tonight",
  // "this morning" and the like.
  bool TakeDay(Expression& expression) {
    const size_t start = pos_;
    const int today = clock_.today;
    if (TakeWord("today")) {
      expression.day = today;
      return true;
    }
    if (TakeWord("tomorrow") || TakeWord("tmrw") || TakeWord("tmr")) {
      expression.day = today + 1;
      return true;
    }
    if (TakeWord("weekend") || (TakeWord("this") && TakeWord("weekend"))) {
      expression.day = NextWeekday(today, kSaturday);
      return true;
    }
    pos_ = start;
    if (TakeWord("tonight") && !expression.part) {
      expression.day = today;
      expression.part = Part::kTonight;
      return true;
    }
    pos_ = start;
    if (TakeWord("this")) {
      const std::optional<Part> part = TakeWordIn(kParts);
      if (part && *part != Part::kNight && !expression.part) {
        expression.day = today;
        expression.part = *part == Part::kEvening ? Part::kTonight : *part;
        return true;
      }
      pos_ = start;
      return false;
    }
    if (TakeWord("next")) {
      if (TakeWord("week")) {
        expression.day = NextWeekday(today, kMonday);
        return true;
      }
      if (TakeWord("weekend")) {
        // On a weekend day, the coming Saturday is already the next weekend.
        const int saturday = NextWeekday(today, kSaturday);
        expression.day = IsWeekend(today) ? saturday : saturday + 7;
        return true;
      }
      if (const std::optional<int> weekday = TakeWordIn(kWeekdays)) {
        expression.day = NextWeekday(today, *weekday);
        return true;
      }
      pos_ = start;
      return false;
    }
    TakeWord("on");
    if (const std::optional<int> weekday = TakeWordIn(kWeekdays)) {
      expression.day = NextWeekday(today, *weekday);
      return true;
    }
    if (const std::optional<int> date = TakeDate()) {
      expression.day = date;
      return true;
    }
    pos_ = start;
    return false;
  }

  // A date that has already passed this year, given without a year, means
  // next year's.
  std::optional<int> TakeDate() {
    const size_t start = pos_;
    const std::optional<MonthDay> date = TakeMonthDay();
    if (!date || date->month < 1 || date->month > 12) {
      pos_ = start;
      return std::nullopt;
    }
    int year = date->year.value_or(clock_.local.year);
    if (!date->year &&
        std::pair(date->month, date->day) <
            std::pair(clock_.local.month, clock_.local.day_of_month)) {
      ++year;
    }
    if (date->day < 1 || date->day > DaysInMonth(year, date->month)) {
      pos_ = start;
      return std::nullopt;
    }
    return DaysFromCivil(
        {.year = year, .month = date->month, .day = date->day});
  }

  // "oct 14", "october 14th 2026", "14 oct", "14th of october", "10/14",
  // "10/14/26", "10/14/2026".
  std::optional<MonthDay> TakeMonthDay() {
    const size_t start = pos_;
    if (const std::optional<int> leading_month = TakeWordIn(kMonths)) {
      if (const std::optional<int> day = TakeDayOfMonth()) {
        return MonthDay{
            .month = *leading_month, .day = *day, .year = TakeYear()};
      }
    } else if (const Token* first = TakeNumber()) {
      if (TakePunctuation(Token::Kind::kSlash)) {
        // Month first, as in the US.
        const Token* second = TakeNumber();
        if (first->text.size() <= 2 && second && second->text.size() <= 2) {
          MonthDay date = {.month = first->value, .day = second->value};
          if (!TakePunctuation(Token::Kind::kSlash)) {
            return date;
          }
          const Token* year = TakeNumber();
          if (year && (year->text.size() == 2 || year->text.size() == 4)) {
            date.year =
                year->text.size() == 2 ? 2000 + year->value : year->value;
            return date;
          }
        }
      } else {
        pos_ = start;
        if (const std::optional<int> day = TakeDayOfMonth()) {
          TakeWord("of");
          if (const std::optional<int> trailing_month = TakeWordIn(kMonths)) {
            return MonthDay{
                .month = *trailing_month, .day = *day, .year = TakeYear()};
          }
        }
      }
    }
    pos_ = start;
    return std::nullopt;
  }

  // 1-31, with an optional ordinal suffix: "14", "14th".
  std::optional<int> TakeDayOfMonth() {
    const Token* number = TakeNumber();
    if (!number) {
      return std::nullopt;
    }
    if (number->text.size() > 2 || number->value < 1 || number->value > 31) {
      --pos_;
      return std::nullopt;
    }
    if (!TakeWord("st") && !TakeWord("nd") && !TakeWord("rd")) {
      TakeWord("th");
    }
    return number->value;
  }

  // A four-digit year, unless it is really a time such as "1030pm".
  std::optional<int> TakeYear() {
    const size_t start = pos_;
    const Token* number = TakeNumber();
    if (number && number->text.size() == 4 && !TakeMeridiem() &&
        !TakePunctuation(Token::Kind::kColon)) {
      return number->value;
    }
    pos_ = start;
    return std::nullopt;
  }

  // "9am", "9:30 p.m.", "21:00", "930pm", "noon", "midnight", each optionally
  // after "at", or a lone hour as in "at 5" or "tomorrow 9".
  std::optional<ClockTime> TakeTime() {
    const size_t start = pos_;
    const bool at = TakeWord("at");
    if (TakeWord("noon") || TakeWord("midday")) {
      return ClockTime{.hour = 12};
    }
    if (TakeWord("midnight")) {
      return ClockTime{.end_of_day = true};
    }
    const Token* number = TakeNumber();
    if (!number) {
      pos_ = start;
      return std::nullopt;
    }
    ClockTime time;
    bool has_minutes = false;
    if (number->text.size() > 2) {
      time.hour = number->value / 100;
      time.minute = number->value % 100;
      has_minutes = true;
    } else {
      time.hour = number->value;
      if (TakePunctuation(Token::Kind::kColon)) {
        const Token* minutes = TakeNumber();
        if (!minutes || minutes->text.size() != 2) {
          pos_ = start;
          return std::nullopt;
        }
        time.minute = minutes->value;
        has_minutes = true;
      }
    }
    const std::optional<bool> pm = TakeMeridiem();
    // "930pm" needs its am/pm to be a time.
    if (time.minute > 59 || (pm && (time.hour < 1 || time.hour > 12)) ||
        (!pm && (number->text.size() > 2 || time.hour > 23))) {
      pos_ = start;
      return std::nullopt;
    }
    if (pm) {
      time.hour = time.hour % 12 + (*pm ? 12 : 0);
    } else {
      time.exact = time.hour == 0 || time.hour > 12 || number->text[0] == '0';
      time.bare = !at && !has_minutes;
    }
    return time;
  }

  std::optional<Resolved> Resolve(const Expression& expression) const {
    int day = expression.day.value_or(clock_.today);
    TimeOfDay time_of_day = {.hour = kDefaultHour};
    if (expression.time) {
      if (expression.time->end_of_day) {
        ++day;
        time_of_day = {};
      } else {
        time_of_day = {.hour = SettleHour(*expression.time, expression.part),
                       .minute = expression.time->minute};
      }
    } else if (expression.part == Part::kTonight) {
      for (int hour : {18, 20}) {
        const std::optional<base::Time> tonight =
            AtLocalTime(day, {.hour = hour});
        if (tonight && *tonight > clock_.now) {
          return Resolved{.time = *tonight};
        }
      }
      return std::nullopt;
    } else if (expression.part) {
      time_of_day.hour = PartHour(*expression.part);
    }

    std::optional<base::Time> time = AtLocalTime(day, time_of_day);
    // Without a day, a time that has passed today means tomorrow.
    if (time && !expression.day && *time <= clock_.now) {
      time = AtLocalTime(day + 1, time_of_day);
    }
    if (!time) {
      return std::nullopt;
    }
    return Resolved{.time = *time};
  }

  std::vector<Token> tokens_;
  Clock clock_;
  size_t pos_ = 0;
};

// "2 hours", "1 minute".
std::u16string CountOf(int count, std::u16string_view unit) {
  return base::StrCat(
      {base::NumberToString16(count), u" ", unit, count == 1 ? u"" : u"s"});
}

// "In 2 hours", "In 1 hour 30 minutes".
std::u16string DescribeDuration(base::TimeDelta duration) {
  const int hours = duration.InHours();
  const int minutes = duration.InMinutes() % 60;
  std::u16string description = u"In";
  if (hours > 0) {
    base::StrAppend(&description, {u" ", CountOf(hours, u"hour")});
  }
  if (minutes > 0) {
    base::StrAppend(&description, {u" ", CountOf(minutes, u"minute")});
  }
  return description;
}

// "Today 6:00 PM", "Tomorrow 9:00 AM", "Mon, Oct 13, 9:00 AM".
}  // namespace

std::u16string DescribeTime(base::Time time, base::Time now) {
  base::Time::Exploded local;
  time.LocalExplode(&local);
  base::Time::Exploded today;
  now.LocalExplode(&today);
  switch (LocalDay(local) - LocalDay(today)) {
    case 0:
      return u"Today " + base::TimeFormatTimeOfDay(time);
    case 1:
      return u"Tomorrow " + base::TimeFormatTimeOfDay(time);
    case -1:
      return u"Yesterday " + base::TimeFormatTimeOfDay(time);
  }
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

std::optional<ParsedTime> Parse(std::u16string_view input, base::Time now) {
  const std::optional<Clock> clock = MakeClock(now);
  std::optional<std::vector<Token>> tokens = Tokenize(input);
  if (!clock || !tokens || tokens->empty()) {
    return std::nullopt;
  }
  const std::optional<Resolved> resolved =
      Parser(std::move(*tokens), *clock).Parse();
  if (!resolved || resolved->time <= now ||
      resolved->time - now > base::Days(kMaxDays)) {
    return std::nullopt;
  }
  return ParsedTime{
      .time = resolved->time,
      .description = resolved->duration && *resolved->duration < base::Days(1)
                         ? DescribeDuration(*resolved->duration)
                         : DescribeTime(resolved->time, now)};
}

std::vector<Preset> GetPresets(base::Time now) {
  std::vector<Preset> presets;
  const std::optional<Clock> clock = MakeClock(now);
  if (!clock) {
    return presets;
  }
  const auto add = [&](std::u16string label, std::optional<base::Time> when) {
    if (when && *when > now) {
      presets.push_back(
          {.label = std::move(label),
           .when = {.time = *when, .description = DescribeTime(*when, now)}});
    }
  };
  const int today = clock->today;
  const base::Time later = Later(now);
  if (LocalDay(later) == today) {
    add(u"Later today", later);
  }
  if (clock->local.hour < 17) {
    add(u"This evening", AtLocalTime(today, {.hour = 18}));
  }
  add(u"Tomorrow", AtLocalTime(today + 1, {.hour = kDefaultHour}));
  add(IsWeekend(today) ? u"Next weekend" : u"This weekend",
      AtLocalTime(NextWeekday(today, kSaturday), {.hour = kDefaultHour}));
  add(u"Next week",
      AtLocalTime(NextWeekday(today, kMonday), {.hour = kDefaultHour}));
  return presets;
}

}  // namespace origin_time_parser
