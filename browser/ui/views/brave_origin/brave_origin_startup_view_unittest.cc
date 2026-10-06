/* Copyright (c) 2026 The Brave Authors. All rights reserved.
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at https://mozilla.org/MPL/2.0/. */

#include "brave/browser/ui/views/brave_origin/brave_origin_startup_view.h"

#include <optional>

#include "components/prefs/testing_pref_service.h"
#include "testing/gtest/include/gtest/gtest.h"

class BraveOriginStartupViewTest : public testing::Test {
 public:
  void SetUp() override {
    BraveOriginStartupView::SetShouldShowDialogForTesting(std::nullopt);
  }

  void TearDown() override {
    BraveOriginStartupView::SetShouldShowDialogForTesting(std::nullopt);
  }

 protected:
  TestingPrefServiceSimple local_state_;
};

// --- ShouldShowDialog tests ---

TEST_F(BraveOriginStartupViewTest, PurchaseGateIsDisabled) {
  EXPECT_FALSE(BraveOriginStartupView::ShouldShowDialog(&local_state_));
}

// --- SetShouldShowDialogForTesting tests ---

TEST_F(BraveOriginStartupViewTest, TestOverrideForceShow) {
  BraveOriginStartupView::SetShouldShowDialogForTesting(true);
  EXPECT_TRUE(BraveOriginStartupView::ShouldShowDialog(&local_state_));
}

TEST_F(BraveOriginStartupViewTest, TestOverrideForceHide) {
  BraveOriginStartupView::SetShouldShowDialogForTesting(false);
  EXPECT_FALSE(BraveOriginStartupView::ShouldShowDialog(&local_state_));
}

TEST_F(BraveOriginStartupViewTest, TestOverrideResetsToNormal) {
  BraveOriginStartupView::SetShouldShowDialogForTesting(false);
  EXPECT_FALSE(BraveOriginStartupView::ShouldShowDialog(&local_state_));

  BraveOriginStartupView::SetShouldShowDialogForTesting(std::nullopt);
  EXPECT_FALSE(BraveOriginStartupView::ShouldShowDialog(&local_state_));
}

// --- IsShowing tests (without needing a widget) ---

TEST_F(BraveOriginStartupViewTest, IsShowingReturnsFalseByDefault) {
  EXPECT_FALSE(BraveOriginStartupView::IsShowing());
}
