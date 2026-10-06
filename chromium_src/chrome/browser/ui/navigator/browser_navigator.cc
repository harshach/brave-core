/* Copyright (c) 2019 The Brave Authors. All rights reserved.
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at https://mozilla.org/MPL/2.0/. */

#include <string_view>

#include "brave/browser/ui/brave_ui_features.h"
#include "brave/components/brave_origin/buildflags/buildflags.h"
#include "brave/components/containers/buildflags/buildflags.h"
#include "build/build_config.h"
#include "chrome/browser/tab_contents/tab_util.h"
#include "chrome/browser/ui/navigator/browser_navigator_params.h"
#include "content/public/common/url_constants.h"
#include "url/gurl.h"

#if BUILDFLAG(ENABLE_CONTAINERS)
#include "brave/components/containers/content/browser/storage_partition_utils.h"
#include "brave/components/containers/core/common/features.h"
#include "content/public/browser/security_principal.h"
#endif  // BUILDFLAG(ENABLE_CONTAINERS)

#if BUILDFLAG(IS_BRAVE_ORIGIN_BRANDED) && !BUILDFLAG(IS_ANDROID)
#include "brave/browser/ui/startup/origin_external_link_router.h"

// Pages opened by another application stack in a single ephemeral window that
// has no tab strip. Without this, upstream sends every link after the first one
// to a normal browser window, which is exactly what the ephemeral window is
// there to avoid.
#define BRAVE_WINDOW_CAN_OPEN_TABS                                    \
  if (origin_external_link::IsTemporaryLinkBrowser(params.browser)) { \
    return true;                                                      \
  }
#else
#define BRAVE_WINDOW_CAN_OPEN_TABS
#endif  // BUILDFLAG(IS_BRAVE_ORIGIN_BRANDED) && !BUILDFLAG(IS_ANDROID)

namespace {

void UpdateBraveScheme(NavigateParams* params) {
  if (params->url.SchemeIs(content::kBraveUIScheme)) {
    GURL::Replacements replacements;
    replacements.SetSchemeStr(content::kChromeUIScheme);
    params->url = params->url.ReplaceComponents(replacements);
  }
}

void MaybeOverridePopupDisposition(NavigateParams* params) {
  if (base::FeatureList::IsEnabled(features::kForcePopupToBeOpenedAsTab) &&
      params->disposition == WindowOpenDisposition::NEW_POPUP) {
    params->disposition = WindowOpenDisposition::NEW_FOREGROUND_TAB;
  }
}

void UpdateParams(NavigateParams* params) {
  UpdateBraveScheme(params);
  MaybeOverridePopupDisposition(params);
}

#if BUILDFLAG(ENABLE_CONTAINERS)
std::optional<content::StoragePartitionConfig>
GetStoragePartitionConfigToInherit(const NavigateParams& params) {
  if (!base::FeatureList::IsEnabled(containers::features::kContainers)) {
    return std::nullopt;
  }

  if (params.storage_partition_config) {
    return containers::MaybeInheritStoragePartition(
        *params.storage_partition_config);
  }

  if (params.source_site_instance) {
    return containers::MaybeInheritStoragePartition(
        params.source_site_instance->GetSecurityPrincipal()
            .GetStoragePartitionConfig());
  }

  return std::nullopt;
}
#endif  // BUILDFLAG(ENABLE_CONTAINERS)

}  // namespace

#include <chrome/browser/ui/navigator/browser_navigator.cc>
