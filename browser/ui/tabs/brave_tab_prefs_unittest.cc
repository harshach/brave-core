// Copyright (c) 2025 The Brave Authors. All rights reserved.
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this file,
// You can obtain one at https://mozilla.org/MPL/2.0/.

#include "brave/browser/ui/tabs/brave_tab_prefs.h"

#include "base/test/scoped_feature_list.h"
#include "brave/components/brave_origin/buildflags/buildflags.h"
#include "chrome/browser/ui/tabs/features.h"
#include "components/prefs/testing_pref_service.h"
#include "testing/gtest/include/gtest/gtest.h"

TEST(BraveTabPrefsTest,
     IsScrollableHorizontalTabStripEnabled_FalseWhenFeatureDisabled) {
  base::test::ScopedFeatureList feature_list;
  feature_list.InitAndDisableFeature(tabs::kBraveScrollableTabStrip);

  TestingPrefServiceSimple prefs;
  brave_tabs::RegisterBraveProfilePrefs(prefs.registry());
  prefs.SetBoolean(brave_tabs::kScrollableHorizontalTabStrip, true);

  EXPECT_FALSE(brave_tabs::IsScrollableHorizontalTabStripEnabled(&prefs));
}

TEST(BraveTabPrefsTest,
     IsScrollableHorizontalTabStripEnabled_FalseWhenPrefDisabled) {
  base::test::ScopedFeatureList feature_list;
  feature_list.InitAndEnableFeature(tabs::kBraveScrollableTabStrip);

  TestingPrefServiceSimple prefs;
  brave_tabs::RegisterBraveProfilePrefs(prefs.registry());
  prefs.SetBoolean(brave_tabs::kScrollableHorizontalTabStrip, false);

  EXPECT_FALSE(brave_tabs::IsScrollableHorizontalTabStripEnabled(&prefs));
}

TEST(BraveTabPrefsTest,
     IsScrollableHorizontalTabStripEnabled_TrueWhenFeatureAndPrefOn) {
  base::test::ScopedFeatureList feature_list;
  feature_list.InitAndEnableFeature(tabs::kBraveScrollableTabStrip);

  TestingPrefServiceSimple prefs;
  brave_tabs::RegisterBraveProfilePrefs(prefs.registry());
  prefs.SetBoolean(brave_tabs::kScrollableHorizontalTabStrip, true);

  EXPECT_TRUE(brave_tabs::IsScrollableHorizontalTabStripEnabled(&prefs));
}

TEST(BraveTabPrefsTest, AlwaysUseMiniAccentIconDefaultsToFalse) {
  TestingPrefServiceSimple prefs;
  brave_tabs::RegisterBraveProfilePrefs(prefs.registry());

  EXPECT_FALSE(prefs.GetBoolean(brave_tabs::kAlwaysUseMiniAccentIcon));
}

TEST(BraveTabPrefsTest, OriginUsesNativeTreeTabWorkspaceDefaults) {
  TestingPrefServiceSimple prefs;
  brave_tabs::RegisterBraveProfilePrefs(prefs.registry());

  EXPECT_EQ(BUILDFLAG(IS_BRAVE_ORIGIN_BRANDED),
            prefs.GetBoolean(brave_tabs::kVerticalTabsEnabled));
  EXPECT_EQ(BUILDFLAG(IS_BRAVE_ORIGIN_BRANDED),
            prefs.GetBoolean(brave_tabs::kTreeTabsEnabled));
  EXPECT_EQ(!BUILDFLAG(IS_BRAVE_ORIGIN_BRANDED),
            prefs.GetBoolean(brave_tabs::kVerticalTabsFloatingEnabled));
  EXPECT_TRUE(prefs.GetBoolean(brave_tabs::kVerticalTabsShowToggleButton));
#if BUILDFLAG(IS_BRAVE_ORIGIN_BRANDED)
  EXPECT_EQ(280, prefs.GetInteger(brave_tabs::kVerticalTabsExpandedWidth));
#else
  EXPECT_EQ(220, prefs.GetInteger(brave_tabs::kVerticalTabsExpandedWidth));
#endif
}

TEST(BraveTabPrefsTest, OriginMigratesWorkspaceAndPreservesCollapsedState) {
  TestingPrefServiceSimple prefs;
  brave_tabs::RegisterBraveProfilePrefs(prefs.registry());
  prefs.SetBoolean(brave_tabs::kVerticalTabsEnabled, false);
  prefs.SetBoolean(brave_tabs::kTreeTabsEnabled, false);
  prefs.SetBoolean(brave_tabs::kVerticalTabsCollapsed, true);

  brave_tabs::MigrateBraveProfilePrefs(&prefs);

#if BUILDFLAG(IS_BRAVE_ORIGIN_BRANDED)
  EXPECT_TRUE(prefs.GetBoolean(brave_tabs::kVerticalTabsEnabled));
  EXPECT_TRUE(prefs.GetBoolean(brave_tabs::kTreeTabsEnabled));
  EXPECT_TRUE(prefs.GetBoolean(brave_tabs::kVerticalTabsCollapsed));
#else
  EXPECT_FALSE(prefs.GetBoolean(brave_tabs::kVerticalTabsEnabled));
  EXPECT_FALSE(prefs.GetBoolean(brave_tabs::kTreeTabsEnabled));
  EXPECT_TRUE(prefs.GetBoolean(brave_tabs::kVerticalTabsCollapsed));
#endif
}
