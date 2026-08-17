/* Copyright (c) 2026 The Brave Authors. All rights reserved.
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at https://mozilla.org/MPL/2.0/. */

#ifndef BRAVE_BROWSER_WORKSPACES_PREF_NAMES_H_
#define BRAVE_BROWSER_WORKSPACES_PREF_NAMES_H_

// Profile preference key — stores a dict keyed by hash of the display name.
inline constexpr char kWorkspacesMetadataPref[] = "brave.workspaces.metadata";

// Ordered list of live Sigma-style spaces. Each entry contains a stable id,
// display name, and emoji icon. This is intentionally separate from saved
// workspace snapshots above.
inline constexpr char kOriginSpacesPref[] = "brave.origin.spaces";

#endif  // BRAVE_BROWSER_WORKSPACES_PREF_NAMES_H_
