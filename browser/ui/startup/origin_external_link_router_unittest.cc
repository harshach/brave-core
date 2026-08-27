// Copyright (c) 2026 The Brave Authors. All rights reserved.
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this file,
// You can obtain one at https://mozilla.org/MPL/2.0/.

#include "brave/browser/ui/startup/origin_external_link_router.h"

#include "testing/gtest/include/gtest/gtest.h"
#include "ui/gfx/geometry/rect.h"

namespace origin_external_link {

TEST(OriginExternalLinkRouterTest, TemporaryWindowFillsInsetBrowserBounds) {
  EXPECT_EQ(gfx::Rect(64, 74, 1352, 852),
            CalculateTemporaryLinkWindowBounds(
                gfx::Rect(40, 50, 1400, 900)));
}

TEST(OriginExternalLinkRouterTest, TemporaryWindowAdaptsToSmallBounds) {
  EXPECT_EQ(gfx::Rect(11, 12, 2, 12),
            CalculateTemporaryLinkWindowBounds(gfx::Rect(2, 3, 20, 30)));
  EXPECT_TRUE(CalculateTemporaryLinkWindowBounds(gfx::Rect()).IsEmpty());
}

}  // namespace origin_external_link
