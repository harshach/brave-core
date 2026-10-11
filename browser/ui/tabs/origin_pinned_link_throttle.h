// Copyright (c) 2026 The Brave Authors. All rights reserved.
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this file,
// You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef BRAVE_BROWSER_UI_TABS_ORIGIN_PINNED_LINK_THROTTLE_H_
#define BRAVE_BROWSER_UI_TABS_ORIGIN_PINNED_LINK_THROTTLE_H_

#include "content/public/browser/navigation_throttle.h"

namespace content {
class NavigationThrottleRegistry;
}  // namespace content

// A link that leaves a pinned page's site opens in the temporary link window
// instead, as Arc's Peek does, so the pinned page stays where it was pinned.
class OriginPinnedLinkThrottle : public content::NavigationThrottle {
 public:
  explicit OriginPinnedLinkThrottle(
      content::NavigationThrottleRegistry& registry);
  OriginPinnedLinkThrottle(const OriginPinnedLinkThrottle&) = delete;
  OriginPinnedLinkThrottle& operator=(const OriginPinnedLinkThrottle&) = delete;
  ~OriginPinnedLinkThrottle() override;

  // Only pinned pages in normal Origin windows get the throttle.
  static void MaybeCreateAndAdd(content::NavigationThrottleRegistry& registry);

  // content::NavigationThrottle:
  ThrottleCheckResult WillStartRequest() override;
  const char* GetNameForLogging() override;
};

#endif  // BRAVE_BROWSER_UI_TABS_ORIGIN_PINNED_LINK_THROTTLE_H_
