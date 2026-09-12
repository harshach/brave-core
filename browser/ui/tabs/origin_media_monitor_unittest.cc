// Copyright (c) 2026 The Brave Authors. All rights reserved.
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this file,
// You can obtain one at https://mozilla.org/MPL/2.0/.

#include "brave/browser/ui/tabs/origin_media_monitor.h"

#include <memory>
#include <string>
#include <utility>

#include "base/memory/raw_ptr.h"
#include "base/test/scoped_feature_list.h"
#include "brave/browser/ui/views/frame/origin_widgets/origin_youtube_widget.h"
#include "brave/browser/workspaces/features.h"
#include "brave/browser/workspaces/workspace_service.h"
#include "brave/browser/workspaces/workspace_service_factory.h"
#include "chrome/browser/extensions/chrome_extension_web_contents_observer.h"
#include "chrome/browser/extensions/test_extension_system.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/browser_window/public/browser_window_features.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/test/base/browser_with_test_window_test.h"
#include "components/sessions/content/session_tab_helper.h"
#include "content/public/browser/media_session.h"
#include "content/public/browser/media_session_player_observer.h"
#include "content/public/browser/web_contents.h"
#include "content/public/test/web_contents_tester.h"
#include "extensions/browser/extension_system.h"
#include "media/base/media_content_type.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace {

class TestMediaPlayer : public content::MediaSessionPlayerObserver {
 public:
  explicit TestMediaPlayer(content::WebContents* contents)
      : contents_(contents) {}
  void OnSuspend(int player_id, bool triggered_by_user) override {
    paused = true;
    ++pause_count;
  }
  void OnResume(int player_id, bool triggered_by_user) override {
    paused = false;
    ++play_count;
  }
  bool HasAudio(int player_id) const override { return true; }
  bool IsPaused(int player_id) const override { return paused; }
  media::MediaContentType GetMediaContentType() const override {
    return media::MediaContentType::kPersistent;
  }
  content::RenderFrameHost* render_frame_host() const override {
    return contents_->GetPrimaryMainFrame();
  }
  bool paused = false;
  int pause_count = 0;
  int play_count = 0;

 private:
  raw_ptr<content::WebContents> contents_;
};

class OriginMediaMonitorTest : public BrowserWithTestWindowTest {
 public:
  OriginMediaMonitorTest() {
    feature_list_.InitAndEnableFeature(features::kWorkspaces);
    WorkspaceServiceFactory::GetInstance();
  }

  void SetUp() override {
    BrowserWithTestWindowTest::SetUp();
    auto* extension_system = static_cast<extensions::TestExtensionSystem*>(
        extensions::ExtensionSystem::Get(profile()));
    ASSERT_TRUE(extension_system);
    extension_system->Init();
    controller_ = browser()->GetFeatures().origin_space_controller();
    monitor_ = browser()->GetFeatures().origin_media_monitor();
    ASSERT_TRUE(controller_);
    ASSERT_TRUE(monitor_);
  }

  void TearDown() override {
    monitor_ = nullptr;
    controller_ = nullptr;
    BrowserWithTestWindowTest::TearDown();
  }

 protected:
  content::WebContents* AddPage() {
    auto contents = content::WebContentsTester::CreateTestWebContents(
        profile(), /*site_instance=*/nullptr);
    auto* page = contents.get();
    extensions::ChromeExtensionWebContentsObserver::CreateForWebContents(page);
    browser()->tab_strip_model()->AppendWebContents(std::move(contents), false);
    return page;
  }

  base::test::ScopedFeatureList feature_list_;
  raw_ptr<OriginSpaceController> controller_ = nullptr;
  raw_ptr<OriginMediaMonitor> monitor_ = nullptr;
};

TEST_F(OriginMediaMonitorTest, AudibleStateClearsWhenPlaybackStops) {
  const std::string space_id = controller_->active_space_id();
  auto* page = AddPage();
  EXPECT_FALSE(monitor_->IsSpaceAudible(space_id));

  content::WebContentsTester::For(page)->SetIsCurrentlyAudible(true);
  EXPECT_TRUE(monitor_->IsSpaceAudible(space_id));

  content::WebContentsTester::For(page)->SetIsCurrentlyAudible(false);
  ASSERT_TRUE(page->WasEverAudible());
  EXPECT_FALSE(monitor_->IsSpaceAudible(space_id));
}

TEST_F(OriginMediaMonitorTest, MutedPlaybackHasNoSoundIndicator) {
  const std::string space_id = controller_->active_space_id();
  auto* page = AddPage();
  content::WebContentsTester::For(page)->SetIsCurrentlyAudible(true);
  ASSERT_TRUE(monitor_->IsSpaceAudible(space_id));

  monitor_->SetSpaceMuted(space_id, true);
  ASSERT_TRUE(page->IsAudioMuted());
  EXPECT_FALSE(monitor_->IsSpaceAudible(space_id));

  monitor_->ToggleSpaceMuted(space_id);
  EXPECT_TRUE(monitor_->IsSpaceAudible(space_id));

  content::WebContentsTester::For(page)->SetIsCurrentlyAudible(false);
  monitor_->SetSpaceMuted(space_id, true);
  EXPECT_FALSE(monitor_->IsSpaceAudible(space_id));
  monitor_->SetSpaceMuted(space_id, false);
  EXPECT_FALSE(monitor_->IsSpaceAudible(space_id));
}

TEST_F(OriginMediaMonitorTest, AnyAudibleUnmutedPageKeepsIndicatorVisible) {
  const std::string space_id = controller_->active_space_id();
  auto* first = AddPage();
  auto* second = AddPage();
  content::WebContentsTester::For(first)->SetIsCurrentlyAudible(true);
  content::WebContentsTester::For(second)->SetIsCurrentlyAudible(true);

  first->SetAudioMuted(true);
  EXPECT_TRUE(monitor_->IsSpaceAudible(space_id));
  content::WebContentsTester::For(second)->SetIsCurrentlyAudible(false);
  EXPECT_FALSE(monitor_->IsSpaceAudible(space_id));
  first->SetAudioMuted(false);
  EXPECT_TRUE(monitor_->IsSpaceAudible(space_id));
}

TEST_F(OriginMediaMonitorTest, SoundFollowsPageSpaceAndDisappearsOnClose) {
  const std::string home = controller_->active_space_id();
  auto* page = AddPage();
  content::WebContentsTester::For(page)->SetIsCurrentlyAudible(true);
  const auto& spaces =
      WorkspaceServiceFactory::GetForProfile(profile())->GetOriginSpaces();
  ASSERT_GT(spaces.size(), 1u);
  const std::string other = spaces[1].id;

  controller_->MoveTabToSpace(page, other);
  EXPECT_FALSE(monitor_->IsSpaceAudible(home));
  EXPECT_TRUE(monitor_->IsSpaceAudible(other));

  auto* model = browser()->tab_strip_model();
  model->CloseWebContentsAt(model->GetIndexOfWebContents(page),
                            TabCloseTypes::CLOSE_NONE);
  EXPECT_FALSE(monitor_->IsSpaceAudible(other));
}

TEST_F(OriginMediaMonitorTest, PlayPauseWorksWithoutSiteActionHandlers) {
  auto* page = AddPage();
  auto* session = content::MediaSession::Get(page);
  TestMediaPlayer player(page);
  ASSERT_TRUE(session->AddPlayer(&player, 0));
  const auto id = sessions::SessionTabHelper::IdForTab(page);

  monitor_->TogglePlayPause(id);
  EXPECT_EQ(player.pause_count, 1);
  EXPECT_TRUE(player.paused);
  monitor_->TogglePlayPause(id);
  EXPECT_EQ(player.play_count, 1);
  EXPECT_FALSE(player.paused);
  session->RemovePlayer(&player, 0);
}

TEST_F(OriginMediaMonitorTest, YouTubeWidgetFollowsPageVisibility) {
  auto* page = AddPage();
  content::WebContentsTester::For(page)->NavigateAndCommit(
      GURL("https://www.youtube.com/watch?v=test"));
  content::WebContentsTester::For(page)->SetIsCurrentlyAudible(true);
  auto* session = content::MediaSession::Get(page);
  TestMediaPlayer player(page);
  ASSERT_TRUE(session->AddPlayer(&player, 0));
  auto* model = browser()->tab_strip_model();
  model->ActivateTabAt(model->GetIndexOfWebContents(page));
  {
    OriginYouTubeWidgetView widget({.browser = browser(),
                                    .media_monitor = monitor_,
                                    .prefs = profile()->GetPrefs()});
    EXPECT_FALSE(widget.GetVisible());
    auto* other = AddPage();
    model->ActivateTabAt(model->GetIndexOfWebContents(other));
    EXPECT_TRUE(widget.GetVisible());
    model->ActivateTabAt(model->GetIndexOfWebContents(page));
    EXPECT_FALSE(widget.GetVisible());
  }
  session->RemovePlayer(&player, 0);
}

}  // namespace
