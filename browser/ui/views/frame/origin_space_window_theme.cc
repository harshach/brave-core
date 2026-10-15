// Copyright (c) 2026 The Brave Authors. All rights reserved.
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this file,
// You can obtain one at https://mozilla.org/MPL/2.0/.

#include "brave/browser/ui/views/frame/origin_space_window_theme.h"

#include <utility>

#include "brave/browser/ui/tabs/origin_space_theme.h"

OriginSpaceWindowTheme::OriginSpaceWindowTheme(
    OriginSpaceController& controller,
    WorkspaceService& workspace_service,
    base::RepeatingClosure on_changed)
    : controller_(controller),
      workspace_service_(workspace_service),
      on_changed_(std::move(on_changed)) {
  controller_observation_.Observe(&controller);
  workspace_observation_.Observe(&workspace_service);
  Update();
}

OriginSpaceWindowTheme::~OriginSpaceWindowTheme() = default;

void OriginSpaceWindowTheme::SetPreviewSpace(
    std::optional<std::string> space_id) {
  preview_space_id_ = std::move(space_id);
  Update();
}

void OriginSpaceWindowTheme::OnOriginSpaceControllerChanged() {
  Update();
}

void OriginSpaceWindowTheme::OnOriginSpacesChanged() {
  Update();
}

void OriginSpaceWindowTheme::Update() {
  const OriginSpaceMetadata* space = workspace_service_->GetOriginSpace(
      preview_space_id_.value_or(controller_->active_space_id()));
  ui::ColorProviderKey::InitializerSupplier* supplier =
      space ? origin_space_theme::GetColorSupplier(
                  space->theme,
                  workspace_service_->GetOriginSpaceThemeIntensity())
            : nullptr;
  if (supplier == color_supplier_) {
    return;
  }
  color_supplier_ = supplier;
  on_changed_.Run();
}
