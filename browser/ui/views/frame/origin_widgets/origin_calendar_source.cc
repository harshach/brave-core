// Copyright (c) 2026 The Brave Authors. All rights reserved.
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this file,
// You can obtain one at https://mozilla.org/MPL/2.0/.

#include "brave/browser/ui/views/frame/origin_widgets/origin_calendar_source.h"

#include <algorithm>
#include <utility>

#include "base/byte_size.h"
#include "base/functional/bind.h"
#include "base/json/json_reader.h"
#include "base/strings/strcat.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/string_split.h"
#include "base/strings/utf_string_conversions.h"
#include "base/values.h"
#include "google_apis/common/time_util.h"
#include "net/base/load_flags.h"
#include "net/base/url_util.h"
#include "net/http/http_response_headers.h"
#include "net/traffic_annotation/network_traffic_annotation.h"
#include "services/network/public/cpp/resource_request.h"
#include "services/network/public/cpp/shared_url_loader_factory.h"
#include "services/network/public/cpp/simple_url_loader.h"
#include "services/network/public/mojom/url_response_head.mojom.h"
#include "third_party/icu/source/i18n/unicode/calendar.h"

namespace {

constexpr auto kTrafficAnnotation =
    net::DefineNetworkTrafficAnnotation("origin_google_calendar_events", R"(
      semantics {
        sender: "Origin Google Calendar Widget"
        description:
          "Reads events for the day selected in the calendar widget using "
          "the Google Calendar API, including recurring event instances."
        trigger:
          "The user connects Google Calendar. Events refresh while the widget "
          "is visible, when the selected day changes, or on manual refresh."
        data: "A Google OAuth access token and the selected day's time range."
        destination: GOOGLE_OWNED_SERVICE
      }
      policy {
        cookies_allowed: NO
        setting: "Disconnect Google Calendar or remove the calendar widget."
        policy_exception_justification: "Not implemented."
      })");

std::unique_ptr<icu::Calendar> LocalCalendar(base::Time day) {
  UErrorCode status = U_ZERO_ERROR;
  std::unique_ptr<icu::Calendar> calendar(
      icu::Calendar::createInstance(status));
  if (U_FAILURE(status) || !calendar) {
    return nullptr;
  }
  calendar->setTime(day.InMillisecondsFSinceUnixEpoch(), status);
  return U_SUCCESS(status) ? std::move(calendar) : nullptr;
}

std::optional<base::Time> ParseEventTime(const base::DictValue* value,
                                         bool all_day) {
  if (!value) {
    return std::nullopt;
  }
  const std::string* text = value->FindString(all_day ? "date" : "dateTime");
  if (!text) {
    return std::nullopt;
  }
  base::Time parsed;
  if (!all_day) {
    if (!google_apis::util::GetTimeFromString(*text, &parsed)) {
      return std::nullopt;
    }
    return parsed;
  }
  if (!google_apis::util::GetDateOnlyFromString(*text, &parsed)) {
    return std::nullopt;
  }
  // A date-only event belongs to that local calendar date, not midnight UTC.
  base::Time::Exploded date;
  parsed.UTCExplode(&date);
  auto calendar = LocalCalendar(parsed);
  if (!calendar) {
    return std::nullopt;
  }
  calendar->clear();
  calendar->setLenient(false);
  calendar->set(date.year, date.month - 1, date.day_of_month, 0, 0, 0);
  UErrorCode status = U_ZERO_ERROR;
  const auto time = calendar->getTime(status);
  return U_SUCCESS(status)
             ? std::make_optional(
                   base::Time::FromMillisecondsSinceUnixEpoch(time))
             : std::nullopt;
}

GURL SafeUrl(const std::string* value) {
  const GURL url(value ? *value : std::string());
  return url.SchemeIs("https") && !url.has_username() && !url.has_password()
             ? url
             : GURL();
}

std::u16string Text(const base::DictValue& value, std::string_view key) {
  const auto* text = value.FindStringByDottedPath(key);
  return text ? base::UTF8ToUTF16(*text) : std::u16string();
}

}  // namespace

OriginCalendarSource::OriginCalendarSource(
    scoped_refptr<network::SharedURLLoaderFactory> url_loader_factory)
    : url_loader_factory_(std::move(url_loader_factory)) {}
OriginCalendarSource::~OriginCalendarSource() = default;

// static
base::Time OriginCalendarSource::StartOfDay(base::Time day) {
  auto calendar = LocalCalendar(day);
  if (!calendar) {
    return day.LocalMidnight();
  }
  calendar->set(UCAL_HOUR_OF_DAY, 0);
  calendar->set(UCAL_MINUTE, 0);
  calendar->set(UCAL_SECOND, 0);
  calendar->set(UCAL_MILLISECOND, 0);
  UErrorCode status = U_ZERO_ERROR;
  const auto time = calendar->getTime(status);
  return U_SUCCESS(status) ? base::Time::FromMillisecondsSinceUnixEpoch(time)
                           : day.LocalMidnight();
}

// static
base::Time OriginCalendarSource::OffsetDay(base::Time day, int offset) {
  auto calendar = LocalCalendar(day);
  if (!calendar) {
    return StartOfDay(day + base::Days(offset));
  }
  UErrorCode status = U_ZERO_ERROR;
  calendar->add(UCAL_DATE, offset, status);
  const auto time = calendar->getTime(status);
  return StartOfDay(U_SUCCESS(status)
                        ? base::Time::FromMillisecondsSinceUnixEpoch(time)
                        : day + base::Days(offset));
}

// static
GURL OriginCalendarSource::DayUrl(base::Time day) {
  auto calendar = LocalCalendar(day);
  if (!calendar) {
    return GURL("https://calendar.google.com/calendar/u/0/r/day");
  }
  UErrorCode status = U_ZERO_ERROR;
  const int year = calendar->get(UCAL_YEAR, status);
  const int month = calendar->get(UCAL_MONTH, status) + 1;
  const int date = calendar->get(UCAL_DATE, status);
  return GURL(base::StrCat({"https://calendar.google.com/calendar/u/0/r/day/",
                            base::NumberToString(year), "/",
                            base::NumberToString(month), "/",
                            base::NumberToString(date)}));
}

void OriginCalendarSource::Cancel() {
  weak_factory_.InvalidateWeakPtrs();
  url_loader_.reset();
  callback_.Reset();
  access_token_.clear();
  pending_day_ = {};
  page_tokens_.clear();
}

void OriginCalendarSource::Fetch(std::string access_token,
                                 base::Time day,
                                 FetchCallback callback) {
  Cancel();
  callback_ = std::move(callback);
  access_token_ = std::move(access_token);
  day_ = StartOfDay(day);
  if (access_token_.empty() || !url_loader_factory_) {
    Finish(base::unexpected(Error::kAuthentication));
    return;
  }
  FetchPage(std::string());
}

void OriginCalendarSource::FetchPage(const std::string& page_token) {
  GURL url("https://www.googleapis.com/calendar/v3/calendars/primary/events");
  for (const auto& [key, value] : base::StringPairs{
           {"timeMin", google_apis::util::FormatTimeAsString(day_)},
           {"timeMax",
            google_apis::util::FormatTimeAsString(OffsetDay(day_, 1))},
           {"singleEvents", "true"},
           {"orderBy", "startTime"},
           {"showDeleted", "false"},
           {"maxResults", "250"},
           {"pageToken", page_token}}) {
    if (!value.empty()) {
      url = net::AppendQueryParameter(url, key, value);
    }
  }
  auto request = std::make_unique<network::ResourceRequest>();
  request->url = url;
  request->credentials_mode = network::mojom::CredentialsMode::kOmit;
  request->redirect_mode = network::mojom::RedirectMode::kError;
  request->load_flags = net::LOAD_BYPASS_CACHE | net::LOAD_DISABLE_CACHE;
  request->headers.SetHeader("Authorization", "Bearer " + access_token_);
  url_loader_ =
      network::SimpleURLLoader::Create(std::move(request), kTrafficAnnotation);
  url_loader_->SetTimeoutDuration(base::Seconds(20));
  url_loader_->DownloadToString(url_loader_factory_.get(),
                                base::BindOnce(&OriginCalendarSource::OnFetched,
                                               weak_factory_.GetWeakPtr()),
                                base::MiBU(2).InBytes());
}

void OriginCalendarSource::OnFetched(std::optional<std::string> body) {
  int status = 0;
  if (url_loader_->ResponseInfo() && url_loader_->ResponseInfo()->headers) {
    status = url_loader_->ResponseInfo()->headers->response_code();
  }
  url_loader_.reset();
  if (status != 200 || !body) {
    const Error error = status == 401   ? Error::kAuthentication
                        : status == 403 ? Error::kPermission
                        : status == 429 ? Error::kRateLimited
                                        : Error::kNetwork;
    Finish(base::unexpected(error));
    return;
  }
  auto page = ParseGoogleEvents(*body, day_);
  if (!page.has_value()) {
    Finish(base::unexpected(page.error()));
    return;
  }
  pending_day_.calendar_name = std::move(page->calendar_name);
  for (auto& event : page->events) {
    pending_day_.events.push_back(std::move(event));
  }
  if (!page->next_page_token.empty()) {
    if (page_tokens_.size() >= 20 ||
        !page_tokens_.insert(page->next_page_token).second) {
      Finish(base::unexpected(Error::kInvalidResponse));
      return;
    }
    FetchPage(page->next_page_token);
    return;
  }
  std::set<std::string> ids;
  std::erase_if(pending_day_.events, [&ids](const auto& event) {
    return !ids.insert(event.id).second;
  });
  std::ranges::sort(pending_day_.events, [](const auto& a, const auto& b) {
    if (a.all_day != b.all_day) {
      return a.all_day;
    }
    return a.start != b.start ? a.start < b.start : a.id < b.id;
  });
  Finish(std::move(pending_day_));
}

void OriginCalendarSource::Finish(Result result) {
  access_token_.clear();
  auto callback = std::move(callback_);
  if (callback) {
    std::move(callback).Run(std::move(result));
  }
}

// static
OriginCalendarSource::Result OriginCalendarSource::ParseGoogleEvents(
    std::string_view json,
    base::Time day) {
  auto value = base::JSONReader::ReadDict(json, base::JSON_PARSE_RFC);
  const auto* items = value ? value->FindList("items") : nullptr;
  if (!items) {
    return base::unexpected(Error::kInvalidResponse);
  }
  OriginCalendarDay result;
  result.calendar_name = Text(*value, "summary");
  if (const auto* token = value->FindString("nextPageToken")) {
    result.next_page_token = *token;
  }
  const base::Time start_of_day = StartOfDay(day);
  const base::Time end_of_day = OffsetDay(day, 1);
  for (const auto& item : *items) {
    const auto* event = item.GetIfDict();
    if (!event || !event->FindString("id") ||
        event->FindString("id")->empty() ||
        Text(*event, "status") == u"cancelled") {
      continue;
    }
    if (const auto* attendees = event->FindList("attendees");
        attendees && std::ranges::any_of(*attendees, [](const auto& attendee) {
          const auto* person = attendee.GetIfDict();
          return person && person->FindBool("self").value_or(false) &&
                 Text(*person, "responseStatus") == u"declined";
        })) {
      continue;
    }
    const auto* start_value = event->FindDict("start");
    const bool all_day = start_value && start_value->FindString("date");
    const auto start = ParseEventTime(start_value, all_day);
    const auto end = ParseEventTime(event->FindDict("end"), all_day);
    if (!start || !end || *end < *start || *start >= end_of_day ||
        (*end <= start_of_day && *start != *end) ||
        (*start == *end && *start < start_of_day)) {
      continue;
    }
    OriginCalendarEvent parsed;
    parsed.id = *event->FindString("id");
    parsed.title = Text(*event, "summary");
    if (parsed.title.empty()) {
      parsed.title = u"Untitled event";
    }
    parsed.location = Text(*event, "location");
    parsed.organizer = Text(*event, "organizer.displayName");
    parsed.start = *start;
    parsed.end = *end;
    parsed.all_day = all_day;
    parsed.event_url = SafeUrl(event->FindString("htmlLink"));
    if (!parsed.event_url.DomainIs("google.com")) {
      parsed.event_url = GURL();
    }
    parsed.join_url = SafeUrl(event->FindString("hangoutLink"));
    if (const auto* entries =
            event->FindListByDottedPath("conferenceData.entryPoints")) {
      for (const auto& entry : *entries) {
        const auto* point = entry.GetIfDict();
        if (point && Text(*point, "entryPointType") == u"video" &&
            parsed.join_url.is_empty()) {
          parsed.join_url = SafeUrl(point->FindString("uri"));
        }
      }
    }
    result.events.push_back(std::move(parsed));
  }
  return result;
}
