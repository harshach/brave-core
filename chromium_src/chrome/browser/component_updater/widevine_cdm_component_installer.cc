/* Copyright (c) 2019 The Brave Authors. All rights reserved.
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "chrome/browser/component_updater/widevine_cdm_component_installer.h"

#include <optional>

#include "brave/browser/widevine/widevine_utils.h"
#include "brave/components/brave_origin/buildflags/buildflags.h"
#include "build/build_config.h"

#if BUILDFLAG(IS_MAC)
#include "base/files/file_enumerator.h"
#include "base/files/scoped_temp_dir.h"
#include "base/path_service.h"
#include "components/component_updater/component_updater_paths.h"
#include "components/update_client/utils.h"
#endif

#if BUILDFLAG(IS_MAC) && BUILDFLAG(IS_SOCKET_BRANDED)
#define RegisterWidevineCdmComponent RegisterWidevineCdmComponent_ChromiumImpl
#endif

#include <chrome/browser/component_updater/widevine_cdm_component_installer.cc>

#if BUILDFLAG(IS_MAC) && BUILDFLAG(IS_SOCKET_BRANDED)
#undef RegisterWidevineCdmComponent
#endif

#if BUILDFLAG(IS_MAC)
namespace component_updater {

namespace {

// User data dirs, relative to Application Support, of browsers whose Widevine
// CDM can be reused.
constexpr const char* kWidevineImportSources[] = {
    "BraveSoftware/Brave-Browser",
    "BraveSoftware/Brave-Browser-Beta",
    "BraveSoftware/Brave-Browser-Nightly",
    "Google/Chrome",
};

struct WidevineCdmDir {
  base::Version version;
  base::FilePath path;
};

// Returns the newest version dir under `base_dir` that the Widevine installer
// would accept.
std::optional<WidevineCdmDir> FindNewestUsableWidevineCdm(
    const base::FilePath& base_dir) {
  // VerifyInstallation() is private in the policy but public in its base.
  const std::unique_ptr<ComponentInstallerPolicy> policy =
      std::make_unique<WidevineCdmComponentInstallerPolicy>();
  std::optional<WidevineCdmDir> newest;
  base::FileEnumerator versions(base_dir, /*recursive=*/false,
                                base::FileEnumerator::DIRECTORIES);
  for (base::FilePath path = versions.Next(); !path.empty();
       path = versions.Next()) {
    base::Version version(path.BaseName().MaybeAsASCII());
    if (!version.IsValid() || (newest && version <= newest->version)) {
      continue;
    }
    std::optional<base::DictValue> manifest = update_client::ReadManifest(path);
    if (manifest && policy->VerifyInstallation(*manifest, path)) {
      newest = WidevineCdmDir{std::move(version), path};
    }
  }
  return newest;
}

}  // namespace

void ImportWidevineCdmFromOtherBrowsers(const base::FilePath& app_data_dir,
                                        const base::FilePath& install_dir) {
  std::optional<WidevineCdmDir> source;
  for (const char* browser : kWidevineImportSources) {
    std::optional<WidevineCdmDir> candidate = FindNewestUsableWidevineCdm(
        app_data_dir.AppendASCII(browser).AppendASCII(
            kWidevineCdmBaseDirectory));
    if (candidate && (!source || candidate->version > source->version)) {
      source = std::move(candidate);
    }
  }
  if (!source) {
    return;
  }
  const std::optional<WidevineCdmDir> installed =
      FindNewestUsableWidevineCdm(install_dir);
  if (installed && installed->version >= source->version) {
    return;
  }

  // Copy under a non-version name first so the installer never picks up a
  // partial CDM.
  base::ScopedTempDir staging;
  if (!base::CreateDirectory(install_dir) ||
      !staging.CreateUniqueTempDirUnderPath(install_dir)) {
    return;
  }
  const std::string version = source->version.GetString();
  const base::FilePath staged = staging.GetPath().AppendASCII(version);
  const base::FilePath target = install_dir.AppendASCII(version);
  if (!base::CopyDirectory(source->path, staged, /*recursive=*/true) ||
      !base::DeletePathRecursively(target) || !base::Move(staged, target)) {
    LOG(WARNING) << "Couldn't import the Widevine CDM from " << source->path;
  }
}

#if BUILDFLAG(IS_SOCKET_BRANDED)
void RegisterWidevineCdmComponent(ComponentUpdateService* cus,
                                  base::OnceClosure callback) {
  DCHECK_CURRENTLY_ON(content::BrowserThread::UI);
  if (!IsWidevineEnabled()) {
    return;
  }
  base::FilePath app_data_dir;
  base::FilePath component_dir;
  if (!base::PathService::Get(base::DIR_APP_DATA, &app_data_dir) ||
      !base::PathService::Get(DIR_COMPONENT_USER, &component_dir)) {
    RegisterWidevineCdmComponent_ChromiumImpl(cus, std::move(callback));
    return;
  }
  // Socket can't download Widevine from Brave's update server, so reuse the
  // copy another browser on this Mac already downloaded.
  base::ThreadPool::PostTaskAndReply(
      FROM_HERE, {base::MayBlock(), base::TaskPriority::USER_VISIBLE},
      base::BindOnce(&ImportWidevineCdmFromOtherBrowsers, app_data_dir,
                     component_dir.AppendASCII(kWidevineCdmBaseDirectory)),
      base::BindOnce(&RegisterWidevineCdmComponent_ChromiumImpl,
                     base::Unretained(cus), std::move(callback)));
}
#endif  // BUILDFLAG(IS_SOCKET_BRANDED)

}  // namespace component_updater
#endif  // BUILDFLAG(IS_MAC)
