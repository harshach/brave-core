/* Copyright (c) 2026 The Brave Authors. All rights reserved.
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at https://mozilla.org/MPL/2.0/. */

#ifndef BRAVE_BROWSER_WORKSPACES_PREF_NAMES_H_
#define BRAVE_BROWSER_WORKSPACES_PREF_NAMES_H_

// Profile preference key — stores a dict keyed by hash of the display name.
inline constexpr char kWorkspacesMetadataPref[] = "brave.workspaces.metadata";

// Ordered list of live Sigma-style spaces. Each entry contains a stable id,
// display name, and stable vector-icon identifier. This is intentionally
// separate from saved
// workspace snapshots above.
inline constexpr char kOriginSpacesPref[] = "brave.origin.spaces";

// Dictionary mapping registrable domains to Origin Space IDs. These rules are
// applied only to links handed to the browser by the operating system.
inline constexpr char kOriginDomainSpaceRulesPref[] =
    "brave.origin.domain_space_rules";

// Last Space chosen when keeping an external link. This becomes the suggested
// destination for the next temporary link unless a domain rule applies.
inline constexpr char kOriginLastTemporaryLinkSpacePref[] =
    "brave.origin.last_temporary_link_space";

// Durable fallbacks keyed by Chromium session IDs. Session command files can
// be rebuilt without extension metadata, so these dictionaries preserve the
// latest tab membership and selected Space until the next restore.
inline constexpr char kOriginTabSessionSpacesPref[] =
    "brave.origin.tab_session_spaces";
inline constexpr char kOriginWindowSessionSpacesPref[] =
    "brave.origin.window_session_spaces";

#endif  // BRAVE_BROWSER_WORKSPACES_PREF_NAMES_H_
