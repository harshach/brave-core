// Copyright (c) 2026 The Brave Authors. All rights reserved.
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this file,
// You can obtain one at https://mozilla.org/MPL/2.0/.

#include "brave/browser/ui/startup/origin_external_link_router.h"

#include <algorithm>
#include <memory>
#include <utility>

#include "base/functional/bind.h"
#include "base/task/sequenced_task_runner.h"
#include "brave/browser/ui/tabs/origin_space_controller.h"
#include "brave/browser/workspaces/workspace_service.h"
#include "brave/browser/workspaces/workspace_service_factory.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/browser_window.h"
#include "chrome/browser/ui/browser_window/public/browser_window_features.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface_iterator.h"
#include "chrome/browser/ui/browser_window/public/profile_browser_collection.h"
#include "chrome/browser/ui/navigator/browser_navigator_params.h"
#include "chrome/browser/ui/tabs/split_tab_metrics.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "components/split_tabs/split_tab_visual_data.h"
#include "content/public/browser/web_contents.h"
#include "ui/base/base_window.h"
#include "ui/base/window_open_disposition.h"
#include "ui/display/screen.h"
#include "ui/gfx/geometry/rect.h"
#include "url/gurl.h"
#include "url/url_constants.h"

namespace origin_external_link {
namespace {

constexpr char kTemporaryLinkAppName[] = "brave-origin-temporary-link";
constexpr int kTemporaryWindowWidth = 960;
constexpr int kTemporaryWindowHeight = 600;

Browser* AsBrowser(BrowserWindowInterface* browser) {
  return browser ? browser->GetBrowserForMigrationOnly() : nullptr;
}

Browser* FindTemporaryBrowser(Profile* profile) {
  for (BrowserWindowInterface* browser : GetAllBrowserWindowInterfaces()) {
    Browser* candidate = AsBrowser(browser);
    if (candidate && candidate->GetProfile() == profile &&
        IsTemporaryLinkBrowser(candidate) && !candidate->IsDeleteScheduled()) {
      return candidate;
    }
  }
  return nullptr;
}

Browser* FindNormalBrowser(Profile* profile,
                           BrowserWindowInterface* fallback_browser) {
  Browser* fallback = AsBrowser(fallback_browser);
  const bool fallback_is_eligible =
      fallback && fallback->GetProfile() == profile &&
      fallback->is_type_normal() && !fallback->IsDeleteScheduled();
  if (fallback_is_eligible && !fallback->tab_strip_model()->empty()) {
    return fallback;
  }
  if (Browser* recent =
          AsBrowser(ProfileBrowserCollection::GetForProfile(profile)
                        ->FindTabbedBrowser())) {
    return recent;
  }
  return fallback_is_eligible ? fallback : nullptr;
}

Browser* GetOrCreateNormalBrowser(Profile* profile) {
  if (Browser* browser = FindNormalBrowser(profile, nullptr)) {
    return browser;
  }
  Browser::CreateParams create_params(profile, /*user_gesture=*/false);
  create_params.should_trigger_session_restore = false;
  return Browser::Create(create_params);
}

base::WeakPtr<Browser> GetSyntheticFallback(
    BrowserWindowInterface* fallback_browser,
    Profile* profile,
    Browser* routed_browser) {
  Browser* fallback = AsBrowser(fallback_browser);
  if (!fallback || fallback == routed_browser ||
      fallback->GetProfile() != profile || !fallback->is_type_normal() ||
      fallback->IsDeleteScheduled() || !fallback->tab_strip_model()->empty()) {
    return {};
  }
  return fallback->AsWeakPtr();
}

Browser* CreateTemporaryBrowser(Profile* profile,
                                BrowserWindowInterface* fallback_browser) {
  Browser::CreateParams create_params =
      Browser::CreateParams::CreateForAppPopup(
          kTemporaryLinkAppName, /*trusted_source=*/true, gfx::Rect(), profile,
          /*user_gesture=*/false);
  create_params.omit_from_session_restore = true;
  create_params.should_trigger_session_restore = false;
  gfx::Rect anchor_bounds;
  if (fallback_browser && fallback_browser->GetWindow()) {
    anchor_bounds = fallback_browser->GetWindow()->GetBounds();
  } else if (display::Screen::Get()) {
    anchor_bounds = display::Screen::Get()->GetPrimaryDisplay().work_area();
  }
  if (!anchor_bounds.IsEmpty()) {
    const int width = std::min(kTemporaryWindowWidth, anchor_bounds.width());
    const int height = std::min(kTemporaryWindowHeight, anchor_bounds.height());
    create_params.initial_bounds =
        gfx::Rect(anchor_bounds.CenterPoint().x() - width / 2,
                  anchor_bounds.CenterPoint().y() - height / 2, width, height);
  }
  if (create_params.initial_bounds.IsEmpty()) {
    create_params.initial_bounds =
        gfx::Rect(120, 100, kTemporaryWindowWidth, kTemporaryWindowHeight);
  }
  return Browser::Create(create_params);
}

bool IsOnlyPlaceholderTab(Browser* browser) {
  if (!browser || browser->tab_strip_model()->count() > 1) {
    return false;
  }
  if (browser->tab_strip_model()->empty()) {
    return true;
  }
  const GURL& visible_url =
      browser->tab_strip_model()->GetWebContentsAt(0)->GetVisibleURL();
  return visible_url.is_empty() || visible_url == browser->GetNewTabURL() ||
         visible_url == GURL(url::kAboutBlankURL);
}

void FinalizeRoutedBrowser(base::WeakPtr<Browser> browser,
                           base::WeakPtr<Browser> synthetic_fallback) {
  if (synthetic_fallback && !synthetic_fallback->IsDeleteScheduled() &&
      IsOnlyPlaceholderTab(synthetic_fallback.get())) {
    if (BrowserWindow* window =
            BrowserWindow::FromBrowser(synthetic_fallback.get())) {
      window->Close();
    }
  }
  if (!browser || browser->IsDeleteScheduled()) {
    return;
  }
  BrowserWindow* window = BrowserWindow::FromBrowser(browser.get());
  if (!window) {
    return;
  }
  window->Show();
  window->Activate();
}

OriginSpaceController* GetSpaceController(Browser* browser) {
  return browser ? browser->GetFeatures().origin_space_controller() : nullptr;
}

}  // namespace

bool IsTemporaryLinkBrowser(const BrowserWindowInterface* browser) {
  if (!browser) {
    return false;
  }
  const Browser* concrete = browser->GetBrowserForMigrationOnly();
  return concrete && concrete->app_name() == kTemporaryLinkAppName;
}

void ConfigureNavigation(const GURL& url,
                         BrowserWindowInterface* fallback_browser,
                         NavigateParams* params) {
  if (!params || !url.SchemeIsHTTPOrHTTPS()) {
    return;
  }
  Profile* profile = params->initiating_profile.get();
  if (!profile && fallback_browser) {
    profile = fallback_browser->GetProfile();
  }
  if (!profile) {
    return;
  }
  WorkspaceService* workspace_service =
      WorkspaceServiceFactory::GetForProfile(profile);
  if (!workspace_service) {
    return;
  }

  if (const auto mapped_space =
          workspace_service->GetOriginSpaceForDomain(url)) {
    Browser* target = FindNormalBrowser(profile, fallback_browser);
    if (!target) {
      target = GetOrCreateNormalBrowser(profile);
    }
    if (target) {
      if (OriginSpaceController* controller = GetSpaceController(target)) {
        controller->SelectSpace(*mapped_space);
        params->browser = target;
        params->disposition = WindowOpenDisposition::NEW_FOREGROUND_TAB;
        params->tabstrip_add_types |= AddTabTypes::ADD_ACTIVE;
        params->window_action = NavigateParams::WindowAction::kShowWindow;
        base::WeakPtr<Browser> synthetic_fallback =
            GetSyntheticFallback(fallback_browser, profile, target);
        if (synthetic_fallback) {
          base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
              FROM_HERE,
              base::BindOnce(&FinalizeRoutedBrowser, target->AsWeakPtr(),
                             std::move(synthetic_fallback)));
        }
        return;
      }
    }
  }

  Browser* temporary = FindTemporaryBrowser(profile);
  if (!temporary) {
    temporary = CreateTemporaryBrowser(profile, fallback_browser);
  }
  base::WeakPtr<Browser> synthetic_fallback =
      GetSyntheticFallback(fallback_browser, profile, temporary);
  params->browser = temporary;
  params->disposition = WindowOpenDisposition::NEW_FOREGROUND_TAB;
  params->tabstrip_add_types |= AddTabTypes::ADD_ACTIVE;
  params->window_action = NavigateParams::WindowAction::kShowWindow;

  // StartupBrowserCreator shows its normal fallback after processing all
  // command-line tabs. Remove that window when it was created solely as an
  // untouched placeholder, then re-activate the temporary frame.
  base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
      FROM_HERE, base::BindOnce(&FinalizeRoutedBrowser, temporary->AsWeakPtr(),
                                std::move(synthetic_fallback)));
}

std::string GetSuggestedSpaceId(Browser* temporary_browser, const GURL& url) {
  if (!temporary_browser) {
    return {};
  }
  Profile* profile = temporary_browser->GetProfile();
  WorkspaceService* workspace_service =
      WorkspaceServiceFactory::GetForProfile(profile);
  if (!workspace_service || workspace_service->GetOriginSpaces().empty()) {
    return {};
  }
  if (const auto mapped = workspace_service->GetOriginSpaceForDomain(url)) {
    return *mapped;
  }

  const std::string domain = WorkspaceService::GetOriginDomainKey(url);
  Browser* fallback = FindNormalBrowser(profile, nullptr);
  if (!domain.empty()) {
    for (BrowserWindowInterface* window : GetAllBrowserWindowInterfaces()) {
      Browser* browser = AsBrowser(window);
      OriginSpaceController* controller = GetSpaceController(browser);
      if (!browser || browser->GetProfile() != profile ||
          !browser->is_type_normal() || !controller) {
        continue;
      }
      for (int index = 0; index < browser->tab_strip_model()->count();
           ++index) {
        content::WebContents* contents =
            browser->tab_strip_model()->GetWebContentsAt(index);
        if (WorkspaceService::GetOriginDomainKey(contents->GetVisibleURL()) ==
            domain) {
          const std::string space_id = controller->GetSpaceIdForTab(contents);
          if (workspace_service->GetOriginSpace(space_id)) {
            return space_id;
          }
        }
      }
    }
  }
  if (OriginSpaceController* controller = GetSpaceController(fallback)) {
    return controller->active_space_id();
  }
  return workspace_service->GetOriginSpaces().front().id;
}

bool KeepActivePage(Browser* temporary_browser,
                    const std::string& space_id,
                    bool always_open_domain_here,
                    KeepDisposition disposition) {
  if (!temporary_browser || !IsTemporaryLinkBrowser(temporary_browser)) {
    return false;
  }
  WorkspaceService* workspace_service =
      WorkspaceServiceFactory::GetForProfile(temporary_browser->GetProfile());
  if (!workspace_service || !workspace_service->GetOriginSpace(space_id)) {
    return false;
  }
  Browser* target = GetOrCreateNormalBrowser(temporary_browser->GetProfile());
  OriginSpaceController* controller = GetSpaceController(target);
  TabStripModel* source_model = temporary_browser->tab_strip_model();
  if (!target || !controller ||
      source_model->active_index() == TabStripModel::kNoTab) {
    return false;
  }

  const int source_index = source_model->active_index();
  content::WebContents* active_contents =
      source_model->GetWebContentsAt(source_index);
  const GURL page_url = active_contents->GetVisibleURL();
  if (always_open_domain_here) {
    workspace_service->SetOriginSpaceForDomain(page_url, space_id);
  }

  controller->SelectSpace(space_id);
  TabStripModel* target_model = target->tab_strip_model();
  const int previous_active_index = target_model->active_index();
  content::WebContents* previous_active_contents =
      previous_active_index != TabStripModel::kNoTab
          ? target_model->GetWebContentsAt(previous_active_index)
          : nullptr;
  const bool has_destination_page =
      previous_active_contents &&
      controller->GetSpaceIdForTab(previous_active_contents) == space_id;
  content::WebContents* split_partner =
      disposition == KeepDisposition::kSplit && has_destination_page &&
              !controller->IsTabPlaceholder(previous_active_contents)
          ? previous_active_contents
          : nullptr;
  std::unique_ptr<content::WebContents> contents =
      source_model->DetachWebContentsAtForInsertion(source_index);
  int target_index = previous_active_index;
  if (disposition == KeepDisposition::kReplace && has_destination_page) {
    target_model->DiscardWebContentsAt(previous_active_index,
                                       std::move(contents));
  } else {
    target_index = target_model->InsertWebContentsAt(
        target_model->count(), std::move(contents), AddTabTypes::ADD_ACTIVE);
  }
  content::WebContents* inserted = target_model->GetWebContentsAt(target_index);
  controller->MoveTabToSpace(inserted, space_id);

  const int split_partner_index =
      split_partner ? target_model->GetIndexOfWebContents(split_partner)
                    : TabStripModel::kNoTab;
  const int inserted_index = target_model->GetIndexOfWebContents(inserted);
  if (split_partner_index != TabStripModel::kNoTab &&
      inserted_index != TabStripModel::kNoTab &&
      split_partner_index != inserted_index &&
      !target_model->GetSplitForTab(split_partner_index).has_value() &&
      !target_model->GetSplitForTab(inserted_index).has_value()) {
    target_model->AddToNewSplit(
        {split_partner_index},
        split_tabs::SplitTabVisualData(split_tabs::SplitTabLayout::kSideBySide),
        split_tabs::SplitTabCreatedSource::kKeyboardShortcut);
  }
  if (BrowserWindow* window = BrowserWindow::FromBrowser(target)) {
    window->Show();
    window->Activate();
  }
  if (source_model->count() == 0) {
    if (BrowserWindow* window = BrowserWindow::FromBrowser(temporary_browser)) {
      window->Close();
    }
  }
  return true;
}

void DiscardActivePage(Browser* temporary_browser) {
  if (!temporary_browser || !IsTemporaryLinkBrowser(temporary_browser)) {
    return;
  }
  TabStripModel* model = temporary_browser->tab_strip_model();
  if (model->count() == 1) {
    if (BrowserWindow* window = BrowserWindow::FromBrowser(temporary_browser)) {
      window->Close();
    }
    return;
  }
  if (model->active_index() != TabStripModel::kNoTab) {
    model->CloseWebContentsAt(model->active_index(),
                              TabCloseTypes::CLOSE_USER_GESTURE);
  }
}

}  // namespace origin_external_link
