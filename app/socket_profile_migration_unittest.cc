// Copyright (c) 2026 The Brave Authors. All rights reserved.
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this file,
// You can obtain one at https://mozilla.org/MPL/2.0/.

#include "brave/app/socket_profile_migration.h"

#include <string>

#include "base/files/file_util.h"
#include "base/files/scoped_temp_dir.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace socket_profile::internal {

namespace {

class SocketProfileMigrationTest : public testing::Test {
 protected:
  void SetUp() override { ASSERT_TRUE(temp_dir_.CreateUniqueTempDir()); }

  base::FilePath CreateBraveProfile() {
    base::FilePath source = temp_dir_.GetPath().AppendASCII("Brave-Browser");
    EXPECT_TRUE(base::CreateDirectory(source.AppendASCII("Default")));
    EXPECT_TRUE(base::WriteFile(source.AppendASCII("Local State"), "state"));
    EXPECT_TRUE(base::WriteFile(
        source.AppendASCII("Default").AppendASCII("Preferences"), "prefs"));
    EXPECT_TRUE(base::WriteFile(
        source.AppendASCII("Default").AppendASCII("Cookies"), "cookies"));
    return source;
  }

  base::ScopedTempDir temp_dir_;
};

TEST_F(SocketProfileMigrationTest, CopiesCompleteProfile) {
  const base::FilePath source = CreateBraveProfile();
  const base::FilePath destination = temp_dir_.GetPath().AppendASCII("Socket");
  ASSERT_TRUE(base::CreateDirectory(destination));

  EXPECT_TRUE(CopyProfileData(source, destination));
  EXPECT_TRUE(HasProfileData(destination));

  std::string contents;
  ASSERT_TRUE(base::ReadFileToString(
      destination.AppendASCII("Default").AppendASCII("Cookies"), &contents));
  EXPECT_EQ("cookies", contents);
}

TEST_F(SocketProfileMigrationTest, RemovesSingletonArtifacts) {
  const base::FilePath source = CreateBraveProfile();
  ASSERT_TRUE(
      base::WriteFile(source.AppendASCII("SingletonCookie"), "transient"));
  const base::FilePath destination = temp_dir_.GetPath().AppendASCII("Socket");

  ASSERT_TRUE(CopyProfileData(source, destination));
  EXPECT_FALSE(base::PathExists(destination.AppendASCII("SingletonCookie")));
}

TEST_F(SocketProfileMigrationTest, ImportsOverStartupScaffolding) {
  // Chromium creates directories such as BrowserMetrics in the user data
  // directory before the import runs, so the destination is never empty.
  const base::FilePath source = temp_dir_.GetPath().AppendASCII("brave");
  const base::FilePath destination = temp_dir_.GetPath().AppendASCII("socket");
  ASSERT_TRUE(base::CreateDirectory(source.AppendASCII("Default")));
  ASSERT_TRUE(base::WriteFile(source.AppendASCII("Local State"), "{}"));
  ASSERT_TRUE(base::WriteFile(
      source.AppendASCII("Default").AppendASCII("Preferences"), "{}"));
  ASSERT_TRUE(base::CreateDirectory(destination.AppendASCII("BrowserMetrics")));

  EXPECT_TRUE(internal::CopyProfileData(source, destination));
  EXPECT_TRUE(base::PathExists(destination.AppendASCII("Local State")));
}

TEST_F(SocketProfileMigrationTest, PreservesExistingSocketProfile) {
  const base::FilePath source = CreateBraveProfile();
  const base::FilePath destination = temp_dir_.GetPath().AppendASCII("Socket");
  ASSERT_TRUE(base::CreateDirectory(destination));
  ASSERT_TRUE(
      base::WriteFile(destination.AppendASCII("Local State"), "socket-state"));

  EXPECT_FALSE(CopyProfileData(source, destination));

  std::string contents;
  ASSERT_TRUE(base::ReadFileToString(destination.AppendASCII("Local State"),
                                     &contents));
  EXPECT_EQ("socket-state", contents);
}

TEST_F(SocketProfileMigrationTest, RejectsIncompleteSource) {
  const base::FilePath source = temp_dir_.GetPath().AppendASCII("NotBrave");
  ASSERT_TRUE(base::CreateDirectory(source));
  const base::FilePath destination = temp_dir_.GetPath().AppendASCII("Socket");

  EXPECT_FALSE(CopyProfileData(source, destination));
  EXPECT_FALSE(base::PathExists(destination));
}

}  // namespace
}  // namespace socket_profile::internal
