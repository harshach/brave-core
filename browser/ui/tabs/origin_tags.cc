// Copyright (c) 2026 The Brave Authors. All rights reserved.
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this file,
// You can obtain one at https://mozilla.org/MPL/2.0/.

#include "brave/browser/ui/tabs/origin_tags.h"

#include <algorithm>
#include <set>
#include <utility>

#include "base/i18n/case_conversion.h"
#include "base/location.h"
#include "base/memory/raw_ptr.h"
#include "base/strings/string_util.h"
#include "base/strings/utf_string_conversions.h"
#include "components/bookmarks/browser/bookmark_model.h"
#include "components/bookmarks/browser/bookmark_node.h"
#include "components/bookmarks/browser/scoped_group_bookmark_actions.h"
#include "components/bookmarks/common/bookmark_metrics.h"

namespace origin_tags {

namespace {

using bookmarks::BookmarkModel;
using bookmarks::BookmarkNode;

// A tag and the folders behind it. Two devices can each create a tag, or the
// tags folder itself, before they sync, which leaves folders that share a
// name; a tag gathers all of them. The first names the tag and takes its new
// pages.
struct TagFolders {
  std::u16string name;
  std::u16string key;
  std::vector<raw_ptr<const BookmarkNode, VectorExperimental>> folders;
};

std::u16string KeyFor(std::u16string_view text) {
  return base::i18n::FoldCase(text);
}

bool Contains(std::u16string_view haystack, std::u16string_view needle) {
  return haystack.find(needle) != std::u16string_view::npos;
}

bool IsLoaded(const BookmarkModel* model) {
  return model && model->loaded() && model->other_node();
}

// The "Socket Tags" folders: normally one, see TagFolders.
std::vector<const BookmarkNode*> GetTagsFolders(const BookmarkModel* model) {
  std::vector<const BookmarkNode*> roots;
  if (!IsLoaded(model)) {
    return roots;
  }
  for (const auto& child : model->other_node()->children()) {
    if (child->is_folder() && child->GetTitle() == kTagsFolderTitle) {
      roots.push_back(child.get());
    }
  }
  return roots;
}

// Sorted by key. Only for reading: the folders must not be removed while
// this is alive.
std::vector<TagFolders> GetAllTagFolders(const BookmarkModel* model) {
  std::vector<TagFolders> tags;
  for (const BookmarkNode* root : GetTagsFolders(model)) {
    for (const auto& child : root->children()) {
      if (!child->is_folder()) {
        continue;
      }
      std::u16string name = NormalizeName(child->GetTitle());
      if (name.empty()) {
        continue;
      }
      std::u16string key = KeyFor(name);
      auto existing = std::ranges::find(tags, key, &TagFolders::key);
      if (existing != tags.end()) {
        existing->folders.push_back(child.get());
        continue;
      }
      TagFolders& tag = tags.emplace_back();
      tag.name = std::move(name);
      tag.key = std::move(key);
      tag.folders.push_back(child.get());
    }
  }
  std::ranges::sort(tags, {}, &TagFolders::key);
  return tags;
}

const TagFolders* FindTag(const std::vector<TagFolders>& tags,
                          std::u16string_view name) {
  const std::u16string normalized = NormalizeName(name);
  if (normalized.empty()) {
    return nullptr;
  }
  auto tag = std::ranges::find(tags, KeyFor(normalized), &TagFolders::key);
  return tag == tags.end() ? nullptr : &*tag;
}

// The folders behind `name`, first one first, for changing the model.
std::vector<const BookmarkNode*> GetFoldersForTag(const BookmarkModel* model,
                                                  std::u16string_view name) {
  const std::vector<TagFolders> tags = GetAllTagFolders(model);
  const TagFolders* tag = FindTag(tags, name);
  if (!tag) {
    return {};
  }
  return std::vector<const BookmarkNode*>(tag->folders.begin(),
                                          tag->folders.end());
}

// The bookmarks for `url` directly inside `folders`.
std::vector<const BookmarkNode*> GetBookmarksIn(
    const std::vector<const BookmarkNode*>& folders,
    const GURL& url) {
  std::vector<const BookmarkNode*> nodes;
  for (const BookmarkNode* folder : folders) {
    for (const auto& child : folder->children()) {
      if (child->is_url() && child->url() == url) {
        nodes.push_back(child.get());
      }
    }
  }
  return nodes;
}

std::vector<Page> GetPagesIn(const TagFolders& tag) {
  std::vector<Page> pages;
  std::set<GURL> listed;
  for (const BookmarkNode* folder : tag.folders) {
    for (const auto& child : folder->children()) {
      if (child->is_url() && listed.insert(child->url()).second) {
        pages.push_back({child->GetTitle(), child->url()});
      }
    }
  }
  return pages;
}

}  // namespace

std::u16string NormalizeName(std::u16string_view name) {
  std::u16string_view rest = base::TrimWhitespace(name, base::TRIM_ALL);
  while (!rest.empty() && rest.front() == u'#') {
    rest = base::TrimWhitespace(rest.substr(1), base::TRIM_LEADING);
  }
  return base::CollapseWhitespace(rest,
                                  /*trim_sequences_with_line_breaks=*/false);
}

std::vector<Tag> GetTags(const BookmarkModel* model) {
  std::vector<Tag> tags;
  for (const TagFolders& tag : GetAllTagFolders(model)) {
    tags.push_back({tag.name, GetPagesIn(tag).size()});
  }
  return tags;
}

std::vector<std::u16string> GetTagsForUrl(const BookmarkModel* model,
                                          const GURL& url) {
  std::vector<std::u16string> names;
  if (!IsLoaded(model) || !url.is_valid()) {
    return names;
  }
  std::set<const BookmarkNode*> parents;
  for (const BookmarkNode* node : model->GetNodesByURL(url)) {
    parents.insert(node->parent());
  }
  if (parents.empty()) {
    return names;
  }
  for (const TagFolders& tag : GetAllTagFolders(model)) {
    if (std::ranges::any_of(tag.folders, [&](const BookmarkNode* folder) {
          return parents.contains(folder);
        })) {
      names.push_back(tag.name);
    }
  }
  return names;
}

void SetTagged(BookmarkModel* model,
               const std::u16string& tag,
               const GURL& url,
               const std::u16string& title,
               bool tagged) {
  const std::u16string name = NormalizeName(tag);
  if (!IsLoaded(model) || name.empty() || !url.is_valid()) {
    return;
  }
  const std::vector<const BookmarkNode*> folders =
      GetFoldersForTag(model, name);
  const std::vector<const BookmarkNode*> bookmarked =
      GetBookmarksIn(folders, url);
  if (tagged == !bookmarked.empty()) {
    return;
  }

  // One step for undo, however many nodes it takes.
  bookmarks::ScopedGroupBookmarkActions group(model);
  if (!tagged) {
    // Duplicates included, or the page would stay tagged.
    for (const BookmarkNode* node : bookmarked) {
      model->Remove(node, bookmarks::metrics::BookmarkEditSource::kUser,
                    FROM_HERE);
    }
    return;
  }
  const BookmarkNode* folder = folders.empty() ? nullptr : folders.front();
  if (!folder) {
    const std::vector<const BookmarkNode*> roots = GetTagsFolders(model);
    const BookmarkNode* root = roots.empty() ? nullptr : roots.front();
    if (!root) {
      const BookmarkNode* other = model->other_node();
      root =
          model->AddFolder(other, other->children().size(), kTagsFolderTitle);
    }
    folder = model->AddFolder(root, root->children().size(), name);
  }
  model->AddNewURL(folder, folder->children().size(), title, url);
}

void DeleteTag(BookmarkModel* model, const std::u16string& tag) {
  const std::vector<const BookmarkNode*> folders = GetFoldersForTag(model, tag);
  if (folders.empty()) {
    return;
  }
  bookmarks::ScopedGroupBookmarkActions group(model);
  for (const BookmarkNode* folder : folders) {
    model->Remove(folder, bookmarks::metrics::BookmarkEditSource::kUser,
                  FROM_HERE);
  }
}

std::vector<Page> GetPages(const BookmarkModel* model,
                           const std::u16string& tag) {
  const std::vector<TagFolders> tags = GetAllTagFolders(model);
  const TagFolders* found = FindTag(tags, tag);
  return found ? GetPagesIn(*found) : std::vector<Page>();
}

std::vector<Match> Search(const BookmarkModel* model,
                          std::u16string_view tag_prefix,
                          std::u16string_view text,
                          size_t max_results) {
  std::vector<Match> matches;
  if (max_results == 0) {
    return matches;
  }
  const std::u16string prefix = KeyFor(NormalizeName(tag_prefix));
  const std::u16string needle =
      KeyFor(base::TrimWhitespace(text, base::TRIM_ALL));
  std::set<GURL> listed;
  for (const TagFolders& tag : GetAllTagFolders(model)) {
    if (!tag.key.starts_with(prefix)) {
      continue;
    }
    for (Page& page : GetPagesIn(tag)) {
      if (listed.contains(page.url)) {
        continue;
      }
      if (!needle.empty() && !Contains(KeyFor(page.title), needle) &&
          !Contains(KeyFor(base::UTF8ToUTF16(page.url.spec())), needle)) {
        continue;
      }
      listed.insert(page.url);
      matches.push_back({std::move(page), tag.name});
      if (matches.size() == max_results) {
        return matches;
      }
    }
  }
  return matches;
}

}  // namespace origin_tags
