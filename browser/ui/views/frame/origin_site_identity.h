/* Copyright (c) 2026 The Brave Authors. All rights reserved.
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at https://mozilla.org/MPL/2.0/. */

#ifndef BRAVE_BROWSER_UI_VIEWS_FRAME_ORIGIN_SITE_IDENTITY_H_
#define BRAVE_BROWSER_UI_VIEWS_FRAME_ORIGIN_SITE_IDENTITY_H_

#include <array>
#include <optional>
#include <string>
#include <string_view>

#include "third_party/skia/include/core/SkColor.h"
#include "url/gurl.h"

struct OriginPopularSite {
  std::string_view keyword;
  std::string_view title;
  std::string_view url;
  SkColor accent;
};

// Clean profiles have no local history or favicon cache yet. These navigation
// fallbacks mirror Brave's recognizable default sites and carry the same brand
// color used by the temporary monogram and page chrome until a real favicon is
// available.
inline constexpr std::array kOriginPopularSites = {
    OriginPopularSite{"reddit", "reddit.com", "https://www.reddit.com/",
                      SkColorSetRGB(0xFF, 0x45, 0x00)},
    OriginPopularSite{"macrumors", "MacRumors",
                      "https://www.macrumors.com/",
                      SkColorSetRGB(0xE5, 0x32, 0x2D)},
    OriginPopularSite{"flipboard", "flipboard.com", "https://flipboard.com/",
                      SkColorSetRGB(0xE1, 0x28, 0x28)},
    OriginPopularSite{"espn", "espn.com", "https://www.espn.com/",
                      SkColorSetRGB(0xD0, 0x0A, 0x0A)},
    OriginPopularSite{"wikipedia", "wikipedia.org",
                      "https://www.wikipedia.org/",
                      SkColorSetRGB(0x54, 0x59, 0x5D)},
    OriginPopularSite{"youtube", "youtube.com", "https://www.youtube.com/",
                      SkColorSetRGB(0xFF, 0x00, 0x33)},
    OriginPopularSite{"hacker news", "Hacker News",
                      "https://news.ycombinator.com/",
                      SkColorSetRGB(0xFF, 0x66, 0x00)},
    OriginPopularSite{"news.ycombinator", "Hacker News",
                      "https://news.ycombinator.com/",
                      SkColorSetRGB(0xFF, 0x66, 0x00)},
    OriginPopularSite{"github", "github.com", "https://github.com/",
                      SkColorSetRGB(0x24, 0x29, 0x2F)},
    OriginPopularSite{"gmail", "mail.google.com", "https://mail.google.com/",
                      SkColorSetRGB(0xEA, 0x43, 0x35)},
    OriginPopularSite{"linkedin", "linkedin.com", "https://www.linkedin.com/",
                      SkColorSetRGB(0x0A, 0x66, 0xC2)},
};

inline std::optional<SkColor> GetOriginKnownSiteAccent(const GURL& url) {
  if (!url.is_valid()) {
    return std::nullopt;
  }

  std::string host(url.host());
  if (host.starts_with("www.")) {
    host.erase(0, 4);
  }
  for (const OriginPopularSite& site : kOriginPopularSites) {
    const GURL popular_url(site.url);
    std::string popular_host(popular_url.host());
    if (popular_host.starts_with("www.")) {
      popular_host.erase(0, 4);
    }
    if (host == popular_host) {
      return site.accent;
    }
  }
  return std::nullopt;
}

#endif  // BRAVE_BROWSER_UI_VIEWS_FRAME_ORIGIN_SITE_IDENTITY_H_
