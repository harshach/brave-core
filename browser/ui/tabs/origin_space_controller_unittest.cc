// Copyright (c) 2026 The Brave Authors. All rights reserved.
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this file,
// You can obtain one at https://mozilla.org/MPL/2.0/.

#include "brave/browser/ui/tabs/origin_space_controller.h"

#include <map>
#include <memory>
#include <string>

#include "base/memory/raw_ptr.h"
#include "base/test/scoped_feature_list.h"
#include "brave/browser/sessions/brave_session_keys.h"
#include "brave/browser/workspaces/features.h"
#include "brave/browser/workspaces/workspace_service.h"
#include "brave/browser/workspaces/workspace_service_factory.h"
#include "brave/components/constants/webui_url_constants.h"
#include "chrome/browser/extensions/chrome_extension_web_contents_observer.h"
#include "chrome/browser/extensions/test_extension_system.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/browser_window/public/browser_window_features.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/test/base/browser_with_test_window_test.h"
#include "content/public/browser/web_contents.h"
#include "content/public/test/web_contents_tester.h"
#include "extensions/browser/extension_system.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace {

class OriginSpaceControllerTest : public BrowserWithTestWindowTest {
 public:
  OriginSpaceControllerTest() {
    feature_list_.InitAndEnableFeature(features::kWorkspaces);
    // Register the keyed service before BrowserWithTestWindowTest creates its
    // profile so the Origin spaces preference is available.
    WorkspaceServiceFactory::GetInstance();
  }

  void SetUp() override {
    BrowserWithTestWindowTest::SetUp();
    workspace_service_ = WorkspaceServiceFactory::GetForProfile(profile());
    ASSERT_TRUE(workspace_service_);
    auto* extension_system = static_cast<extensions::TestExtensionSystem*>(
        extensions::ExtensionSystem::Get(profile()));
    ASSERT_TRUE(extension_system);
    extension_system->Init();
    controller_ = browser()->GetFeatures().origin_space_controller();
    ASSERT_TRUE(controller_);
  }

  void TearDown() override {
    controller_ = nullptr;
    workspace_service_ = nullptr;
    BrowserWithTestWindowTest::TearDown();
  }

 protected:
  content::WebContents* AddTab(bool foreground) {
    auto contents = content::WebContentsTester::CreateTestWebContents(
        profile(), /*site_instance=*/nullptr);
    content::WebContents* raw_contents = contents.get();
    extensions::ChromeExtensionWebContentsObserver::CreateForWebContents(
        raw_contents);
    browser()->tab_strip_model()->AppendWebContents(std::move(contents),
                                                    foreground);
    return raw_contents;
  }

  base::test::ScopedFeatureList feature_list_;
  raw_ptr<OriginSpaceController> controller_ = nullptr;
  raw_ptr<WorkspaceService> workspace_service_ = nullptr;
};

TEST_F(OriginSpaceControllerTest, KeepsTabsIsolatedAndRemembersSelection) {
  const std::string home_id = controller_->active_space_id();
  content::WebContents* home_first = AddTab(/*foreground=*/true);
  content::WebContents* home_second = AddTab(/*foreground=*/true);
  EXPECT_EQ(controller_->GetSpaceIdForTab(home_first), home_id);
  EXPECT_EQ(controller_->GetSpaceIdForTab(home_second), home_id);

  const std::string work_id =
      workspace_service_->CreateOriginSpace("Work", "✏️");
  ASSERT_TRUE(controller_->SelectSpace(work_id));
  EXPECT_FALSE(controller_->ActiveSpaceHasTabs());

  content::WebContents* work_tab = AddTab(/*foreground=*/true);
  EXPECT_EQ(controller_->GetSpaceIdForTab(work_tab), work_id);
  EXPECT_TRUE(controller_->IsTabInActiveSpace(work_tab));
  EXPECT_FALSE(controller_->IsTabInActiveSpace(home_first));

  ASSERT_TRUE(controller_->SelectSpace(home_id));
  EXPECT_EQ(browser()->tab_strip_model()->GetActiveWebContents(), home_second);
  browser()->tab_strip_model()->ActivateTabAt(0);
  EXPECT_EQ(browser()->tab_strip_model()->GetActiveWebContents(), home_first);

  ASSERT_TRUE(controller_->SelectSpace(work_id));
  EXPECT_EQ(browser()->tab_strip_model()->GetActiveWebContents(), work_tab);
  ASSERT_TRUE(controller_->SelectSpace(home_id));
  EXPECT_EQ(browser()->tab_strip_model()->GetActiveWebContents(), home_first);
}

TEST_F(OriginSpaceControllerTest, AdjacentSpaceNavigationWraps) {
  const std::string home_id = controller_->active_space_id();
  const auto& spaces = workspace_service_->GetOriginSpaces();
  ASSERT_EQ(spaces.size(), 5u);

  ASSERT_TRUE(controller_->SelectAdjacentSpace(/*next=*/true));
  EXPECT_EQ(controller_->active_space_id(), spaces[1].id);
  ASSERT_TRUE(controller_->SelectAdjacentSpace(/*next=*/true));
  EXPECT_EQ(controller_->active_space_id(), spaces[2].id);

  ASSERT_TRUE(controller_->SelectSpace(spaces.back().id));
  ASSERT_TRUE(controller_->SelectAdjacentSpace(/*next=*/true));
  EXPECT_EQ(controller_->active_space_id(), home_id);

  ASSERT_TRUE(controller_->SelectAdjacentSpace(/*next=*/false));
  EXPECT_EQ(controller_->active_space_id(), spaces.back().id);
}

TEST_F(OriginSpaceControllerTest, SelectsSpaceByRailPosition) {
  const std::string home_id = controller_->active_space_id();
  const auto& spaces = workspace_service_->GetOriginSpaces();
  ASSERT_EQ(spaces.size(), 5u);

  EXPECT_TRUE(controller_->SelectSpaceAtIndex(1));
  EXPECT_EQ(controller_->active_space_id(), spaces[1].id);
  EXPECT_TRUE(controller_->SelectSpaceAtIndex(2));
  EXPECT_EQ(controller_->active_space_id(), spaces[2].id);
  EXPECT_TRUE(controller_->SelectSpaceAtIndex(0));
  EXPECT_EQ(controller_->active_space_id(), home_id);
  EXPECT_FALSE(controller_->SelectSpaceAtIndex(9));
  EXPECT_EQ(controller_->active_space_id(), home_id);
}

TEST_F(OriginSpaceControllerTest, RestoresAnEmptySelectedSpaceForTheWindow) {
  const std::string home_id = controller_->active_space_id();
  const std::string empty_id =
      workspace_service_->CreateOriginSpace("Planning", "📝");

  controller_->BeginWindowRestore({{kBraveOriginActiveSpaceIdKey, empty_id}});
  EXPECT_EQ(controller_->active_space_id(), empty_id);

  content::WebContents* restored_tab = AddTab(/*foreground=*/true);
  controller_->MaybeRestoreTabSpace(restored_tab,
                                    {{kBraveOriginSpaceIdKey, home_id}});
  controller_->FinishWindowRestore();

  EXPECT_EQ(controller_->active_space_id(), empty_id);
  EXPECT_FALSE(controller_->ActiveSpaceHasTabs());
  EXPECT_EQ(controller_->GetSpaceIdForTab(restored_tab), home_id);

  std::map<std::string, std::string> saved_window_data;
  controller_->MaybePopulateWindowExtraData(&saved_window_data);
  EXPECT_EQ(saved_window_data[kBraveOriginActiveSpaceIdKey], empty_id);
}

TEST_F(OriginSpaceControllerTest, InvalidRestoreDataFallsBackToHome) {
  const std::string home_id = controller_->active_space_id();
  controller_->BeginWindowRestore(
      {{kBraveOriginActiveSpaceIdKey, "missing-space"}});
  controller_->FinishWindowRestore();
  EXPECT_EQ(controller_->active_space_id(), home_id);

  content::WebContents* restored_tab = AddTab(/*foreground=*/true);
  controller_->MaybeRestoreTabSpace(
      restored_tab, {{kBraveOriginSpaceIdKey, "missing-space"}});
  EXPECT_EQ(controller_->GetSpaceIdForTab(restored_tab), home_id);
}

TEST_F(OriginSpaceControllerTest, NewTabCanvasIsNotCountedAsAPage) {
  content::WebContents* contents = AddTab(/*foreground=*/true);
  auto* tester = content::WebContentsTester::For(contents);
  tester->NavigateAndCommit(GURL(kBraveUINewTabURL));

  EXPECT_TRUE(controller_->ActiveSpaceHasTabs());
  EXPECT_TRUE(controller_->IsTabPlaceholder(contents));
  EXPECT_FALSE(controller_->ShouldShowTabInPageList(contents));
  EXPECT_EQ(controller_->GetPageCountForSpace(controller_->active_space_id()),
            0u);

  tester->NavigateAndCommit(GURL("https://example.com/"));
  EXPECT_FALSE(controller_->IsTabPlaceholder(contents));
  EXPECT_TRUE(controller_->ShouldShowTabInPageList(contents));
  EXPECT_EQ(controller_->GetPageCountForSpace(controller_->active_space_id()),
            1u);
}

TEST_F(OriginSpaceControllerTest, PinnedTabsUseTheirOwnSectionCount) {
  content::WebContents* pinned = AddTab(/*foreground=*/true);
  content::WebContentsTester::For(pinned)->NavigateAndCommit(
      GURL("https://example.com/pinned"));
  browser()->tab_strip_model()->SetTabPinned(0, true);

  content::WebContents* page = AddTab(/*foreground=*/true);
  content::WebContentsTester::For(page)->NavigateAndCommit(
      GURL("https://example.com/page"));

  EXPECT_TRUE(controller_->ShouldShowTabInPageList(pinned));
  EXPECT_EQ(controller_->GetPageCountForSpace(controller_->active_space_id()),
            1u);
}

}  // namespace
