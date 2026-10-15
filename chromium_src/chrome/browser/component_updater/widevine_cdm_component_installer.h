/* Copyright (c) 2023 The Brave Authors. All rights reserved.
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at https://mozilla.org/MPL/2.0/. */

#ifndef BRAVE_CHROMIUM_SRC_CHROME_BROWSER_COMPONENT_UPDATER_WIDEVINE_CDM_COMPONENT_INSTALLER_H_
#define BRAVE_CHROMIUM_SRC_CHROME_BROWSER_COMPONENT_UPDATER_WIDEVINE_CDM_COMPONENT_INSTALLER_H_

#include "services/network/public/cpp/shared_url_loader_factory.h"

#include <chrome/browser/component_updater/widevine_cdm_component_installer.h>  // IWYU pragma: export

#include "build/build_config.h"

#if BUILDFLAG(IS_MAC)
namespace base {
class FilePath;
}  // namespace base

namespace component_updater {

// Copies the newest usable Widevine CDM that Brave or Chrome keeps under
// `app_data_dir` into `install_dir` when it's newer than what's installed.
void ImportWidevineCdmFromOtherBrowsers(const base::FilePath& app_data_dir,
                                        const base::FilePath& install_dir);

}  // namespace component_updater
#endif  // BUILDFLAG(IS_MAC)

#endif  // BRAVE_CHROMIUM_SRC_CHROME_BROWSER_COMPONENT_UPDATER_WIDEVINE_CDM_COMPONENT_INSTALLER_H_
