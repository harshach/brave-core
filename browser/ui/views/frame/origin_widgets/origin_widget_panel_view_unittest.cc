// Copyright (c) 2026 The Brave Authors. All rights reserved.
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this file,
// You can obtain one at https://mozilla.org/MPL/2.0/.

#include "brave/browser/ui/views/frame/origin_widgets/origin_widget_panel_view.h"

#include <memory>
#include <utility>

#include "base/files/scoped_temp_dir.h"
#include "base/test/scoped_feature_list.h"
#include "base/test/test_future.h"
#include "base/values.h"
#include "brave/browser/ui/views/frame/origin_widgets/origin_widget_registry.h"
#include "brave/browser/workspaces/features.h"
#include "brave/browser/workspaces/pref_names.h"
#include "brave/browser/workspaces/workspace_service_factory.h"
#include "chrome/test/base/browser_with_test_window_test.h"
#include "components/prefs/json_pref_store.h"
#include "components/prefs/pref_service.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace {

class OriginWidgetPanelViewTest : public BrowserWithTestWindowTest {
 public:
  OriginWidgetPanelViewTest() {
    feature_list_.InitAndEnableFeature(features::kWorkspaces);
    WorkspaceServiceFactory::GetInstance();
  }

 private:
  base::test::ScopedFeatureList feature_list_;
};

TEST_F(OriginWidgetPanelViewTest, RemovingAllWidgetsPersistsAcrossRecreation) {
  auto panel = std::make_unique<OriginWidgetPanelView>(browser(), nullptr);
  const int count = static_cast<int>(GetOriginWidgetCatalog().size());
  for (int command = 1; command <= count; ++command) {
    ASSERT_TRUE(panel->IsCommandIdChecked(command));
    panel->ExecuteCommand(command, 0);
  }
  EXPECT_TRUE(
      profile()->GetPrefs()->GetList(kOriginWidgetsEnabledPref).empty());

  panel = std::make_unique<OriginWidgetPanelView>(browser(), nullptr);
  for (int command = 1; command <= count; ++command) {
    EXPECT_FALSE(panel->IsCommandIdChecked(command));
  }

  panel->ExecuteCommand(1, 0);
  EXPECT_TRUE(panel->IsCommandIdChecked(1));
  EXPECT_EQ(profile()->GetPrefs()->GetList(kOriginWidgetsEnabledPref).size(),
            1u);
}

TEST_F(OriginWidgetPanelViewTest, VisibilityTracksSharedPreference) {
  OriginWidgetPanelView first(browser(), nullptr);
  OriginWidgetPanelView second(browser(), nullptr);
  ASSERT_TRUE(first.GetVisible());
  ASSERT_TRUE(second.GetVisible());

  first.SetPanelOpen(false);
  EXPECT_FALSE(first.GetVisible());
  EXPECT_FALSE(second.GetVisible());
  second.TogglePanel();
  EXPECT_TRUE(first.GetVisible());
  EXPECT_TRUE(second.GetVisible());
}

TEST_F(OriginWidgetPanelViewTest, DuplicateAndUnknownWidgetIdsAreIgnored) {
  base::ListValue ids;
  ids.Append("youtube");
  ids.Append("youtube");
  ids.Append("removed-widget");
  profile()->GetPrefs()->SetList(kOriginWidgetsEnabledPref, std::move(ids));
  OriginWidgetPanelView panel(browser(), nullptr);

  ASSERT_TRUE(panel.IsCommandIdChecked(1));
  panel.ExecuteCommand(1, 0);
  EXPECT_FALSE(panel.IsCommandIdChecked(1));
  EXPECT_TRUE(
      profile()->GetPrefs()->GetList(kOriginWidgetsEnabledPref).empty());
}

TEST_F(OriginWidgetPanelViewTest, WidgetSettingsSurviveDiskRoundTrip) {
  base::ScopedTempDir directory;
  ASSERT_TRUE(directory.CreateUniqueTempDir());
  const auto path = directory.GetPath().AppendASCII("Preferences");
  {
    auto store = base::MakeRefCounted<JsonPrefStore>(path);
    store->ReadPrefs();
    base::ListValue enabled;
    enabled.Append("youtube");
    enabled.Append("calendar");
    store->SetValue(kOriginWidgetsEnabledPref, base::Value(std::move(enabled)),
                    0);
    store->SetValue(kOriginWidgetPanelVisiblePref, base::Value(false), 0);
    store->SetValue(kOriginCalendarGoogleTokenPref,
                    base::Value("encrypted-test-token"), 0);
    base::test::TestFuture<void> written;
    store->CommitPendingWrite(written.GetCallback());
    ASSERT_TRUE(written.Wait());
  }
  auto restored = base::MakeRefCounted<JsonPrefStore>(path);
  EXPECT_EQ(restored->ReadPrefs(), PersistentPrefStore::PREF_READ_ERROR_NONE);
  const base::Value* value = nullptr;
  ASSERT_TRUE(restored->GetValue(kOriginWidgetsEnabledPref, &value));
  ASSERT_TRUE(value->is_list());
  EXPECT_EQ(value->GetList().size(), 2u);
  ASSERT_TRUE(restored->GetValue(kOriginWidgetPanelVisiblePref, &value));
  EXPECT_FALSE(value->GetBool());
  ASSERT_TRUE(restored->GetValue(kOriginCalendarGoogleTokenPref, &value));
  EXPECT_EQ(value->GetString(), "encrypted-test-token");
}

}  // namespace
