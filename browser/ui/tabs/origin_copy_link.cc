// Copyright (c) 2026 The Brave Authors. All rights reserved.
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this file,
// You can obtain one at https://mozilla.org/MPL/2.0/.

#include "brave/browser/ui/tabs/origin_copy_link.h"

#include <algorithm>
#include <array>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "base/containers/fixed_flat_map.h"
#include "base/containers/fixed_flat_set.h"
#include "base/containers/map_util.h"
#include "base/containers/span.h"
#include "base/i18n/break_iterator.h"
#include "base/strings/escape.h"
#include "base/strings/strcat.h"
#include "base/strings/string_split.h"
#include "base/strings/string_util.h"
#include "base/strings/utf_string_conversions.h"
#include "brave/components/url_sanitizer/core/browser/url_sanitizer_service.h"

namespace origin_copy_link {

namespace {

// Dropped on every site. Mostly the every-site rule of Brave's query filter
// list, which already strips these from cross-site navigations, so pages don't
// rely on them. Added for shared links: gbraid, Mailchimp's and HubSpot's
// campaign IDs (mc_cid, _hsmi) beside their subscriber IDs, and Instagram's
// igsh and igshid, which Threads links carry too.
constexpr auto kTrackers = base::MakeFixedFlatSet<std::string_view>({
    "__hsfp",
    "__hssc",
    "__hstc",
    "__s",
    "_bhlid",
    "_branch_match_id",
    "_branch_referrer",
    "_gl",
    "_hsenc",
    "_hsmi",
    "_openstat",
    "at_recipient_id",
    "at_recipient_list",
    "bbeml",
    "bsft_clkid",
    "bsft_uid",
    "dclid",
    "et_rid",
    "fb_action_ids",
    "fb_comment_id",
    "fbclid",
    "gbraid",
    "gclid",
    "guce_referrer",
    "guce_referrer_sig",
    "hsCtaTracking",
    "igsh",
    "igshid",
    "irclickid",
    "mc_cid",
    "mc_eid",
    "ml_subscriber",
    "ml_subscriber_hash",
    "msclkid",
    "mtm_cid",
    "oft_c",
    "oft_ck",
    "oft_d",
    "oft_id",
    "oft_ids",
    "oft_k",
    "oft_lk",
    "oft_sk",
    "oly_anon_id",
    "oly_enc_id",
    "pk_cid",
    "rb_clickid",
    "s_cid",
    "sc_customer",
    "sc_eh",
    "sc_uid",
    "sfmc_activityid",
    "sfmc_id",
    "sms_click",
    "sms_source",
    "sms_uph",
    "srsltid",
    "ss_email_id",
    "syclid",
    "ttclid",
    "twclid",
    "unicorn_click_id",
    "vero_conv",
    "vero_id",
    "vgo_ee",
    "wbraid",
    "wickedid",
    "yclid",
    "ymclid",
    "ysclid",
});

// Google Analytics campaign tags (utm_source, utm_medium, ...). Only analytics
// reads them, but Brave's navigation filter keeps them, so links often still
// have them.
constexpr std::string_view kCampaignTagPrefix = "utm_";

// Marketo's subscriber token. Its unsubscribe and "view as web page" pages use
// it to find the subscriber, so it stays there, as in Brave's query filter.
constexpr std::string_view kMarketoToken = "mkt_tok";

// Share and search trackers that are only trackers on these sites; elsewhere
// the names can mean anything. YouTube's `t`, for one, is where playback
// starts, while Twitter's is a share token.
constexpr auto kYouTubeTrackers =
    std::to_array<std::string_view>({"feature", "pp", "si"});
constexpr auto kSpotifyTrackers = std::to_array<std::string_view>({"si"});
constexpr auto kTwitterTrackers =
    std::to_array<std::string_view>({"ref_src", "ref_url", "s", "t"});

struct SiteTrackers {
  // Subdomains match too.
  std::string_view domain;
  base::span<const std::string_view> params;
};

constexpr auto kSiteTrackers = std::to_array<SiteTrackers>({
    {"youtube.com", kYouTubeTrackers},
    {"youtu.be", kYouTubeTrackers},
    {"spotify.com", kSpotifyTrackers},
    {"twitter.com", kTwitterTrackers},
    {"x.com", kTwitterTrackers},
});

// Proofpoint's link wrapper. Brave's query filter leaves it alone too.
constexpr std::string_view kUrlDefenseDomain = "urldefense.com";

// Characters that would end a Markdown link's URL early, or read as an escape
// in it, and their percent-encodings.
constexpr auto kMarkdownUrlEscapes =
    base::MakeFixedFlatMap<char, std::string_view>({
        {' ', "%20"},
        {'(', "%28"},
        {')', "%29"},
        {'<', "%3C"},
        {'>', "%3E"},
        {'\\', "%5C"},
    });

// Characters escaped in a Markdown link's text.
constexpr std::u16string_view kMarkdownTextSpecials = u"\\[]<";

// A one-line selection up to this long is matched whole, as Chrome does.
constexpr size_t kMaxExactLength = 300;

// Words taken from each end of a range.
constexpr size_t kRangeWords = 5;

// Breaks in selected text that likely separate blocks: lines and table cells.
constexpr std::u16string_view kBlockBreaks = u"\n\r\t\u2028\u2029";

constexpr std::string_view kDirectiveDelimiter = ":~:";

base::span<const std::string_view> SiteTrackersFor(const GURL& url) {
  for (const SiteTrackers& site : kSiteTrackers) {
    if (url.DomainIs(site.domain)) {
      return site.params;
    }
  }
  return {};
}

bool PageNeedsMarketoToken(const GURL& url) {
  const std::string spec = base::ToLowerASCII(url.spec());
  return spec.contains("unsubscribe") || spec.contains("emailwebview");
}

bool IsTracker(std::string_view name,
               base::span<const std::string_view> site_trackers,
               const GURL& url) {
  if (name == kMarketoToken) {
    return !PageNeedsMarketoToken(url);
  }
  return name.starts_with(kCampaignTagPrefix) || kTrackers.contains(name) ||
         std::ranges::contains(site_trackers, name);
}

std::u16string EscapeMarkdownText(std::u16string_view text) {
  std::u16string escaped;
  escaped.reserve(text.size());
  for (char16_t c : text) {
    if (std::ranges::contains(kMarkdownTextSpecials, c)) {
      escaped.push_back(u'\\');
    }
    escaped.push_back(c);
  }
  return escaped;
}

std::string EscapeMarkdownUrl(std::string_view spec) {
  std::string escaped;
  escaped.reserve(spec.size());
  for (char c : spec) {
    if (const std::string_view* encoded =
            base::FindOrNull(kMarkdownUrlEscapes, c)) {
      escaped.append(*encoded);
    } else {
      escaped.push_back(c);
    }
  }
  return escaped;
}

// Escapes text for a text directive the way Chrome's "Copy link to highlight"
// does (components/shared_highlighting). That covers '-', ',' and '&', which
// delimit the directive's parts.
std::string EscapeDirectiveText(std::u16string_view text) {
  std::string escaped = base::EscapeQueryParamValue(base::UTF16ToUTF8(text),
                                                    /*use_plus=*/false);
  base::ReplaceSubstringsAfterOffset(&escaped, 0, "-", "%2D");
  return escaped;
}

struct Word {
  size_t start;
  size_t end;
};

std::vector<Word> FindWords(std::u16string_view text) {
  std::vector<Word> words;
  base::i18n::BreakIterator iter(text, base::i18n::BreakIterator::BREAK_WORD);
  if (!iter.Init()) {
    return words;
  }
  while (iter.Advance()) {
    if (iter.IsWord()) {
      words.push_back({iter.prev(), iter.pos()});
    }
  }
  return words;
}

// `text` up to the end of its `count`th word, or all of it if it has no more.
std::u16string_view LeadingWords(std::u16string_view text, size_t count) {
  const std::vector<Word> words = FindWords(text);
  return words.size() > count ? text.substr(0, words[count - 1].end) : text;
}

// `text` from the start of its `count`th word from the end, or all of it if it
// has no more.
std::u16string_view TrailingWords(std::u16string_view text, size_t count) {
  const std::vector<Word> words = FindWords(text);
  return words.size() > count ? text.substr(words[words.size() - count].start)
                              : text;
}

// The escaped value of a text directive for the non-empty `lines` of a
// selection: the text itself, or "start,end".
std::string TextDirectiveValue(const std::vector<std::u16string>& lines) {
  const std::u16string_view first = lines.front();
  const std::u16string_view last = lines.back();
  size_t count = kRangeWords;
  if (lines.size() == 1) {
    if (first.size() <= kMaxExactLength) {
      return EscapeDirectiveText(first);
    }
    // The two ends of one line mustn't overlap.
    count = std::min(count, FindWords(first).size() / 2);
    if (count == 0) {
      return EscapeDirectiveText(first);
    }
  }
  return base::StrCat({EscapeDirectiveText(LeadingWords(first, count)), ",",
                       EscapeDirectiveText(TrailingWords(last, count))});
}

}  // namespace

GURL CleanUrl(brave::URLSanitizerService* sanitizer, const GURL& url) {
  return RemoveTrackingParameters(sanitizer ? sanitizer->SanitizeURL(url)
                                            : url);
}

GURL RemoveTrackingParameters(const GURL& url) {
  if (!url.SchemeIsHTTPOrHTTPS() || !url.has_query() ||
      url.DomainIs(kUrlDefenseDomain)) {
    return url;
  }
  const base::span<const std::string_view> site_trackers = SiteTrackersFor(url);
  // Parameters are compared and kept as written, so nothing else changes.
  // Empty ones ("a=1&&b=2") carry nothing and go once the query is rebuilt.
  std::vector<std::string_view> kept;
  bool removed = false;
  for (std::string_view param :
       base::SplitStringPiece(url.query(), "&", base::KEEP_WHITESPACE,
                              base::SPLIT_WANT_NONEMPTY)) {
    if (IsTracker(param.substr(0, param.find('=')), site_trackers, url)) {
      removed = true;
    } else {
      kept.push_back(param);
    }
  }
  if (!removed) {
    return url;
  }
  const std::string query = base::JoinString(kept, "&");
  GURL::Replacements replacements;
  if (query.empty()) {
    replacements.ClearQuery();
  } else {
    replacements.SetQueryStr(query);
  }
  return url.ReplaceComponents(replacements);
}

std::u16string MarkdownLink(std::u16string_view title, const GURL& url) {
  std::u16string text = base::CollapseWhitespace(
      title, /*trim_sequences_with_line_breaks=*/false);
  const std::string& spec = url.possibly_invalid_spec();
  if (text.empty()) {
    text = base::UTF8ToUTF16(url.has_host() ? url.host() : spec);
  }
  return base::StrCat({u"[", EscapeMarkdownText(text), u"](",
                       base::UTF8ToUTF16(EscapeMarkdownUrl(spec)), u")"});
}

std::optional<GURL> LinkToText(const GURL& url, std::u16string_view selection) {
  if (!url.SchemeIsHTTPOrHTTPS()) {
    return std::nullopt;
  }
  std::vector<std::u16string> lines;
  for (std::u16string_view line :
       base::SplitStringPiece(selection, kBlockBreaks, base::TRIM_WHITESPACE,
                              base::SPLIT_WANT_NONEMPTY)) {
    lines.push_back(base::CollapseWhitespace(
        line, /*trim_sequences_with_line_breaks=*/false));
  }
  if (lines.empty()) {
    return std::nullopt;
  }
  // The page's fragment can be an anchor or a hash route the link needs, so
  // only an old directive is dropped.
  std::string_view page_fragment = url.ref();
  page_fragment =
      page_fragment.substr(0, page_fragment.find(kDirectiveDelimiter));
  const std::string fragment = base::StrCat(
      {page_fragment, kDirectiveDelimiter, "text=", TextDirectiveValue(lines)});
  GURL::Replacements replacements;
  replacements.SetRefStr(fragment);
  return url.ReplaceComponents(replacements);
}

}  // namespace origin_copy_link
