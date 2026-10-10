// Copyright (c) 2026 The Brave Authors. All rights reserved.
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this file,
// You can obtain one at https://mozilla.org/MPL/2.0/.

#include "brave/browser/ui/tabs/origin_tags.h"

#include <memory>
#include <string>
#include <vector>

#include "base/strings/string_number_conversions.h"
#include "base/test/task_environment.h"
#include "components/bookmarks/browser/bookmark_model.h"
#include "components/bookmarks/browser/bookmark_node.h"
#include "components/bookmarks/test/test_bookmark_client.h"
#include "testing/gmock/include/gmock/gmock.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "url/gurl.h"

namespace origin_tags {

namespace {

using bookmarks::BookmarkNode;
using ::testing::ElementsAre;
using ::testing::IsEmpty;

class OriginTagsTest : public testing::Test {
 protected:
  bookmarks::BookmarkModel* model() { return model_.get(); }

  void AddTag(const std::u16string& tag,
              const GURL& url,
              const std::u16string& title = u"Title") {
    SetTagged(model(), tag, url, title, /*tagged=*/true);
  }

  void RemoveTag(const std::u16string& tag, const GURL& url) {
    SetTagged(model(), tag, url, std::u16string(), /*tagged=*/false);
  }

  // "name:count" per tag, in order.
  std::vector<std::u16string> DescribeTags() {
    std::vector<std::u16string> described;
    for (const Tag& tag : GetTags(model())) {
      described.push_back(tag.name + u":" +
                          base::NumberToString16(tag.page_count));
    }
    return described;
  }

  std::vector<GURL> PageUrls(const std::u16string& tag) {
    std::vector<GURL> urls;
    for (const Page& page : GetPages(model(), tag)) {
      urls.push_back(page.url);
    }
    return urls;
  }

  // "tag/title" per match, in order.
  static std::vector<std::u16string> Describe(
      const std::vector<Match>& matches) {
    std::vector<std::u16string> described;
    for (const Match& match : matches) {
      described.push_back(match.tag + u"/" + match.page.title);
    }
    return described;
  }

  std::vector<const BookmarkNode*> TagsFolders() {
    std::vector<const BookmarkNode*> folders;
    for (const auto& child : model()->other_node()->children()) {
      if (child->is_folder() && child->GetTitle() == kTagsFolderTitle) {
        folders.push_back(child.get());
      }
    }
    return folders;
  }

 private:
  base::test::TaskEnvironment task_environment_;
  std::unique_ptr<bookmarks::BookmarkModel> model_ =
      bookmarks::TestBookmarkClient::CreateModel();
};

TEST_F(OriginTagsTest, TagsAndUntagsAPage) {
  const GURL page("https://example.com/article");
  AddTag(u"work", page, u"Article");

  EXPECT_THAT(DescribeTags(), ElementsAre(u"work:1"));
  EXPECT_THAT(GetTagsForUrl(model(), page), ElementsAre(u"work"));
  const std::vector<Page> pages = GetPages(model(), u"work");
  ASSERT_EQ(pages.size(), 1u);
  EXPECT_EQ(pages[0].title, u"Article");
  EXPECT_EQ(pages[0].url, page);

  // A plain bookmark: Other Bookmarks > Socket Tags > work > Article.
  const std::vector<const BookmarkNode*> roots = TagsFolders();
  ASSERT_EQ(roots.size(), 1u);
  ASSERT_EQ(roots[0]->children().size(), 1u);
  const BookmarkNode* folder = roots[0]->children()[0].get();
  EXPECT_TRUE(folder->is_folder());
  EXPECT_EQ(folder->GetTitle(), u"work");
  ASSERT_EQ(folder->children().size(), 1u);
  EXPECT_EQ(folder->children()[0]->url(), page);
  EXPECT_EQ(folder->children()[0]->GetTitle(), u"Article");

  // Tagging twice adds nothing.
  AddTag(u"work", page, u"Article");
  EXPECT_EQ(folder->children().size(), 1u);

  // The tag outlives its last page.
  RemoveTag(u"work", page);
  EXPECT_THAT(GetTagsForUrl(model(), page), IsEmpty());
  EXPECT_THAT(DescribeTags(), ElementsAre(u"work:0"));
  EXPECT_FALSE(model()->IsBookmarked(page));

  // Untagging a page that isn't tagged, or from a tag that doesn't exist,
  // changes nothing.
  RemoveTag(u"work", page);
  RemoveTag(u"missing", page);
  EXPECT_THAT(DescribeTags(), ElementsAre(u"work:0"));
}

TEST_F(OriginTagsTest, PageCanHaveSeveralTags) {
  const GURL a("https://a.example/");
  const GURL b("https://b.example/");
  AddTag(u"work", a);
  AddTag(u"reading", a);
  AddTag(u"work", b);

  EXPECT_THAT(GetTagsForUrl(model(), a), ElementsAre(u"reading", u"work"));
  EXPECT_THAT(GetTagsForUrl(model(), b), ElementsAre(u"work"));
  EXPECT_THAT(DescribeTags(), ElementsAre(u"reading:1", u"work:2"));
  EXPECT_THAT(PageUrls(u"work"), ElementsAre(a, b));
  EXPECT_EQ(TagsFolders().size(), 1u);

  RemoveTag(u"work", a);
  EXPECT_THAT(GetTagsForUrl(model(), a), ElementsAre(u"reading"));
  EXPECT_THAT(DescribeTags(), ElementsAre(u"reading:1", u"work:1"));
  EXPECT_TRUE(model()->IsBookmarked(a));
}

TEST_F(OriginTagsTest, NamesMatchCaseInsensitivelyAndKeepTheirCasing) {
  const GURL a("https://a.example/");
  const GURL b("https://b.example/");
  AddTag(u"Work", a);
  AddTag(u"WORK", b);
  AddTag(u"  #work ", b);

  EXPECT_THAT(DescribeTags(), ElementsAre(u"Work:2"));
  EXPECT_THAT(GetTagsForUrl(model(), b), ElementsAre(u"Work"));
  EXPECT_THAT(PageUrls(u"wOrK"), ElementsAre(a, b));

  RemoveTag(u"work", a);
  EXPECT_THAT(PageUrls(u"Work"), ElementsAre(b));

  AddTag(u"beta", a);
  AddTag(u"Alpha", a);
  AddTag(u"gamma", a);
  EXPECT_THAT(GetTagsForUrl(model(), a),
              ElementsAre(u"Alpha", u"beta", u"gamma"));
  EXPECT_THAT(DescribeTags(),
              ElementsAre(u"Alpha:1", u"beta:1", u"gamma:1", u"Work:1"));
}

TEST_F(OriginTagsTest, NormalizesTypedNames) {
  EXPECT_EQ(NormalizeName(u"  #Read   later "), u"Read later");
  EXPECT_EQ(NormalizeName(u"## work"), u"work");
  EXPECT_EQ(NormalizeName(u"c#"), u"c#");
  EXPECT_EQ(NormalizeName(u" # "), u"");

  // No name left, or nothing to tag.
  AddTag(u"#", GURL("https://a.example/"));
  AddTag(u"work", GURL());
  EXPECT_THAT(GetTags(model()), IsEmpty());
  EXPECT_THAT(TagsFolders(), IsEmpty());
}

TEST_F(OriginTagsTest, DeleteTagRemovesItsPages) {
  const GURL a("https://a.example/");
  const GURL b("https://b.example/");
  AddTag(u"work", a);
  AddTag(u"work", b);
  AddTag(u"reading", a);

  DeleteTag(model(), u"WORK");
  EXPECT_THAT(DescribeTags(), ElementsAre(u"reading:1"));
  EXPECT_THAT(GetTagsForUrl(model(), a), ElementsAre(u"reading"));
  EXPECT_THAT(GetTagsForUrl(model(), b), IsEmpty());
  EXPECT_FALSE(model()->IsBookmarked(b));

  DeleteTag(model(), u"missing");
  EXPECT_THAT(DescribeTags(), ElementsAre(u"reading:1"));
}

TEST_F(OriginTagsTest, SearchFiltersByTagPrefixAndText) {
  const GURL report("https://docs.example/q3-report");
  const GURL wiki("https://wiki.example/team");
  const GURL plan("https://run.example/plan");
  const GURL story("https://news.example/story");
  AddTag(u"work", report, u"Quarterly report");
  AddTag(u"work", wiki, u"Team wiki");
  AddTag(u"Workout", plan, u"Running plan");
  AddTag(u"reading", story, u"Long read");
  AddTag(u"reading", report, u"Quarterly report");

  // Every page once, under the first tag it is in.
  EXPECT_THAT(Describe(Search(model(), u"", u"", 10)),
              ElementsAre(u"reading/Long read", u"reading/Quarterly report",
                          u"work/Team wiki", u"Workout/Running plan"));

  // The tag by prefix, in any case, with or without its '#'.
  EXPECT_THAT(Describe(Search(model(), u"WORK", u"", 10)),
              ElementsAre(u"work/Quarterly report", u"work/Team wiki",
                          u"Workout/Running plan"));
  EXPECT_THAT(Describe(Search(model(), u"#workout", u"", 10)),
              ElementsAre(u"Workout/Running plan"));

  // Text matches titles and URLs.
  EXPECT_THAT(Describe(Search(model(), u"work", u"REPORT", 10)),
              ElementsAre(u"work/Quarterly report"));
  EXPECT_THAT(Describe(Search(model(), u"", u" wiki.example ", 10)),
              ElementsAre(u"work/Team wiki"));
  EXPECT_THAT(Describe(Search(model(), u"rea", u"example/story", 10)),
              ElementsAre(u"reading/Long read"));

  EXPECT_THAT(Describe(Search(model(), u"", u"", 2)),
              ElementsAre(u"reading/Long read", u"reading/Quarterly report"));
  EXPECT_THAT(Search(model(), u"", u"", 0), IsEmpty());
  EXPECT_THAT(Search(model(), u"travel", u"", 10), IsEmpty());
  EXPECT_THAT(Search(model(), u"work", u"recipe", 10), IsEmpty());
}

TEST_F(OriginTagsTest, IgnoresOtherBookmarks) {
  const GURL a("https://a.example/");
  const GURL b("https://b.example/");
  const GURL c("https://c.example/");
  const GURL d("https://d.example/");
  AddTag(u"work", a);
  const BookmarkNode* root = TagsFolders()[0];
  const BookmarkNode* work = root->children()[0].get();

  // A page loose in the tags folder, a folder inside a tag, a "Socket Tags"
  // folder outside Other Bookmarks, and a page titled "Socket Tags".
  model()->AddURL(root, root->children().size(), u"Loose", b);
  const BookmarkNode* nested =
      model()->AddFolder(work, work->children().size(), u"nested");
  model()->AddURL(nested, 0, u"Nested", c);
  const BookmarkNode* bar_root =
      model()->AddFolder(model()->bookmark_bar_node(), 0, kTagsFolderTitle);
  const BookmarkNode* bar_tag = model()->AddFolder(bar_root, 0, u"elsewhere");
  model()->AddURL(bar_tag, 0, u"Elsewhere", d);
  model()->AddURL(model()->other_node(), 0, kTagsFolderTitle, d);

  EXPECT_THAT(DescribeTags(), ElementsAre(u"work:1"));
  EXPECT_THAT(PageUrls(u"work"), ElementsAre(a));
  EXPECT_THAT(GetTagsForUrl(model(), b), IsEmpty());
  EXPECT_THAT(GetTagsForUrl(model(), c), IsEmpty());
  EXPECT_THAT(GetTagsForUrl(model(), d), IsEmpty());

  // New tags still go to the one tags folder.
  AddTag(u"reading", b);
  EXPECT_EQ(TagsFolders().size(), 1u);
  EXPECT_THAT(GetTagsForUrl(model(), b), ElementsAre(u"reading"));
}

TEST_F(OriginTagsTest, MergesFoldersDuplicatedBySync) {
  const GURL a("https://a.example/");
  const GURL b("https://b.example/");
  const GURL c("https://c.example/");
  // Two devices each made the tags folder and a "work" tag before syncing.
  const BookmarkNode* other = model()->other_node();
  const BookmarkNode* root1 = model()->AddFolder(other, 0, kTagsFolderTitle);
  const BookmarkNode* root2 = model()->AddFolder(other, 1, kTagsFolderTitle);
  const BookmarkNode* work1 = model()->AddFolder(root1, 0, u"Work");
  const BookmarkNode* work2 = model()->AddFolder(root2, 0, u"work");
  model()->AddURL(work1, 0, u"A", a);
  model()->AddURL(work2, 0, u"A", a);
  model()->AddURL(work2, 1, u"B", b);

  EXPECT_THAT(DescribeTags(), ElementsAre(u"Work:2"));
  EXPECT_THAT(PageUrls(u"work"), ElementsAre(a, b));
  EXPECT_THAT(GetTagsForUrl(model(), b), ElementsAre(u"Work"));

  // Untagging clears every copy; new pages go to the first folder.
  RemoveTag(u"work", a);
  EXPECT_THAT(GetTagsForUrl(model(), a), IsEmpty());
  AddTag(u"work", c);
  ASSERT_EQ(work1->children().size(), 1u);
  EXPECT_EQ(work1->children()[0]->url(), c);

  DeleteTag(model(), u"work");
  EXPECT_THAT(GetTags(model()), IsEmpty());
  EXPECT_TRUE(root1->children().empty());
  EXPECT_TRUE(root2->children().empty());
}

TEST_F(OriginTagsTest, DoesNothingBeforeTheModelLoads) {
  const GURL a("https://a.example/");
  auto unloaded = std::make_unique<bookmarks::BookmarkModel>(
      std::make_unique<bookmarks::TestBookmarkClient>());
  ASSERT_FALSE(unloaded->loaded());

  SetTagged(unloaded.get(), u"work", a, u"A", /*tagged=*/true);
  DeleteTag(unloaded.get(), u"work");
  EXPECT_THAT(GetTags(unloaded.get()), IsEmpty());
  EXPECT_THAT(GetTagsForUrl(unloaded.get(), a), IsEmpty());
  EXPECT_THAT(GetPages(unloaded.get(), u"work"), IsEmpty());
  EXPECT_THAT(Search(unloaded.get(), u"", u"", 10), IsEmpty());

  SetTagged(nullptr, u"work", a, u"A", /*tagged=*/true);
  EXPECT_THAT(GetTags(nullptr), IsEmpty());
}

}  // namespace

}  // namespace origin_tags
