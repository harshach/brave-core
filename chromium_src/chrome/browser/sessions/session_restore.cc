/* Copyright (c) 2026 The Brave Authors. All rights reserved.
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at https://mozilla.org/MPL/2.0/. */

#include "brave/components/containers/buildflags/buildflags.h"
#include "brave/browser/ui/tabs/origin_space_controller.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/browser_window/public/browser_window_features.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface.h"
#include "chrome/browser/ui/navigator/browser_navigator_params.h"
#include "chrome/browser/ui/startup/startup_tab.h"

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
}

void BraveBeginOriginSpaceWindowRestore(
    BrowserWindowInterface* browser,
    const std::map<std::string, std::string>& extra_data) {
  if (auto* controller =
          browser->GetFeatures().origin_space_controller()) {
    controller->BeginWindowRestore(extra_data);
  }
}

void BraveFinishOriginSpaceWindowRestore(BrowserWindowInterface* browser) {
  if (auto* controller =
          browser->GetFeatures().origin_space_controller()) {
    controller->FinishWindowRestore();
  }
}

}  // namespace

#include <chrome/browser/sessions/session_restore.cc>
