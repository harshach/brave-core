/* Copyright (c) 2024 The Brave Authors. All rights reserved.
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at https://mozilla.org/MPL/2.0/. */

#include "brave/components/sidebar/common/features.h"

#include "brave/components/brave_origin/buildflags/buildflags.h"

namespace sidebar::features {

BASE_FEATURE(kSidebarShowAlwaysOnStable,
             base::FEATURE_DISABLED_BY_DEFAULT);

BASE_FEATURE(kSidebarWebPanel,
             BUILDFLAG(IS_BRAVE_ORIGIN_BRANDED)
                 ? base::FEATURE_ENABLED_BY_DEFAULT
                 : base::FEATURE_DISABLED_BY_DEFAULT);

const base::FeatureParam<bool> kOpenOneShotLeoPanel{
    &kSidebarShowAlwaysOnStable,
    /*name=*/"open_one_shot_leo_panel",
    /*default_value=*/false};

SidebarDefaultMode GetSidebarDefaultMode() {
   if (base::FeatureList::IsEnabled(kSidebarShowAlwaysOnStable)) {
     if (kOpenOneShotLeoPanel.Get()) {
       return SidebarDefaultMode::kOnOneShot;
     } else {
       return SidebarDefaultMode::kAlwaysOn;
     }
   } else {
     return SidebarDefaultMode::kOff;
   }
}

}  // namespace sidebar::features
