/* Copyright (c) 2026 The Brave Authors. All rights reserved.
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at https://mozilla.org/MPL/2.0/. */

#ifndef BRAVE_BROWSER_WORKSPACES_WORKSPACE_METADATA_H_
#define BRAVE_BROWSER_WORKSPACES_WORKSPACE_METADATA_H_

#include <string>

#include "base/time/time.h"

// Lightweight summary returned by ListWorkspaces(), used to populate UI.
// The full session state is stored as Chromium session commands on disk.
struct WorkspaceMetadata {
  std::string name;
  base::Time modified_at;
  int number_of_windows = 0;
  int number_of_tabs = 0;
};

struct OriginSpaceMetadata {
  std::string id;
  std::string name;
  std::string icon;
  // One of the kOriginSpaceTheme* ids below.
  std::string theme;

  bool operator==(const OriginSpaceMetadata&) const = default;
};

// Stable vector-icon identifiers shared by the Origin rail, page-list header,
// and Quick Open destination chips.
inline constexpr char kOriginSpaceIconHome[] = "home";
inline constexpr char kOriginSpaceIconWork[] = "work";
inline constexpr char kOriginSpaceIconPlayground[] = "playground";
inline constexpr char kOriginSpaceIconReading[] = "reading";
inline constexpr char kOriginSpaceIconTerminal[] = "terminal";
inline constexpr char kOriginSpaceIconIdeas[] = "ideas";
inline constexpr char kOriginSpaceIconMessages[] = "messages";
inline constexpr char kOriginSpaceIconSchool[] = "school";
inline constexpr char kOriginSpaceIconShopping[] = "shopping";
inline constexpr char kOriginSpaceIconTravel[] = "travel";

// Window colour themes a Space can carry.
inline constexpr char kOriginSpaceThemeEmber[] = "ember";
inline constexpr char kOriginSpaceThemeAmber[] = "amber";
inline constexpr char kOriginSpaceThemeForest[] = "forest";
inline constexpr char kOriginSpaceThemeTeal[] = "teal";
inline constexpr char kOriginSpaceThemeOcean[] = "ocean";
inline constexpr char kOriginSpaceThemeViolet[] = "violet";
inline constexpr char kOriginSpaceThemeRose[] = "rose";
inline constexpr char kOriginSpaceThemeGraphite[] = "graphite";

// How strongly Space themes tint the window. Shared by every Space.
enum class OriginSpaceThemeIntensity {
  kSubtle = 1,
  kRich = 2,
  kVivid = 3,
};

#endif  // BRAVE_BROWSER_WORKSPACES_WORKSPACE_METADATA_H_
