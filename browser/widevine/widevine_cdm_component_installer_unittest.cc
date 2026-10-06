/* Copyright (c) 2019 The Brave Authors. All rights reserved.
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "chrome/browser/component_updater/widevine_cdm_component_installer.h"

#include <optional>

#include "base/run_loop.h"
#include "base/test/scoped_path_override.h"
#include "base/version.h"
#include "brave/browser/widevine/widevine_utils.h"
#include "brave/components/widevine/constants.h"
#include "build/build_config.h"
#include "components/component_updater/component_installer.h"
#include "components/component_updater/component_updater_paths.h"
#include "components/component_updater/component_updater_service.h"
#include "components/component_updater/mock_component_updater_service.h"
#include "content/public/test/browser_task_environment.h"
#include "testing/gmock/include/gmock/gmock.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "third_party/widevine/cdm/buildflags.h"

#if BUILDFLAG(IS_MAC)
#include <string_view>

#include "base/base_paths.h"
#include "base/files/file_path.h"
#include "base/files/file_util.h"
#include "base/files/scoped_temp_dir.h"
#include "base/json/json_writer.h"
#include "base/native_library.h"
#include "base/strings/string_number_conversions.h"
#include "base/values.h"
#include "media/cdm/api/content_decryption_module.h"
#include "media/cdm/cdm_paths.h"
#include "media/cdm/supported_cdm_versions.h"
#include "third_party/widevine/cdm/widevine_cdm_common.h"
#endif

// We don't bundle. Only use widevine as a component.
TEST(WidevineBuildFlag, FlagTest) {
  EXPECT_TRUE(
#if BUILDFLAG(BUNDLE_WIDEVINE_CDM)
      false);
#else
      true);
#endif

  EXPECT_TRUE(
#if BUILDFLAG(ENABLE_WIDEVINE_CDM_COMPONENT)
      true);
#else
      false);
#endif
}

namespace component_updater {

class WidevineCdmComponentInstallerTest : public testing::Test {
 public:
  void SetUp() override {
    // RegisterWidevineCdmComponent() is a no-op until the user has opted in.
    SetWidevineEnabled(true);
  }

  void TearDown() override { SetWidevineEnabled(false); }

  // Registers the Widevine component against a mock service and returns what
  // the installer policy produced.
  std::optional<ComponentRegistration> RegisterAndCaptureComponent() {
    testing::NiceMock<MockComponentUpdateService> cus;
    // ComponentInstaller compares these against the installed version, and
    // base::Version::CompareTo() DCHECKs on invalid operands. CrxUpdateService
    // returns kNullVersion when it has nothing recorded; the mock's default
    // return is an invalid Version, so mirror the real service here.
    ON_CALL(cus, GetRegisteredVersion(testing::_))
        .WillByDefault(testing::Return(base::Version(kNullVersion)));
    ON_CALL(cus, GetMaxPreviousProductVersion(testing::_))
        .WillByDefault(testing::Return(base::Version(kNullVersion)));

    std::optional<ComponentRegistration> registration;
    ON_CALL(cus, RegisterComponent(testing::_))
        .WillByDefault([&registration](const ComponentRegistration& component) {
          registration = component;
          return true;
        });

    base::RunLoop run_loop;
    RegisterWidevineCdmComponent(&cus, run_loop.QuitClosure());
    run_loop.Run();
    return registration;
  }

 private:
  content::BrowserTaskEnvironment task_environment_;
  // ComponentInstaller reads and creates the component's install directory
  // while registering; keep that out of the real user data directory.
  base::ScopedPathOverride component_dir_override_{DIR_COMPONENT_USER};
#if BUILDFLAG(IS_MAC)
  // Registration may import another browser's CDM from here.
  base::ScopedPathOverride app_data_override_{base::DIR_APP_DATA};
#endif
};

// The Widevine CRX is fetched from Google's servers. Brave overrides upstream's
// RequiresNetworkEncryption() so that fetch can't fall back to plaintext HTTP,
// which would otherwise announce on the wire that this user is installing
// Widevine. Assert on the registration itself rather than on the policy class,
// so that a wrapper around the policy, or an upstream refactor of how the
// installer is registered, can't silently drop the override.
TEST_F(WidevineCdmComponentInstallerTest, RequiresNetworkEncryption) {
  const std::optional<ComponentRegistration> registration =
      RegisterAndCaptureComponent();

  ASSERT_TRUE(registration.has_value());
  ASSERT_EQ(kWidevineComponentId, registration->app_id);
  EXPECT_TRUE(registration->requires_network_encryption);
}

#if BUILDFLAG(IS_MAC)
class ImportWidevineCdmTest : public testing::Test {
 public:
  void SetUp() override {
    ASSERT_TRUE(app_data_dir_.CreateUniqueTempDir());
    ASSERT_TRUE(install_root_.CreateUniqueTempDir());
  }

  base::FilePath SourceCdmDir(std::string_view browser,
                              std::string_view version) const {
    return app_data_dir_.GetPath()
        .AppendASCII(browser)
        .AppendASCII(kWidevineCdmBaseDirectory)
        .AppendASCII(version);
  }

  // Writes a CDM the Widevine installer accepts under `browser`'s user data.
  void WriteCdm(std::string_view browser, std::string_view version) {
    WriteCdmAt(SourceCdmDir(browser, version), version);
  }

  void WriteInstalledCdm(std::string_view version) {
    WriteCdmAt(install_dir().AppendASCII(version), version);
  }

  void Import() {
    ImportWidevineCdmFromOtherBrowsers(app_data_dir_.GetPath(), install_dir());
  }

  bool IsInstalled(std::string_view version) {
    return base::PathExists(CdmLibraryPath(install_dir().AppendASCII(version)));
  }

  base::FilePath install_dir() const {
    return install_root_.GetPath().AppendASCII(kWidevineCdmBaseDirectory);
  }

 private:
  static base::FilePath CdmLibraryPath(const base::FilePath& cdm_dir) {
    return media::GetPlatformSpecificDirectory(cdm_dir).AppendASCII(
        base::GetNativeLibraryName(kWidevineCdmLibraryName));
  }

  static void WriteCdmAt(const base::FilePath& cdm_dir,
                         std::string_view version) {
    base::DictValue manifest;
    manifest.Set("version", version);
    manifest.Set("x-cdm-module-versions",
                 base::NumberToString(CDM_MODULE_VERSION));
    manifest.Set(
        "x-cdm-interface-versions",
        base::NumberToString(media::kSupportedCdmInterfaceVersions[0].version));
    manifest.Set("x-cdm-host-versions",
                 base::NumberToString(media::kMinSupportedCdmHostVersion));
    manifest.Set("x-cdm-codecs", "vp8,vp09,avc1,av01");
    const base::FilePath library = CdmLibraryPath(cdm_dir);
    ASSERT_TRUE(base::CreateDirectory(library.DirName()));
    ASSERT_TRUE(base::WriteFile(library, "cdm"));
    ASSERT_TRUE(base::WriteFile(cdm_dir.AppendASCII("manifest.json"),
                                *base::WriteJson(manifest)));
  }

  base::ScopedTempDir app_data_dir_;
  base::ScopedTempDir install_root_;
};

TEST_F(ImportWidevineCdmTest, ImportsBravesCdm) {
  WriteCdm("BraveSoftware/Brave-Browser", "4.10.3050.0");

  Import();

  EXPECT_TRUE(IsInstalled("4.10.3050.0"));
}

TEST_F(ImportWidevineCdmTest, ImportsNewestCdmAcrossBrowsers) {
  WriteCdm("BraveSoftware/Brave-Browser", "4.10.2934.0");
  WriteCdm("Google/Chrome", "4.10.3050.0");

  Import();

  EXPECT_TRUE(IsInstalled("4.10.3050.0"));
  EXPECT_FALSE(IsInstalled("4.10.2934.0"));
}

TEST_F(ImportWidevineCdmTest, KeepsInstalledCdmUnlessOlder) {
  WriteInstalledCdm("4.10.3050.0");
  WriteCdm("BraveSoftware/Brave-Browser", "4.10.2934.0");
  Import();
  EXPECT_FALSE(IsInstalled("4.10.2934.0"));

  WriteCdm("BraveSoftware/Brave-Browser", "4.10.3100.0");
  Import();
  EXPECT_TRUE(IsInstalled("4.10.3100.0"));
}

TEST_F(ImportWidevineCdmTest, SkipsCdmWithoutLibrary) {
  WriteCdm("BraveSoftware/Brave-Browser", "4.10.3050.0");
  ASSERT_TRUE(base::DeletePathRecursively(
      SourceCdmDir("BraveSoftware/Brave-Browser", "4.10.3050.0")
          .AppendASCII("_platform_specific")));

  Import();

  EXPECT_FALSE(base::PathExists(install_dir()));
}
#endif  // BUILDFLAG(IS_MAC)

}  // namespace component_updater
