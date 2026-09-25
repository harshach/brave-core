/* Copyright (c) 2026 The Brave Authors. All rights reserved.
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at https://mozilla.org/MPL/2.0/. */

#include <map>
#include <string>

#include "brave/browser/sessions/brave_session_keys.h"
#include "brave/browser/ui/tabs/origin_space_controller.h"
#include "brave/components/tabs/public/tree_tab_node.h"
#include "brave/components/tabs/public/tree_tab_node_tab_collection.h"
#include "chrome/browser/ui/browser_window/public/browser_window_features.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "components/sessions/content/session_tab_helper.h"
#include "components/sessions/core/command_storage_manager.h"
#include "components/sessions/core/session_id.h"
#include "components/sessions/core/session_service_commands.h"

namespace {

// Constructs commands for tree tab metadata. We walk up the tab-collection
// hierarchy to find an enclosing TREE_NODE collection and, if found, append
// AddTabExtraData commands for the three brave tree-node keys.
void BuildCommandsForTreeTab(
    const tabs::TabInterface* tab_interface,
    sessions::CommandStorageManager* command_storage_manager) {
  const tabs::TabCollection* parent = tab_interface->GetParentCollection();
  if (!parent || parent->type() != tabs::TabCollection::Type::TREE_NODE) {
    return;
  }

  const auto* tree_collection =
      static_cast<const tabs::TreeTabNodeTabCollection*>(parent);
  const auto& node = tree_collection->node();
  const SessionID session_id =
      sessions::SessionTabHelper::IdForTab(tab_interface->GetContents());
  const std::string node_id = node.id().ToString();
  auto parent_node_id = node.GetParentTreeNodeId();
  const std::string parent_id =
      parent_node_id ? parent_node_id->ToString() : "";
  command_storage_manager->AppendRebuildCommand(
      sessions::CreateAddTabExtraDataCommand(session_id, kBraveTreeNodeIdKey,
                                             node_id));
  command_storage_manager->AppendRebuildCommand(
      sessions::CreateAddTabExtraDataCommand(
          session_id, kBraveTreeParentNodeIdKey, parent_id));
  command_storage_manager->AppendRebuildCommand(
      sessions::CreateAddTabExtraDataCommand(session_id,
                                             kBraveTreeNodeCollapsedKey,
                                             node.collapsed() ? "1" : "0"));
}

// Origin keeps each page's Space and the window's selected Space as session
// extra data. Those entries only reach the file when they change, so a full
// rebuild -- which rewrites the session from live browser state -- drops them
// unless they are re-emitted here, and the next launch puts every page in the
// first Space.
void BuildCommandsForOriginSpaces(
    TabStripModel* tab_strip_model,
    sessions::CommandStorageManager* command_storage_manager) {
  if (tab_strip_model->empty()) {
    return;
  }
  BrowserWindowInterface* browser =
      tab_strip_model->GetTabAtIndex(0)->GetBrowserWindowInterface();
  if (!browser) {
    return;
  }
  OriginSpaceController* controller =
      browser->GetFeatures().origin_space_controller();
  if (!controller) {
    return;
  }
  for (int index = 0; index < tab_strip_model->count(); ++index) {
    std::map<std::string, std::string> tab_data;
    controller->MaybePopulateTabExtraData(index, &tab_data);
    const SessionID tab_id = sessions::SessionTabHelper::IdForTab(
        tab_strip_model->GetWebContentsAt(index));
    for (const auto& [key, value] : tab_data) {
      command_storage_manager->AppendRebuildCommand(
          sessions::CreateAddTabExtraDataCommand(tab_id, key, value));
    }
  }
  std::map<std::string, std::string> window_data;
  controller->MaybePopulateWindowExtraData(&window_data);
  for (const auto& [key, value] : window_data) {
    command_storage_manager->AppendRebuildCommand(
        sessions::CreateAddWindowExtraDataCommand(browser->GetSessionID(), key,
                                                  value));
  }
}

// This will be called in SessionTabBase::BuildCommandsForBrowser(). Plaster
// will insert this function there. Every piece of per-tab or per-window state
// Brave keeps as session extra data has to be re-emitted from here.
void BuildCommandsForTreeTab(
    TabStripModel* tab_strip_model,
    sessions::CommandStorageManager* command_storage_manager) {
  for (const auto* tab : *tab_strip_model) {
    BuildCommandsForTreeTab(tab, command_storage_manager);
  }
  BuildCommandsForOriginSpaces(tab_strip_model, command_storage_manager);
}

}  // namespace

#include <chrome/browser/sessions/session_service_base.cc>
