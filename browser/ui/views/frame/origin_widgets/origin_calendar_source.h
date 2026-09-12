// Copyright (c) 2026 The Brave Authors. All rights reserved.
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this file,
// You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef BRAVE_BROWSER_UI_VIEWS_FRAME_ORIGIN_WIDGETS_ORIGIN_CALENDAR_SOURCE_H_
#define BRAVE_BROWSER_UI_VIEWS_FRAME_ORIGIN_WIDGETS_ORIGIN_CALENDAR_SOURCE_H_

#include <memory>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <vector>

#include "base/functional/callback.h"
#include "base/memory/scoped_refptr.h"
#include "base/memory/weak_ptr.h"
#include "base/time/time.h"
#include "base/types/expected.h"
#include "url/gurl.h"

namespace network {
class SharedURLLoaderFactory;
class SimpleURLLoader;
}  // namespace network

struct OriginCalendarEvent {
  std::string id;
  std::u16string title;
  std::u16string location;
  std::u16string organizer;
  base::Time start;
  base::Time end;
  GURL join_url;
  GURL event_url;
  bool all_day = false;
};

struct OriginCalendarDay {
  std::u16string calendar_name;
  std::vector<OriginCalendarEvent> events;
  std::string next_page_token;
};

// Reads one complete local day from the primary Google Calendar. Google
// expands recurring events; all pages are fetched before publishing a day.
class OriginCalendarSource {
 public:
  enum class Error {
    kNetwork,
    kAuthentication,
    kPermission,
    kRateLimited,
    kInvalidResponse
  };
  using Result = base::expected<OriginCalendarDay, Error>;
  using FetchCallback = base::OnceCallback<void(Result)>;

  explicit OriginCalendarSource(
      scoped_refptr<network::SharedURLLoaderFactory> url_loader_factory);
  ~OriginCalendarSource();

  // Replaces any request already in flight. The bearer token is never stored
  // in a URL, a preference, or an event cache.
  void Fetch(std::string access_token, base::Time day, FetchCallback callback);
  void Cancel();

  // Calendar arithmetic uses the local timezone, including 23/25-hour days.
  static base::Time StartOfDay(base::Time day);
  static base::Time OffsetDay(base::Time day, int offset);
  static GURL DayUrl(base::Time day);
  static Result ParseGoogleEvents(std::string_view json, base::Time day);

 private:
  void FetchPage(const std::string& page_token);
  void OnFetched(std::optional<std::string> body);
  void Finish(Result result);

  scoped_refptr<network::SharedURLLoaderFactory> url_loader_factory_;
  std::unique_ptr<network::SimpleURLLoader> url_loader_;
  FetchCallback callback_;
  std::string access_token_;
  base::Time day_;
  OriginCalendarDay pending_day_;
  std::set<std::string> page_tokens_;
  base::WeakPtrFactory<OriginCalendarSource> weak_factory_{this};
};

#endif  // BRAVE_BROWSER_UI_VIEWS_FRAME_ORIGIN_WIDGETS_ORIGIN_CALENDAR_SOURCE_H_
