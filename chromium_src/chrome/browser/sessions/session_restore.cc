/* Copyright (c) 2026 The Brave Authors. All rights reserved.
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at https://mozilla.org/MPL/2.0/. */

#include "base/strings/string_number_conversions.h"
#include "brave/browser/sessions/brave_session_keys.h"
#include "brave/browser/ui/tabs/origin_space_controller.h"
#include "brave/components/containers/buildflags/buildflags.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/browser_tabstrip.h"
#include "chrome/browser/ui/browser_window/public/browser_window_features.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface.h"
#include "chrome/browser/ui/navigator/browser_navigator.h"
#include "chrome/browser/ui/navigator/browser_navigator_params.h"
#include "chrome/browser/ui/startup/startup_tab.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "components/sessions/core/session_types.h"
#include "content/public/browser/web_contents.h"
#include "url/gurl.h"

#if BUILDFLAG(ENABLE_CONTAINERS)
#include "brave/browser/containers/container_specifier_utils.h"
#endif  // BUILDFLAG(ENABLE_CONTAINERS)

// Returns true if the ephemeral storage for |url|'s domain is scheduled for
// cleanup, in which case the tab should not be restored.
// Implemented in
// browser/ephemeral_storage/ephemeral_storage_session_restore_helpers.cc so
// that the upstream sessions target does not depend on Brave ephemeral storage.
bool BraveIsScheduledForCleanup(const GURL& url, Profile* profile);

namespace {

void BraveNavigateStartupTab(const StartupTab& tab,
                             BrowserWindowInterface* browser,
                             NavigateParams& params) {
#if BUILDFLAG(ENABLE_CONTAINERS)
  if (!params.storage_partition_config) {
    params.storage_partition_config =
        containers::GetStoragePartitionConfigForContainerSpecifier(
            browser->GetProfile(), tab.container);
  }
#endif
  Navigate(&params);
}

// Returns the index of the first tab to be made an active if the active tab is
// deleted, or simply the index of the existing active tab. The index of the
// newly added fallback tab if the tab strip was empty, or kNoTab as a fail
// state.
int MaybeAddFallbackTabIfEmpty(BrowserWindowInterface* browser) {
  if (!browser->GetTabStripModel()->empty()) {
    auto index = browser->GetTabStripModel()->active_index();
    return index == TabStripModel::kNoTab ? 0 : index;
  }
  if (const auto* contents =
          chrome::AddAndReturnTabAt(browser, GURL(), -1, true)) {
    return browser->GetTabStripModel()->GetIndexOfWebContents(contents);
  }

  return TabStripModel::kNoTab;
}

void BraveBeginOriginSpaceWindowRestore(BrowserWindowInterface* browser,
                                        sessions::SessionWindow& window) {
  auto* controller = browser->GetFeatures().origin_space_controller();
  if (!controller) {
    return;
  }
  // The window and its tabs are about to get new SessionIDs, and the Space
  // backups are recorded under the ones in this file. Carry those along with
  // the rest of each entry's data so the restore can still find them when the
  // file itself lost its Space data.
  window.extra_data[kBraveOriginRestoredWindowIdKey] =
      base::NumberToString(window.window_id.id());
  for (auto& tab : window.tabs) {
    tab->extra_data[kBraveOriginRestoredTabIdKey] =
        base::NumberToString(tab->tab_id.id());
  }
  controller->BeginWindowRestore(window.extra_data);
}

void BraveFinishOriginSpaceWindowRestore(BrowserWindowInterface* browser) {
  if (auto* controller =
          browser->GetFeatures().origin_space_controller()) {
    controller->FinishWindowRestore();
  }
}

}  // namespace

#include <chrome/browser/sessions/session_restore.cc>
