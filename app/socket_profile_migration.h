// Copyright (c) 2026 The Brave Authors. All rights reserved.
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this file,
// You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef BRAVE_APP_SOCKET_PROFILE_MIGRATION_H_
#define BRAVE_APP_SOCKET_PROFILE_MIGRATION_H_

#include "base/files/file_path.h"

namespace socket_profile {

enum class StartupDisposition {
  kContinue,
  kExit,
};

// Offers to copy an existing Brave profile into Socket on first launch.
StartupDisposition MaybeMigrateBraveProfile();

namespace internal {

bool HasProfileData(const base::FilePath& profile_dir);
bool CopyProfileData(const base::FilePath& source,
                     const base::FilePath& destination);

}  // namespace internal
}  // namespace socket_profile

#endif  // BRAVE_APP_SOCKET_PROFILE_MIGRATION_H_
