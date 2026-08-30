/* Copyright (c) 2020 The Brave Authors. All rights reserved.
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

#include <string>

#include "base/files/file_path.h"
#include "brave/components/brave_origin/buildflags/buildflags.h"
#include "build/build_config.h"
#include "chrome/common/channel_info.h"
#include "chrome/common/chrome_paths_internal.h"
#include "components/version_info/channel.h"
#include "testing/gtest/include/gtest/gtest.h"

#if BUILDFLAG(IS_LINUX)
#include "base/environment.h"
#endif

#if BUILDFLAG(IS_MAC)
// GetChannelByName is only supported on MacOS.
TEST(BraveChannelInfoTest, ChannelByNameTest) {
#if defined(OFFICIAL_BUILD)
  EXPECT_EQ(version_info::Channel::STABLE,
            chrome::GetChannelByName(""));
  EXPECT_EQ(version_info::Channel::BETA,
            chrome::GetChannelByName("beta"));
  EXPECT_EQ(version_info::Channel::DEV,
            chrome::GetChannelByName("dev"));
  EXPECT_EQ(version_info::Channel::CANARY,
            chrome::GetChannelByName("nightly"));
#else
  EXPECT_EQ(version_info::Channel::UNKNOWN,
            chrome::GetChannelByName(""));
#endif
}
#endif  // OS_MAC

#if BUILDFLAG(IS_LINUX)
TEST(BraveChannelInfoTest, ParentDirectoryOfUserDataDirectoryTest) {
  base::FilePath path;
  EXPECT_TRUE(chrome::GetDefaultUserDataDirectory(&path));
#if BUILDFLAG(IS_SOCKET_BRANDED)
  EXPECT_EQ("Socket", path.DirName().BaseName().AsUTF8Unsafe());
#else
  EXPECT_EQ("BraveSoftware", path.DirName().BaseName().AsUTF8Unsafe());
#endif
}

TEST(BraveChannelInfoTest, DefaultUserDataDirectoryAndChannelTest) {
  base::FilePath path;

#if BUILDFLAG(IS_SOCKET_BRANDED)
  constexpr char kProduct[] = "Socket";
#elif BUILDFLAG(IS_BRAVE_ORIGIN_BRANDED)
  constexpr char kProduct[] = "Brave-Origin";
#else
  constexpr char kProduct[] = "Brave-Browser";
#endif
  const auto expected_product = [&](const char* channel_suffix) {
    return std::string(kProduct) + channel_suffix;
  };

#if defined(OFFICIAL_BUILD)
  auto env = base::Environment::Create();

  env->SetVar("CHROME_VERSION_EXTRA", LINUX_CHANNEL_STABLE);
  EXPECT_TRUE(chrome::GetDefaultUserDataDirectory(&path));
  EXPECT_EQ(expected_product(""), path.BaseName().AsUTF8Unsafe());
  EXPECT_EQ(version_info::Channel::STABLE, chrome::GetChannel());
  EXPECT_EQ(BRAVE_LINUX_CHANNEL_STABLE,
            chrome::GetChannelName(chrome::WithExtendedStable(false)));

  env->SetVar("CHROME_VERSION_EXTRA", LINUX_CHANNEL_BETA);
  EXPECT_TRUE(chrome::GetDefaultUserDataDirectory(&path));
  EXPECT_EQ(expected_product("-Beta"), path.BaseName().AsUTF8Unsafe());
  EXPECT_EQ(version_info::Channel::BETA, chrome::GetChannel());
  EXPECT_EQ(LINUX_CHANNEL_BETA,
            chrome::GetChannelName(chrome::WithExtendedStable(false)));

  env->SetVar("CHROME_VERSION_EXTRA", BRAVE_LINUX_CHANNEL_DEV);
  EXPECT_TRUE(chrome::GetDefaultUserDataDirectory(&path));
  EXPECT_EQ(expected_product("-Dev"), path.BaseName().AsUTF8Unsafe());
  EXPECT_EQ(version_info::Channel::DEV, chrome::GetChannel());
  EXPECT_EQ(BRAVE_LINUX_CHANNEL_DEV,
            chrome::GetChannelName(chrome::WithExtendedStable(false)));

  env->SetVar("CHROME_VERSION_EXTRA", LINUX_CHANNEL_DEV);
  EXPECT_TRUE(chrome::GetDefaultUserDataDirectory(&path));
  EXPECT_EQ(expected_product("-Dev"), path.BaseName().AsUTF8Unsafe());
  EXPECT_EQ(version_info::Channel::DEV, chrome::GetChannel());
  EXPECT_EQ(BRAVE_LINUX_CHANNEL_DEV,
            chrome::GetChannelName(chrome::WithExtendedStable(false)));

  env->SetVar("CHROME_VERSION_EXTRA", BRAVE_LINUX_CHANNEL_NIGHTLY);
  EXPECT_TRUE(chrome::GetDefaultUserDataDirectory(&path));
  EXPECT_EQ(expected_product("-Nightly"), path.BaseName().AsUTF8Unsafe());
  EXPECT_EQ(version_info::Channel::CANARY, chrome::GetChannel());
  EXPECT_EQ(BRAVE_LINUX_CHANNEL_NIGHTLY,
            chrome::GetChannelName(chrome::WithExtendedStable(false)));
#else   // OFFICIAL_BUILD
  EXPECT_TRUE(chrome::GetDefaultUserDataDirectory(&path));
  EXPECT_EQ(expected_product("-Development"),
            path.BaseName().AsUTF8Unsafe());
  EXPECT_EQ(version_info::Channel::UNKNOWN, chrome::GetChannel());
  EXPECT_EQ(BRAVE_LINUX_CHANNEL_STABLE,
            chrome::GetChannelName(chrome::WithExtendedStable(false)));
#endif  // !OFFICIAL_BUILD
}
#endif
