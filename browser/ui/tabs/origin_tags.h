// Copyright (c) 2026 The Brave Authors. All rights reserved.
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this file,
// You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef BRAVE_BROWSER_UI_TABS_ORIGIN_TAGS_H_
#define BRAVE_BROWSER_UI_TABS_ORIGIN_TAGS_H_

#include <stddef.h>

#include <string>
#include <string_view>
#include <vector>

#include "url/gurl.h"

namespace bookmarks {
class BookmarkModel;
}  // namespace bookmarks

// Page tags, after SigmaOS: each tag is like a bookmark folder, but a page can
// have several. Tags are plain bookmarks, so they persist and sync without
// storage of their own: a "Socket Tags" folder in Other Bookmarks holds one
// folder per tag, and tagging a page bookmarks it in that tag's folder.
//
// Tag names match case-insensitively but keep the casing they were created
// with. Until the model has loaded, every read is empty and every change is
// dropped.
namespace origin_tags {

inline constexpr char16_t kTagsFolderTitle[] = u"Socket Tags";

struct Tag {
  std::u16string name;
  // Distinct pages in the tag.
  size_t page_count = 0;
};

struct Page {
  std::u16string title;
  GURL url;
};

struct Match {
  Page page;
  std::u16string tag;
};

// The form a typed tag name is stored in: whitespace collapsed and trimmed,
// and leading '#'s dropped, since '#' is how Quick Open filters by tag. Empty
// if nothing is left. Every function here normalizes the names it is given.
std::u16string NormalizeName(std::u16string_view name);

// Sorted case-insensitively by name.
std::vector<Tag> GetTags(const bookmarks::BookmarkModel* model);

// The tags `url` is in, in GetTags() order.
std::vector<std::u16string> GetTagsForUrl(const bookmarks::BookmarkModel* model,
                                          const GURL& url);

// Adds `url` to `tag` or removes it from it. Adding creates the tag (and the
// tags folder) on first use and does nothing if the page already has the tag.
// Removing the last page leaves the tag; DeleteTag() removes it.
void SetTagged(bookmarks::BookmarkModel* model,
               const std::u16string& tag,
               const GURL& url,
               const std::u16string& title,
               bool tagged);

// Removes `tag` along with its pages' bookmarks.
void DeleteTag(bookmarks::BookmarkModel* model, const std::u16string& tag);

// The pages in `tag`, in the order they were tagged, one per URL.
std::vector<Page> GetPages(const bookmarks::BookmarkModel* model,
                           const std::u16string& tag);

// For Quick Open's "#" filter: pages in tags whose name starts with
// `tag_prefix` (empty matches every tag) whose title or URL contains `text`
// (empty matches every page), both case-insensitively. Tags are visited in
// GetTags() order and a page is listed once, with the first tag it matched in.
// At most `max_results`.
std::vector<Match> Search(const bookmarks::BookmarkModel* model,
                          std::u16string_view tag_prefix,
                          std::u16string_view text,
                          size_t max_results);

}  // namespace origin_tags

#endif  // BRAVE_BROWSER_UI_TABS_ORIGIN_TAGS_H_
