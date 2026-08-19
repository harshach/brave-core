// Copyright (c) 2026 The Brave Authors. All rights reserved.
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this file,
// You can obtain one at https://mozilla.org/MPL/2.0/.

#include "brave/browser/ui/tabs/public/vertical_tab_controller.h"

#include "base/command_line.h"
#include "brave/browser/ui/focus_mode/focus_mode_controller.h"
#include "brave/browser/ui/tabs/brave_tab_prefs.h"
#include "brave/browser/ui/tabs/public/switches.h"
#include "brave/components/brave_origin/buildflags/buildflags.h"
#include "build/build_config.h"
#include "chrome/browser/ui/browser_window/public/browser_window_features.h"
#include "chrome/browser/ui/tabs/features.h"
#include "components/prefs/pref_service.h"

// static
VerticalTabController* VerticalTabController::FromBrowser(
    BrowserWindowInterface* browser) {
  if (!browser) {
    return nullptr;
  }
  return browser->GetFeatures().vertical_tab_controller();
}

// static
const VerticalTabController* VerticalTabController::FromBrowser(
    const BrowserWindowInterface* browser) {
  if (!browser) {
    return nullptr;
  }
  return browser->GetFeatures().vertical_tab_controller();
}

VerticalTabController::VerticalTabController(
    BrowserWindowInterface::Type type,
    PrefService* prefs,
    FocusModeController* focus_mode_controller)
    : type_(type),
      prefs_(prefs),
      focus_mode_controller_(focus_mode_controller) {}

VerticalTabController::~VerticalTabController() = default;

bool VerticalTabController::SupportsBraveVerticalTabs() const {
  if (base::CommandLine::ForCurrentProcess()->HasSwitch(
          tabs::switches::kDisableVerticalTabsSwitch)) {
    return false;
  }

  if (tabs::IsVerticalTabsFeatureEnabled()) {
    // In case that Chromium's vertical tabs feature is enabled, we should not
    // show Brave's vertical tabs.
    return false;
  }

  return type_ == BrowserWindowInterface::TYPE_NORMAL;
}

bool VerticalTabController::ShouldShowBraveVerticalTabs() const {
  if (!SupportsBraveVerticalTabs()) {
    return false;
  }

#if BUILDFLAG(IS_BRAVE_ORIGIN_BRANDED)
  // The workspace is Origin's only tab surface. Keeping this independent of
  // the legacy preference prevents horizontal tabs from reappearing after a
  // profile migration or a stale settings write.
  return true;
#else
  return prefs_->GetBoolean(brave_tabs::kVerticalTabsEnabled);
#endif
}

bool VerticalTabController::ShouldShowWindowTitleForVerticalTabs() const {
  if (!ShouldShowBraveVerticalTabs()) {
    return false;
  }

  if (focus_mode_controller_ && focus_mode_controller_->IsEnabled()) {
    return false;
  }

#if BUILDFLAG(IS_BRAVE_ORIGIN_BRANDED)
  return false;
#else
  return prefs_->GetBoolean(brave_tabs::kVerticalTabsShowTitleOnWindow);
#endif
}

bool VerticalTabController::IsFloatingVerticalTabsEnabled() const {
  if (!ShouldShowBraveVerticalTabs()) {
    return false;
  }

#if BUILDFLAG(IS_BRAVE_ORIGIN_BRANDED)
  // Focus mode has its own transient hiding behavior. Outside focus mode the
  // Origin workspace remains a stable, resizable navigation surface.
  return false;
#else
  if (ShouldHideVerticalTabsCompletelyWhenCollapsed()) {
    // In this case, we should support floating mode regardless of the setting
    // of kVerticalTabsFloatingEnabled.
    return true;
  }

  if (!ShouldShowVerticalTabToggleButton()) {
    // When the toggle button is hidden, there is no other way to expand
    // collapsed vertical tabs, so floating mode must stay on regardless of
    // the setting of kVerticalTabsFloatingEnabled.
    return true;
  }

  return prefs_->GetBoolean(brave_tabs::kVerticalTabsFloatingEnabled);
#endif
}

bool VerticalTabController::IsVerticalTabOnRight() const {
#if BUILDFLAG(IS_BRAVE_ORIGIN_BRANDED)
  return false;
#else
  return prefs_->GetBoolean(brave_tabs::kVerticalTabsOnRight);
#endif
}

bool VerticalTabController::ShouldHideVerticalTabsCompletelyWhenCollapsed()
    const {
#if BUILDFLAG(IS_BRAVE_ORIGIN_BRANDED)
  // Origin's sidebar is a complete workspace surface, not an icon-only tab
  // rail. Collapsing it should return all of that width to the active page.
  return true;
#else
  return base::FeatureList::IsEnabled(tabs::kBraveVerticalTabHideCompletely) &&
         prefs_->GetBoolean(
             brave_tabs::kVerticalTabsHideCompletelyWhenCollapsed);
#endif
}

bool VerticalTabController::ShouldShowVerticalTabToggleButton() const {
  if (!ShouldShowBraveVerticalTabs()) {
    return false;
  }

#if BUILDFLAG(IS_BRAVE_ORIGIN_BRANDED)
  // The toolbar control is the persistent way to restore a fully hidden
  // workspace sidebar.
  return true;
#else
  return prefs_->GetBoolean(brave_tabs::kVerticalTabsShowToggleButton);
#endif
}

base::WeakPtr<VerticalTabController> VerticalTabController::GetWeakPtr() {
  return weak_ptr_factory_.GetWeakPtr();
}
