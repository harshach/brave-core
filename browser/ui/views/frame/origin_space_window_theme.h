// Copyright (c) 2026 The Brave Authors. All rights reserved.
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this file,
// You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef BRAVE_BROWSER_UI_VIEWS_FRAME_ORIGIN_SPACE_WINDOW_THEME_H_
#define BRAVE_BROWSER_UI_VIEWS_FRAME_ORIGIN_SPACE_WINDOW_THEME_H_

#include <optional>
#include <string>

#include "base/functional/callback.h"
#include "base/memory/raw_ptr.h"
#include "base/memory/raw_ref.h"
#include "base/scoped_observation.h"
#include "brave/browser/ui/tabs/origin_space_controller.h"
#include "brave/browser/workspaces/workspace_service.h"
#include "ui/color/color_provider_key.h"

// Picks the colours for a window from the theme of the Space it shows.
class OriginSpaceWindowTheme : public OriginSpaceController::Observer,
                               public WorkspaceService::Observer {
 public:
  // `on_changed` runs whenever the window needs to re-theme.
  OriginSpaceWindowTheme(OriginSpaceController& controller,
                         WorkspaceService& workspace_service,
                         base::RepeatingClosure on_changed);
  OriginSpaceWindowTheme(const OriginSpaceWindowTheme&) = delete;
  OriginSpaceWindowTheme& operator=(const OriginSpaceWindowTheme&) = delete;
  ~OriginSpaceWindowTheme() override;

  ui::ColorProviderKey::InitializerSupplier* color_supplier() const {
    return color_supplier_;
  }

  // Shows `space_id`'s theme instead of the active Space's until cleared, so
  // the window previews a Space while it is being edited.
  void SetPreviewSpace(std::optional<std::string> space_id);

 private:
  // OriginSpaceController::Observer:
  void OnOriginSpaceControllerChanged() override;

  // WorkspaceService::Observer:
  void OnOriginSpacesChanged() override;

  void Update();

  const raw_ref<OriginSpaceController> controller_;
  const raw_ref<WorkspaceService> workspace_service_;
  const base::RepeatingClosure on_changed_;
  std::optional<std::string> preview_space_id_;
  raw_ptr<ui::ColorProviderKey::InitializerSupplier> color_supplier_ = nullptr;
  base::ScopedObservation<OriginSpaceController,
                          OriginSpaceController::Observer>
      controller_observation_{this};
  base::ScopedObservation<WorkspaceService, WorkspaceService::Observer>
      workspace_observation_{this};
};

#endif  // BRAVE_BROWSER_UI_VIEWS_FRAME_ORIGIN_SPACE_WINDOW_THEME_H_
