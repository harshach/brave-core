// Copyright (c) 2026 The Brave Authors. All rights reserved.
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this file,
// You can obtain one at https://mozilla.org/MPL/2.0/.

#include "brave/browser/ui/tabs/origin_copy_link.h"

#include <array>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "base/strings/escape.h"
#include "base/strings/strcat.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/string_split.h"
#include "base/strings/string_util.h"
#include "base/strings/utf_string_conversions.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "url/gurl.h"

namespace origin_copy_link {

namespace {

// Dropped on every site: the .cc's list, some campaign tags and Marketo's
// token.
constexpr auto kEverySiteTrackers = std::to_array<std::string_view>({
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
    "mkt_tok",
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
    "utm_campaign",
    "utm_content",
    "utm_creative_format",
    "utm_id",
    "utm_marketing_tactic",
    "utm_medium",
    "utm_source",
    "utm_source_platform",
    "utm_term",
    "vero_conv",
    "vero_id",
    "vgo_ee",
    "wbraid",
    "wickedid",
    "yclid",
    "ymclid",
    "ysclid",
});

constexpr std::string_view kTextDirective = ":~:text=";

std::string Clean(std::string_view url) {
  return RemoveTrackingParameters(GURL(url)).possibly_invalid_spec();
}

// The spec LinkToText() gives `url` for `selection`, or "none".
std::string Link(std::string_view url, std::u16string_view selection) {
  const std::optional<GURL> link = LinkToText(GURL(url), selection);
  if (!link) {
    return "none";
  }
  EXPECT_TRUE(link->is_valid());
  return link->spec();
}

// The text directive of Link(), percent-decoded, split at the ',' between a
// range's start and end.
std::vector<std::u16string> DecodedDirective(std::string_view url,
                                             std::u16string_view selection) {
  const std::string link = Link(url, selection);
  const size_t directive = link.find(kTextDirective);
  if (directive == std::string::npos) {
    return {};
  }
  std::vector<std::u16string> parts;
  for (std::string_view part : base::SplitStringPiece(
           std::string_view(link).substr(directive + kTextDirective.size()),
           ",", base::KEEP_WHITESPACE, base::SPLIT_WANT_ALL)) {
    parts.push_back(base::UTF8ToUTF16(base::UnescapeBinaryURLComponent(part)));
  }
  return parts;
}

// "w001 w002 ...", `count` four-character words: 5 * count - 1 characters.
std::u16string Words(int count) {
  std::vector<std::u16string> words;
  for (int i = 1; i <= count; ++i) {
    std::u16string number = base::NumberToString16(i);
    words.push_back(u"w" + std::u16string(3 - number.size(), u'0') + number);
  }
  return base::JoinString(words, u" ");
}

// `text` escaped the only way plain words and spaces need to be.
std::string Spaces(std::u16string_view text) {
  std::string escaped = base::UTF16ToUTF8(text);
  base::ReplaceSubstringsAfterOffset(&escaped, 0, " ", "%20");
  return escaped;
}

}  // namespace

TEST(OriginCopyLinkTest, RemovesEachTracker) {
  for (std::string_view name : kEverySiteTrackers) {
    SCOPED_TRACE(name);
    EXPECT_EQ(Clean(base::StrCat(
                  {"https://example.com/page?", name, "=abc123&id=7"})),
              "https://example.com/page?id=7");
    EXPECT_EQ(Clean(base::StrCat(
                  {"https://example.com/page?id=7&", name, "=abc123"})),
              "https://example.com/page?id=7");
  }
}

TEST(OriginCopyLinkTest, RemovesAnyCampaignTag) {
  EXPECT_EQ(Clean("https://example.com/?utm_source=news&utm_medium=email&"
                  "utm_campaign=fall&utm_brand=x&utm_=y&id=1"),
            "https://example.com/?id=1");
}

TEST(OriginCopyLinkTest, KeepsParametersSitesNeed) {
  // YouTube's video, start time and playlist; Google's query and paging.
  EXPECT_EQ(Clean("https://www.youtube.com/watch?v=dQw4w9WgXcQ&t=42s&"
                  "list=PL123&index=2"),
            "https://www.youtube.com/watch?v=dQw4w9WgXcQ&t=42s&list=PL123&"
            "index=2");
  EXPECT_EQ(Clean("https://www.google.com/search?q=brave+browser&tbm=isch&"
                  "start=10"),
            "https://www.google.com/search?q=brave+browser&tbm=isch&start=10");

  // Names only match whole.
  EXPECT_EQ(
      Clean("https://example.com/?utm=1&gclid2=2&my_fbclid=3&xsi=4&"
            "mkt_token=5"),
      "https://example.com/?utm=1&gclid2=2&my_fbclid=3&xsi=4&mkt_token=5");
}

TEST(OriginCopyLinkTest, KeepsOtherParametersInOrderAsWritten) {
  EXPECT_EQ(Clean("https://example.com/p?b=2&utm_source=x&a=1&fbclid=y&"
                  "c=%7E3&a=4"),
            "https://example.com/p?b=2&a=1&c=%7E3&a=4");
  EXPECT_EQ(Clean("https://example.com/p?q=brave+browser&eq=a=b&gclid=1"),
            "https://example.com/p?q=brave+browser&eq=a=b");
  // Trackers inside another parameter's value are part of that value.
  EXPECT_EQ(Clean("https://example.com/r?to=https%3A%2F%2Fother.com%2F%3F"
                  "utm_source%3Dx&gclid=1"),
            "https://example.com/r?to=https%3A%2F%2Fother.com%2F%3F"
            "utm_source%3Dx");
}

TEST(OriginCopyLinkTest, KeepsTheRestOfTheUrl) {
  EXPECT_EQ(Clean("https://example.com/doc?utm_source=x#section-2"),
            "https://example.com/doc#section-2");
  EXPECT_EQ(Clean("https://example.com/doc?id=1&gclid=2#frag?utm_source=x"),
            "https://example.com/doc?id=1#frag?utm_source=x");
  EXPECT_EQ(Clean("https://example.com/?fbclid=1#:~:text=hi"),
            "https://example.com/#:~:text=hi");
  EXPECT_EQ(Clean("https://user:pw@example.com:8443/a/b.html?gclid=1&x=2"),
            "https://user:pw@example.com:8443/a/b.html?x=2");
  EXPECT_EQ(Clean("http://example.com/?fbclid=1"), "http://example.com/");
}

TEST(OriginCopyLinkTest, DropsTheQueryWhenNothingIsLeft) {
  EXPECT_EQ(Clean("https://example.com/?utm_source=a&fbclid=b&gclid=c"),
            "https://example.com/");
  EXPECT_EQ(Clean("https://example.com/?gclid=1&gclid=2"),
            "https://example.com/");
  // With no value, or an empty one.
  EXPECT_EQ(Clean("https://example.com/?fbclid"), "https://example.com/");
  EXPECT_EQ(Clean("https://example.com/?fbclid=&id=1"),
            "https://example.com/?id=1");
  // Stray '&'s go along once something is removed.
  EXPECT_EQ(Clean("https://example.com/?utm_source=a&&id=1&"),
            "https://example.com/?id=1");
}

TEST(OriginCopyLinkTest, LeavesUrlsWithoutTrackersAsTheyAre) {
  EXPECT_EQ(Clean("https://example.com/?a=1&&b=2&"),
            "https://example.com/?a=1&&b=2&");
  EXPECT_EQ(Clean("https://example.com/?"), "https://example.com/?");
  EXPECT_EQ(Clean("https://example.com/path"), "https://example.com/path");
}

TEST(OriginCopyLinkTest, RemovesShareTrackersOnTheirSitesOnly) {
  EXPECT_EQ(Clean("https://www.youtube.com/watch?v=dQw4w9WgXcQ&si=AbC123&"
                  "t=42s&pp=ygUFaGVsbG8%3D&feature=shared"),
            "https://www.youtube.com/watch?v=dQw4w9WgXcQ&t=42s");
  EXPECT_EQ(Clean("https://youtu.be/dQw4w9WgXcQ?si=AbC123&t=10"),
            "https://youtu.be/dQw4w9WgXcQ?t=10");
  EXPECT_EQ(Clean("https://music.youtube.com/watch?v=abc&si=x"),
            "https://music.youtube.com/watch?v=abc");
  EXPECT_EQ(Clean("https://m.youtube.com/watch?feature=youtu.be&v=abc"),
            "https://m.youtube.com/watch?v=abc");
  EXPECT_EQ(Clean("https://open.spotify.com/track/4cOdK2wGLETKBW3PvgPWqT?"
                  "si=a1b2c3"),
            "https://open.spotify.com/track/4cOdK2wGLETKBW3PvgPWqT");
  EXPECT_EQ(Clean("https://x.com/brave/status/1234567890?s=20&t=AbCdEf"),
            "https://x.com/brave/status/1234567890");
  EXPECT_EQ(Clean("https://mobile.twitter.com/brave/status/1?s=21"),
            "https://mobile.twitter.com/brave/status/1");
  EXPECT_EQ(Clean("https://twitter.com/brave/status/1234567890?ref_src="
                  "twsrc%5Etfw&ref_url=https%3A%2F%2Fexample.com%2F"),
            "https://twitter.com/brave/status/1234567890");
  EXPECT_EQ(Clean("https://x.com/search?q=brave&src=typed_query&f=live"),
            "https://x.com/search?q=brave&src=typed_query&f=live");

  // Elsewhere the same names stay, including on lookalike hosts.
  EXPECT_EQ(Clean("https://example.com/?si=1&pp=2&feature=3&ref_src=4&"
                  "ref_url=5&s=6&t=7"),
            "https://example.com/?si=1&pp=2&feature=3&ref_src=4&ref_url=5&s=6&"
            "t=7");
  EXPECT_EQ(Clean("https://fox.com/video?t=10&s=1"),
            "https://fox.com/video?t=10&s=1");
  EXPECT_EQ(Clean("https://notyoutube.com/watch?v=1&si=2"),
            "https://notyoutube.com/watch?v=1&si=2");
  EXPECT_EQ(Clean("https://youtube.com.example.net/watch?v=1&si=2"),
            "https://youtube.com.example.net/watch?v=1&si=2");
}

TEST(OriginCopyLinkTest, KeepsMarketoTokenWherePagesNeedIt) {
  EXPECT_EQ(Clean("https://example.com/blog/post?mkt_tok=NjU5LVd&id=1"),
            "https://example.com/blog/post?id=1");
  EXPECT_EQ(Clean("https://pages.example.com/UnsubscribePage.html?"
                  "mkt_unsubscribe=1&mkt_tok=NjU5LVd&utm_source=x"),
            "https://pages.example.com/UnsubscribePage.html?"
            "mkt_unsubscribe=1&mkt_tok=NjU5LVd");
  EXPECT_EQ(Clean("https://info.example.com/index.php/email/emailWebview?"
                  "mkt_tok=NjU5LVd"),
            "https://info.example.com/index.php/email/emailWebview?"
            "mkt_tok=NjU5LVd");
}

TEST(OriginCopyLinkTest, LeavesUrlDefenseLinksWhole) {
  EXPECT_EQ(Clean("https://urldefense.com/v3/__https://example.com/?"
                  "utm_source=x__;!!ABC$"),
            "https://urldefense.com/v3/__https://example.com/?"
            "utm_source=x__;!!ABC$");
}

TEST(OriginCopyLinkTest, LeavesOtherSchemesAlone) {
  EXPECT_EQ(Clean("file:///tmp/a.html?utm_source=x"),
            "file:///tmp/a.html?utm_source=x");
  EXPECT_EQ(Clean("ftp://example.com/?utm_source=x"),
            "ftp://example.com/?utm_source=x");
  EXPECT_EQ(Clean("chrome://settings/?utm_source=x"),
            "chrome://settings/?utm_source=x");
  EXPECT_EQ(Clean("mailto:a@example.com?utm_source=x"),
            "mailto:a@example.com?utm_source=x");
  EXPECT_FALSE(RemoveTrackingParameters(GURL("not a url")).is_valid());
}

TEST(OriginCopyLinkTest, CleanUrlWithoutSanitizerUsesTheBuiltInList) {
  EXPECT_EQ(CleanUrl(nullptr, GURL("https://www.youtube.com/watch?v=abc&"
                                   "si=x&utm_source=y#comments")),
            GURL("https://www.youtube.com/watch?v=abc#comments"));
  EXPECT_EQ(CleanUrl(nullptr, GURL("https://example.com/?a=1&&b=2")),
            GURL("https://example.com/?a=1&&b=2"));
  EXPECT_EQ(CleanUrl(nullptr, GURL("file:///tmp/a.html?gclid=1")),
            GURL("file:///tmp/a.html?gclid=1"));
}

TEST(OriginCopyLinkTest, MarkdownLink) {
  EXPECT_EQ(MarkdownLink(u"Brave Search", GURL("https://search.brave.com/")),
            u"[Brave Search](https://search.brave.com/)");
  EXPECT_EQ(MarkdownLink(u"Results", GURL("https://example.com/s?q=a+b&n=1")),
            u"[Results](https://example.com/s?q=a+b&n=1)");
}

TEST(OriginCopyLinkTest, MarkdownLinkEscapesTitle) {
  EXPECT_EQ(MarkdownLink(u"[Draft] Notes \\ ideas [v2]",
                         GURL("https://example.com/")),
            u"[\\[Draft\\] Notes \\\\ ideas \\[v2\\]](https://example.com/)");
  EXPECT_EQ(
      MarkdownLink(u"<input>: The Input element", GURL("https://example.com/")),
      u"[\\<input>: The Input element](https://example.com/)");
  // Other Markdown stays as written.
  EXPECT_EQ(MarkdownLink(u"*Bold* _claims_ `code` (aside) #1 > 2!",
                         GURL("https://example.com/")),
            u"[*Bold* _claims_ `code` (aside) #1 > 2!](https://example.com/)");
}

TEST(OriginCopyLinkTest, MarkdownLinkCollapsesWhitespace) {
  EXPECT_EQ(MarkdownLink(u"  Line one\n\n  line\ttwo\r\n ",
                         GURL("https://example.com/")),
            u"[Line one line two](https://example.com/)");
  EXPECT_EQ(MarkdownLink(u"a\u00a0b", GURL("https://example.com/")),
            u"[a b](https://example.com/)");
}

TEST(OriginCopyLinkTest, MarkdownLinkFallsBackToTheHost) {
  EXPECT_EQ(MarkdownLink(u"", GURL("https://www.example.com/path?q=1")),
            u"[www.example.com](https://www.example.com/path?q=1)");
  EXPECT_EQ(MarkdownLink(u" \n\t ", GURL("https://www.example.com/")),
            u"[www.example.com](https://www.example.com/)");
  EXPECT_EQ(MarkdownLink(u"", GURL("http://[::1]:8080/")),
            u"[\\[::1\\]](http://[::1]:8080/)");
  // Or to the whole URL when there's no host.
  EXPECT_EQ(MarkdownLink(u"", GURL("about:blank")),
            u"[about:blank](about:blank)");
}

TEST(OriginCopyLinkTest, MarkdownLinkEncodesUrl) {
  EXPECT_EQ(MarkdownLink(u"Python", GURL("https://en.wikipedia.org/wiki/"
                                         "Python_(programming_language)")),
            u"[Python](https://en.wikipedia.org/wiki/"
            u"Python_%28programming_language%29)");
  // GURL keeps '\' in a query, where Markdown would read it as an escape.
  EXPECT_EQ(MarkdownLink(u"Path", GURL("https://example.com/?p=C:\\dir")),
            u"[Path](https://example.com/?p=C:%5Cdir)");
  // GURL keeps spaces and angle brackets in data: URLs.
  EXPECT_EQ(MarkdownLink(u"Note", GURL("data:text/plain,a b<c>(d)")),
            u"[Note](data:text/plain,a%20b%3Cc%3E%28d%29)");
  // Existing escapes stay as they are.
  EXPECT_EQ(MarkdownLink(u"Doc", GURL("https://example.com/a%20b?q=%28x%29")),
            u"[Doc](https://example.com/a%20b?q=%28x%29)");
  // A link to text, whose directive may have parentheses.
  const std::optional<GURL> quote =
      LinkToText(GURL("https://example.com/"), u"see (this)");
  ASSERT_TRUE(quote);
  EXPECT_EQ(MarkdownLink(u"Quote", *quote),
            u"[Quote](https://example.com/#:~:text=see%20%28this%29)");
}

TEST(OriginCopyLinkTest, LinkToText) {
  EXPECT_EQ(Link("https://example.com/page", u"hello world"),
            "https://example.com/page#:~:text=hello%20world");
  EXPECT_EQ(Link("https://example.com/search?q=1", u"hello"),
            "https://example.com/search?q=1#:~:text=hello");
  EXPECT_EQ(Link("http://example.com/", u"hello"),
            "http://example.com/#:~:text=hello");
}

TEST(OriginCopyLinkTest, LinkToTextEscapes) {
  // The directive's delimiters, and '#' and '%'.
  EXPECT_EQ(Link("https://example.com/", u"well-known, fast & free #1: 100%"),
            "https://example.com/#:~:text=well%2Dknown%2C%20fast%20%26%20free%"
            "20%231%3A%20100%25");
  // What the spec allows as is.
  EXPECT_EQ(Link("https://example.com/", u"It's (really) *ok*! a_b~c."),
            "https://example.com/#:~:text=It's%20(really)%20*ok*!%20a_b~c.");
  // Other ASCII punctuation.
  EXPECT_EQ(
      Link("https://example.com/", u"a/b?c=d;e@f$g+h\"i<j>k[l]{m}|n\\o^p`q"),
      "https://example.com/#:~:text=a%2Fb%3Fc%3Dd%3Be%40f%24g%2Bh%22i%"
      "3Cj%3Ek%5Bl%5D%7Bm%7D%7Cn%5Co%5Ep%60q");
  // Anything else as UTF-8.
  EXPECT_EQ(Link("https://example.com/", u"caf\u00e9 \u65e5\u672c \U0001F44D"),
            "https://example.com/#:~:text=caf%C3%A9%20%E6%97%A5%E6%9C%AC%20%"
            "F0%9F%91%8D");
  EXPECT_EQ(
      DecodedDirective("https://example.com/", u"well-known, fast & free"),
      std::vector<std::u16string>{u"well-known, fast & free"});
}

TEST(OriginCopyLinkTest, LinkToTextCollapsesWhitespace) {
  EXPECT_EQ(Link("https://example.com/", u"  hello \u00a0  world  "),
            "https://example.com/#:~:text=hello%20world");
}

TEST(OriginCopyLinkTest, LinkToTextNeedsTextAndAWebPage) {
  EXPECT_EQ(Link("https://example.com/", u""), "none");
  EXPECT_EQ(Link("https://example.com/", u"  \n\t\u00a0\r\n "), "none");

  EXPECT_EQ(Link("file:///tmp/a.html", u"hello"), "none");
  EXPECT_EQ(Link("chrome://settings/", u"hello"), "none");
  EXPECT_EQ(Link("ftp://example.com/", u"hello"), "none");
  EXPECT_EQ(Link("data:text/html,hello", u"hello"), "none");
  EXPECT_EQ(Link("about:blank", u"hello"), "none");
  EXPECT_EQ(Link("not a url", u"hello"), "none");
}

TEST(OriginCopyLinkTest, LinkToTextKeepsThePageFragment) {
  EXPECT_EQ(Link("https://example.com/doc#section-2", u"hello"),
            "https://example.com/doc#section-2:~:text=hello");
  // A hash route picks the page.
  EXPECT_EQ(Link("https://docs.example.com/#/guide/intro", u"hello"),
            "https://docs.example.com/#/guide/intro:~:text=hello");
  EXPECT_EQ(Link("https://example.com/#", u"hello"),
            "https://example.com/#:~:text=hello");
}

TEST(OriginCopyLinkTest, LinkToTextReplacesAnOldDirective) {
  EXPECT_EQ(Link("https://example.com/#:~:text=old", u"new"),
            "https://example.com/#:~:text=new");
  EXPECT_EQ(Link("https://example.com/doc#intro:~:text=old&text=older", u"new"),
            "https://example.com/doc#intro:~:text=new");
}

TEST(OriginCopyLinkTest, LinkToTextMatchesUpTo300CharactersWhole) {
  const std::u16string longest = Words(60) + u"x";
  ASSERT_EQ(longest.size(), 300u);
  EXPECT_EQ(Link("https://example.com/", longest),
            "https://example.com/#:~:text=" + Spaces(longest));

  const std::u16string too_long = Words(60) + u"xy";
  EXPECT_EQ(Link("https://example.com/", too_long),
            "https://example.com/#:~:text=w001%20w002%20w003%20w004%20w005,"
            "w056%20w057%20w058%20w059%20w060xy");
}

TEST(OriginCopyLinkTest, LinkToTextUsesARangeForLongSelections) {
  std::u16string prose;
  for (int i = 0; i < 8; ++i) {
    prose += u"The quick brown fox jumps over the lazy dog. ";
  }
  prose += u"Last words of this long-winded selection end here.";
  EXPECT_EQ(Link("https://example.com/", prose),
            "https://example.com/#:~:text=The%20quick%20brown%20fox%20jumps,"
            "long%2Dwinded%20selection%20end%20here.");

  // Whitespace runs are collapsed before counting.
  EXPECT_EQ(Link("https://example.com/", u"  " + Words(70) + u"  "),
            "https://example.com/#:~:text=w001%20w002%20w003%20w004%20w005,"
            "w066%20w067%20w068%20w069%20w070");

  // With fewer than ten words, the halves don't overlap.
  const std::u16string a(80, u'a');
  const std::u16string b(80, u'b');
  const std::u16string c(80, u'c');
  const std::u16string d(80, u'd');
  EXPECT_EQ(Link("https://example.com/", a + u" " + b + u" " + c + u" " + d),
            "https://example.com/#:~:text=" + Spaces(a + u" " + b) + "," +
                Spaces(c + u" " + d));

  // A single long word can only be matched whole.
  const std::u16string word(400, u'a');
  EXPECT_EQ(Link("https://example.com/", word),
            "https://example.com/#:~:text=" + base::UTF16ToUTF8(word));

  // Words in scripts without spaces count too.
  std::u16string japanese;
  for (int i = 0; i < 30; ++i) {
    japanese +=
        u"\u65e5\u672c\u8a9e\u306e\u30c6\u30ad"
        u"\u30b9\u30c8\u3092\u9078\u629e\u3057"
        u"\u307e\u3057\u305f\u3002";
  }
  const std::vector<std::u16string> range =
      DecodedDirective("https://example.com/", japanese);
  ASSERT_EQ(range.size(), 2u);
  EXPECT_TRUE(japanese.starts_with(range[0]));
  EXPECT_TRUE(japanese.ends_with(range[1]));
  EXPECT_LT(range[0].size(), 20u);
  EXPECT_LT(range[1].size(), 20u);
}

TEST(OriginCopyLinkTest, LinkToTextUsesARangeAcrossLines) {
  // Each end comes from its own line, up to five words.
  EXPECT_EQ(Link("https://example.com/",
                 u"Chapter 1\nIt was a bright cold day in April, and the "
                 u"clocks were striking thirteen."),
            "https://example.com/#:~:text=Chapter%201,"
            "the%20clocks%20were%20striking%20thirteen.");
  EXPECT_EQ(Link("https://example.com/",
                 u"Alpha beta gamma delta epsilon zeta\n"
                 u"Eta theta iota kappa lambda mu"),
            "https://example.com/#:~:text=Alpha%20beta%20gamma%20delta%"
            "20epsilon,theta%20iota%20kappa%20lambda%20mu");
  // Any line break, blank lines, and table cells.
  EXPECT_EQ(Link("https://example.com/", u"one two\r\n\r\nthree four"),
            "https://example.com/#:~:text=one%20two,three%20four");
  EXPECT_EQ(Link("https://example.com/", u"one\u2028two"),
            "https://example.com/#:~:text=one,two");
  EXPECT_EQ(Link("https://example.com/", u"Name\tAge\nBob\t42"),
            "https://example.com/#:~:text=Name,42");
  // Blank lines alone don't make a range.
  EXPECT_EQ(Link("https://example.com/", u"\n\n  one   two \n"),
            "https://example.com/#:~:text=one%20two");
}

}  // namespace origin_copy_link
