// Copyright (c) 2026 The Brave Authors. All rights reserved.
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this file,
// You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef BRAVE_BROWSER_UI_TABS_ORIGIN_COPY_LINK_H_
#define BRAVE_BROWSER_UI_TABS_ORIGIN_COPY_LINK_H_

#include <optional>
#include <string>
#include <string_view>

#include "url/gurl.h"

namespace brave {
class URLSanitizerService;
}  // namespace brave

// The forms the address pill copies a page's link in, after Arc: a clean link
// without trackers, a Markdown link, and a link that scrolls to and highlights
// selected text.
namespace origin_copy_link {

// `url` without tracking parameters. Brave's URL sanitizer runs first when
// there is one; the built-in list (RemoveTrackingParameters()) runs after it
// either way, since the sanitizer's rules arrive with a component and are
// empty until it loads. `sanitizer` may be null.
GURL CleanUrl(brave::URLSanitizerService* sanitizer, const GURL& url);

// `url` without the query parameters on the built-in list. Only http(s) URLs
// change, and only by losing parameters: the scheme, host, path, fragment and
// the other parameters, in order and as written, stay.
//  - On every site: utm_* campaign tags, ad click IDs (gclid, fbclid, msclkid,
//    ...) and email and marketing trackers (mc_cid, mc_eid, _hsenc, ...); the
//    .cc has the full list. Marketo's mkt_tok stays on unsubscribe and "view
//    as web page" links, which need it.
//  - Share trackers, only on the sites that add them: YouTube's si, pp and
//    feature (v, t and list stay), Spotify's si, and Twitter/X's ref_src,
//    ref_url, s and t.
// urldefense.com links stay whole: a checksum covers the link they wrap, and
// that link's query shows up as theirs.
GURL RemoveTrackingParameters(const GURL& url);

// "[title](url)" for pasting into Markdown. Whitespace runs in `title`,
// newlines included, become single spaces and the ends are trimmed; '\', '['
// and ']' are escaped so the link holds together, and '<' so a title like
// "<input>" isn't read as HTML. An empty title reads as the URL's host, or as
// the whole URL when it has none. In the URL, spaces, '(', ')', '<', '>' and
// '\' are percent-encoded, as they would end the link early or read as escapes.
std::u16string MarkdownLink(std::u16string_view title, const GURL& url);

// `url` with a text directive ("#:~:text=...") that scrolls to and highlights
// `selection` (https://wicg.github.io/scroll-to-text-fragment/), or nullopt
// when `url` isn't http(s) or `selection` is blank. The page's own fragment,
// an anchor or a hash route, stays in front of the directive, and any
// directive already there is replaced.
//
// Whitespace runs in `selection` read as single spaces. A selection on one
// line of up to 300 characters is matched whole. A longer one becomes a range
// from its first five words to its last five, so the URL stays short. So does
// a selection across lines or table cells, as a whole match can't cross
// blocks: its range runs from the first line's opening words to the last
// line's closing words. The text is escaped as Chrome's "Copy link to
// highlight" escapes it, with everything but letters, digits and !'()*._~
// percent-encoded as UTF-8.
std::optional<GURL> LinkToText(const GURL& url, std::u16string_view selection);

}  // namespace origin_copy_link

#endif  // BRAVE_BROWSER_UI_TABS_ORIGIN_COPY_LINK_H_
