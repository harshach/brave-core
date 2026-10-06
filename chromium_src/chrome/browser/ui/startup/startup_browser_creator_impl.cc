/* Copyright (c) 2019 The Brave Authors. All rights reserved.
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "chrome/browser/ui/startup/startup_browser_creator_impl.h"

#include <algorithm>

#include "brave/browser/ui/startup/brave_startup_tab_provider_impl.h"
#include "brave/browser/ui/startup/origin_external_link_router.h"
#include "brave/components/brave_origin/buildflags/buildflags.h"
#include "brave/components/containers/buildflags/buildflags.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface.h"
#include "chrome/browser/ui/navigator/browser_navigator_params.h"
#include "chrome/browser/ui/startup/startup_browser_creator.h"
#include "chrome/browser/ui/startup/startup_tab_provider.h"
#include "ui/base/base_window.h"

#if BUILDFLAG(ENABLE_CONTAINERS)
#include "brave/browser/containers/container_specifier_utils.h"
#endif  // BUILDFLAG(ENABLE_CONTAINERS)

namespace {

void BraveModifyStartupTabNavigationParams(const StartupTab& tab,
                                           BrowserWindowInterface* browser,
                                           NavigateParams& params) {
#if BUILDFLAG(ENABLE_CONTAINERS)
  if (!params.storage_partition_config) {
    params.storage_partition_config =
        containers::GetStoragePartitionConfigForContainerSpecifier(
            browser->GetProfile(), tab.container);
  }
#endif
#if BUILDFLAG(IS_BRAVE_ORIGIN_BRANDED)
  if (tab.is_origin_external_link) {
    origin_external_link::ConfigureNavigation(tab.url, browser, &params);
  }
#endif
}

void BraveShowStartupBrowser(BrowserWindowInterface* browser,
                             const StartupTabs& tabs) {
#if BUILDFLAG(IS_BRAVE_ORIGIN_BRANDED)
  // Origin shows the window it routed OS links to. Showing this one as well
  // would pull the user over to whichever macOS Space it sits on.
  if (std::ranges::all_of(tabs, [browser](const StartupTab& tab) {
        return tab.is_origin_external_link &&
               origin_external_link::RoutesNavigation(tab.url, browser);
      })) {
    return;
  }
#endif
  browser->GetWindow()->Show();
}

}  // namespace

#define StartupTabProviderImpl BraveStartupTabProviderImpl

#include <chrome/browser/ui/startup/startup_browser_creator_impl.cc>

#undef StartupTabProviderImpl
