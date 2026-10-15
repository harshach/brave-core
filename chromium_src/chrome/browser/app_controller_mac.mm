/* Copyright (c) 2023 The Brave Authors. All rights reserved.
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at https://mozilla.org/MPL/2.0/. */

#import "chrome/browser/app_controller_mac.h"

#import "brave/browser/brave_app_controller_mac.h"
#include "brave/components/brave_origin/buildflags/buildflags.h"

#if BUILDFLAG(IS_BRAVE_ORIGIN_BRANDED)
#define BRAVE_MARK_ORIGIN_EXTERNAL_STARTUP_TABS(tabs)              \
  do {                                                             \
    for (auto& tab : (tabs)) {                                     \
      tab.is_origin_external_link = tab.url.SchemeIsHTTPOrHTTPS(); \
    }                                                              \
  } while (false)
#else
#define BRAVE_MARK_ORIGIN_EXTERNAL_STARTUP_TABS(tabs) \
  do {                                                \
  } while (false)
#endif

#include <chrome/browser/app_controller_mac.mm>

#undef BRAVE_MARK_ORIGIN_EXTERNAL_STARTUP_TABS
