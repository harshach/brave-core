// Copyright (c) 2026 The Brave Authors. All rights reserved.
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this file,
// You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef BRAVE_BROWSER_SESSIONS_BRAVE_SESSION_KEYS_H_
#define BRAVE_BROWSER_SESSIONS_BRAVE_SESSION_KEYS_H_

// Keys used in SessionTab::extra_data to persist tree-tab node structure.
// All values are stored as plain strings.

// The TreeTabNodeId of the tree node that owns this tab, serialized as a
// base::Token 128-bit hex string.
inline constexpr char kBraveTreeNodeIdKey[] = "brave_tree_node_id";

// The TreeTabNodeId of the parent tree node, serialized the same way.
// An empty string means this node is a root (top-level) node.
inline constexpr char kBraveTreeParentNodeIdKey[] = "brave_tree_parent_node_id";

// "1" if the tree node owning this tab is collapsed, "0" otherwise.
// Only written for tabs that are the direct current_value (kTab type) of
// their TreeTabNodeTabCollection.
inline constexpr char kBraveTreeNodeCollapsedKey[] =
    "brave_tree_node_collapsed";

// The profile-scoped Origin space ID that owns this tab. This is intentionally
// tab extra data (rather than a window pref) so closed tabs and full session
// restore return to the same space.
inline constexpr char kBraveOriginSpaceIdKey[] = "brave_origin_space_id";

// The selected Origin space for a browser window. This is separate from tab
// membership so an empty space can survive crash and full-session restore.
inline constexpr char kBraveOriginActiveSpaceIdKey[] =
    "brave_origin_active_space_id";

// Set in memory while a session is being restored and never written back: the
// SessionIDs a tab and its window had in the session file. Restored tabs and
// windows get new IDs, so these are the only link to the Space backups that
// were recorded under the old ones.
inline constexpr char kBraveOriginRestoredTabIdKey[] =
    "brave_origin_restored_tab_id";
inline constexpr char kBraveOriginRestoredWindowIdKey[] =
    "brave_origin_restored_window_id";

#endif  // BRAVE_BROWSER_SESSIONS_BRAVE_SESSION_KEYS_H_
