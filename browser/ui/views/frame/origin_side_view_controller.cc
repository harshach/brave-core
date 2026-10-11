// Copyright (c) 2026 The Brave Authors. All rights reserved.
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this file,
// You can obtain one at https://mozilla.org/MPL/2.0/.

#include "brave/browser/ui/views/frame/origin_side_view_controller.h"

#include <algorithm>
#include <memory>
#include <optional>
#include <string>

#include "base/auto_reset.h"
#include "base/check.h"
#include "base/functional/bind.h"
#include "base/location.h"
#include "base/strings/utf_string_conversions.h"
#include "base/task/sequenced_task_runner.h"
#include "brave/browser/ui/sidebar/sidebar_controller.h"
#include "brave/browser/ui/sidebar/sidebar_utils.h"
#include "brave/browser/ui/sidebar/sidebar_web_panel_controller.h"
#include "brave/browser/ui/tabs/brave_tab_strip_model.h"
#include "brave/browser/ui/views/frame/origin_side_view_downloads.h"
#include "brave/browser/ui/views/frame/origin_side_view_reminders.h"
#include "brave/browser/ui/views/frame/origin_side_view_shortcuts.h"
#include "brave/browser/ui/views/frame/origin_side_view_snoozed.h"
#include "brave/browser/ui/views/frame/origin_side_view_tags.h"
#include "brave/browser/ui/views/frame/split_view/brave_multi_contents_view.h"
#include "brave/browser/workspaces/pref_names.h"
#include "brave/browser/workspaces/workspace_service.h"
#include "brave/browser/workspaces/workspace_service_factory.h"
#include "chrome/browser/bookmarks/bookmark_model_factory.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/browser_tabstrip.h"
#include "chrome/browser/ui/browser_window/public/browser_window_features.h"
#include "chrome/browser/ui/chrome_pages.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "components/favicon/content/content_favicon_driver.h"
#include "components/prefs/pref_service.h"
#include "components/tabs/public/tab_interface.h"
#include "content/public/browser/web_contents.h"
#include "ui/base/models/image_model.h"
#include "url/gurl.h"

OriginSideViewController::OriginSideViewController(
    Browser* browser,
    BraveMultiContentsView* multi_contents_view)
    : browser_(browser),
      multi_contents_view_(multi_contents_view),
      space_controller_(browser->GetFeatures().origin_space_controller()) {
  CHECK(browser_);
  CHECK(multi_contents_view_);
  CHECK(space_controller_);

  width_ = std::clamp(
      browser_->GetProfile()->GetPrefs()->GetInteger(kOriginSideViewWidthPref),
      OriginSideView::kMinimumWidth, OriginSideView::kMaximumWidth);

  auto side_view = std::make_unique<OriginSideView>(this);
  side_view->SetVisible(false);
  side_view_ = side_view.get();
  multi_contents_view_->SetOriginSideView(std::move(side_view),
                                          OriginSideView::GetWebPanelInsets());
  multi_contents_view_->SetOriginSideViewWidth(width_);

  side_view_->SetToolView(OriginSideViewMode::kShortcuts,
                          std::make_unique<OriginSideViewShortcuts>());
  side_view_->SetToolView(
      OriginSideViewMode::kDownloads,
      std::make_unique<OriginSideViewDownloads>(
          browser_->GetProfile(),
          base::BindRepeating(
              [](base::WeakPtr<OriginSideViewController> controller) {
                if (controller) {
                  chrome::ShowDownloads(controller->browser_.get());
                }
              },
              weak_factory_.GetWeakPtr())));

  const auto open_page = base::BindRepeating(
      [](base::WeakPtr<OriginSideViewController> controller, const GURL& url) {
        if (controller) {
          controller->OpenPage(url);
        }
      },
      weak_factory_.GetWeakPtr());
  // Reminders and snoozed pages belong to the regular profile; private
  // windows have neither.
  later_service_ =
      OriginLaterServiceFactory::GetForProfile(browser_->GetProfile());
  if (later_service_) {
    side_view_->SetToolView(
        OriginSideViewMode::kReminders,
        std::make_unique<OriginSideViewReminders>(later_service_, open_page));
    side_view_->SetToolView(
        OriginSideViewMode::kSnoozed,
        std::make_unique<OriginSideViewSnoozed>(
            later_service_,
            base::BindRepeating(
                [](base::WeakPtr<OriginSideViewController> controller,
                   const std::string& space_id) {
                  return controller ? controller->GetSpaceName(space_id)
                                    : std::u16string();
                },
                weak_factory_.GetWeakPtr())));
    later_observation_.Observe(later_service_);
  }
  if (auto* bookmark_model =
          BookmarkModelFactory::GetForBrowserContext(browser_->GetProfile())) {
    side_view_->SetToolView(
        OriginSideViewMode::kTags,
        std::make_unique<OriginSideViewTags>(bookmark_model, open_page));
  }

  space_observation_.Observe(space_controller_);
  OnOriginLaterChanged();
  Sync(/*can_change_tabs=*/true);
}

OriginSideViewController::~OriginSideViewController() {
  // The view outlives this controller by a little during window teardown.
  side_view_->ClearDelegate();
}

void OriginSideViewController::Toggle() {
  if (open_) {
    Hide();
  } else {
    Show(mode_);
  }
}

void OriginSideViewController::Show(OriginSideViewMode mode) {
  open_ = true;
  mode_ = mode;
  showed_restored_side_page_ = true;
  Sync(/*can_change_tabs=*/true);
}

void OriginSideViewController::ToggleMode(OriginSideViewMode mode) {
  if (open_ && mode_ == mode) {
    Hide();
  } else {
    Show(mode);
  }
}

void OriginSideViewController::Hide() {
  open_ = false;
  // Hidden on purpose: a side page coming back no longer reopens the panel.
  showed_restored_side_page_ = true;
  Sync(/*can_change_tabs=*/true);
}

void OriginSideViewController::ToggleActivePageSide() {
  content::WebContents* active =
      browser_->tab_strip_model()->GetActiveWebContents();
  if (!active) {
    return;
  }
  if (space_controller_->IsSidePage(active)) {
    ReturnSidePageToMain();
    return;
  }
  MovePageToSide(active);
}

bool OriginSideViewController::MovePageToSide(content::WebContents* contents) {
  auto* model = static_cast<BraveTabStripModel*>(browser_->tab_strip_model());
  int index = model->GetIndexOfWebContents(contents);
  if (index == TabStripModel::kNoTab || !GetWebPanelController() ||
      space_controller_->IsTabPlaceholder(contents) ||
      space_controller_->IsSidePage(contents) ||
      model->GetTabAtIndex(index)->IsSplit() ||
      !model->GetTreeTabDescendantIndices(index).empty()) {
    return false;
  }

  // A page nested in a branch leaves it first, so no visible branch keeps a
  // child that the list no longer shows.
  if (model->active_index() != index) {
    model->ActivateTabAt(index);
  }
  model->PromoteSelectedTreeTabsToRoot();
  index = model->GetIndexOfWebContents(contents);

  // The main area moves on to the Space's next page before this one leaves.
  const int leaving[] = {index};
  if (!space_controller_->SelectReplacementTabForClose(leaving)) {
    chrome::AddTabAt(browser_, GURL(), -1, /*foreground=*/true);
  }

  open_ = true;
  mode_ = OriginSideViewMode::kPage;
  showed_restored_side_page_ = true;
  space_controller_->SetSidePage(contents, true);
  Sync(/*can_change_tabs=*/true);
  return true;
}

void OriginSideViewController::SelectSideViewMode(OriginSideViewMode mode) {
  Show(mode);
}

void OriginSideViewController::CloseSideView() {
  Hide();
}

void OriginSideViewController::ReturnSidePageToMain() {
  content::WebContents* side_page =
      space_controller_->GetSidePage(space_controller_->active_space_id());
  if (!side_page) {
    return;
  }
  FocusMainPageIfActive(side_page);
  if (auto* web_panel = GetWebPanelController();
      web_panel && web_panel->panel_contents() == side_page) {
    web_panel->SetPanelContents(nullptr);
    attached_side_page_.reset();
  }
  // Putting the page back finishes with the panel, as SigmaOS's "Send side
  // page to main" does.
  open_ = false;
  space_controller_->SetSidePage(side_page, false);
  auto* model = browser_->tab_strip_model();
  const int index = model->GetIndexOfWebContents(side_page);
  if (index != TabStripModel::kNoTab) {
    model->ActivateTabAt(index);
  }
  Sync(/*can_change_tabs=*/true);
}

void OriginSideViewController::CloseSidePage() {
  content::WebContents* side_page =
      space_controller_->GetSidePage(space_controller_->active_space_id());
  if (!side_page) {
    return;
  }
  FocusMainPageIfActive(side_page);
  auto* model = browser_->tab_strip_model();
  const int index = model->GetIndexOfWebContents(side_page);
  if (index != TabStripModel::kNoTab) {
    model->CloseWebContentsAt(index,
                              TabCloseTypes::CLOSE_USER_GESTURE |
                                  TabCloseTypes::CLOSE_CREATE_HISTORICAL_TAB);
  }
}

void OriginSideViewController::ResizeSideView(int width, bool done) {
  width_ = std::clamp(width, OriginSideView::kMinimumWidth,
                      OriginSideView::kMaximumWidth);
  multi_contents_view_->SetOriginSideViewWidth(width_);
  if (done) {
    browser_->GetProfile()->GetPrefs()->SetInteger(kOriginSideViewWidthPref,
                                                   width_);
  }
}

void OriginSideViewController::OnOriginSpaceControllerChanged() {
  // A side page that came back with the window is shown once; after that the
  // panel stays the way the user leaves it.
  if (!showed_restored_side_page_ && !open_ &&
      space_controller_->GetSidePage(space_controller_->active_space_id())) {
    showed_restored_side_page_ = true;
    open_ = true;
    mode_ = OriginSideViewMode::kPage;
  }
  // The tab strip may be notifying its observers, when it can't change.
  Sync(/*can_change_tabs=*/false);
}

void OriginSideViewController::OnOriginLaterChanged() {
  // Due reminders show as a count on the panel's Reminders button.
  side_view_->SetModeBadge(
      OriginSideViewMode::kReminders,
      later_service_ ? later_service_->GetDueReminderCount() : 0);
}

void OriginSideViewController::OpenPage(const GURL& url) {
  if (!url.is_valid()) {
    return;
  }
  auto* model = browser_->tab_strip_model();
  for (int index = 0; index < model->count(); ++index) {
    content::WebContents* contents = model->GetWebContentsAt(index);
    if (contents->GetLastCommittedURL() == url &&
        !space_controller_->IsSidePage(contents)) {
      model->ActivateTabAt(index);
      return;
    }
  }
  chrome::AddTabAt(browser_, url, -1, /*foreground=*/true);
}

std::u16string OriginSideViewController::GetSpaceName(
    const std::string& space_id) const {
  auto* workspace_service =
      WorkspaceServiceFactory::GetForProfile(browser_->GetProfile());
  const auto* space =
      workspace_service ? workspace_service->GetOriginSpace(space_id) : nullptr;
  return space ? base::UTF8ToUTF16(space->name) : std::u16string();
}

void OriginSideViewController::ScheduleSync() {
  if (sync_scheduled_) {
    return;
  }
  sync_scheduled_ = true;
  base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
      FROM_HERE, base::BindOnce(&OriginSideViewController::RunScheduledSync,
                                weak_factory_.GetWeakPtr()));
}

void OriginSideViewController::RunScheduledSync() {
  sync_scheduled_ = false;
  Sync(/*can_change_tabs=*/true);
}

void OriginSideViewController::Sync(bool can_change_tabs) {
  if (syncing_) {
    return;
  }
  base::AutoReset<bool> syncing(&syncing_, true);

  content::WebContents* side_page =
      space_controller_->GetSidePage(space_controller_->active_space_id());
  if (side_page) {
    std::u16string title = side_page->GetTitle();
    if (title.empty()) {
      title = base::UTF8ToUTF16(side_page->GetVisibleURL().host());
    }
    ui::ImageModel favicon;
    if (auto* driver =
            favicon::ContentFaviconDriver::FromWebContents(side_page);
        driver && driver->FaviconIsValid()) {
      favicon = ui::ImageModel::FromImage(driver->GetFavicon());
    }
    side_view_->SetSidePage(std::move(title), favicon);
  } else {
    side_view_->SetSidePage(std::nullopt, ui::ImageModel());
  }
  side_view_->SetMode(mode_);

  if (auto* web_panel = GetWebPanelController()) {
    content::WebContents* wanted =
        open_ && mode_ == OriginSideViewMode::kPage ? side_page : nullptr;
    const content::WebContents* shown = web_panel->panel_contents();
    const bool ours = !shown || shown == attached_side_page_.get();
    const content::WebContents* active =
        browser_->tab_strip_model()->GetActiveWebContents();
    if (ours && shown != wanted && !can_change_tabs && active &&
        (active == shown || active == wanted)) {
      // The active page has to leave the main area first. Let go of a page
      // that isn't active now and finish the rest once the tab strip can
      // change.
      if (shown && shown != active) {
        web_panel->SetPanelContents(nullptr);
        attached_side_page_.reset();
      }
      ScheduleSync();
    } else if (ours && shown != wanted) {
      FocusMainPageIfActive(const_cast<content::WebContents*>(shown));
      // A restored or switched-to side page can be the active tab. It can't be
      // on screen in the main area and the panel at once.
      FocusMainPageIfActive(wanted);
      web_panel->SetPanelContents(wanted);
      attached_side_page_ =
          wanted ? wanted->GetWeakPtr() : base::WeakPtr<content::WebContents>();
    }
  }

  if (side_view_->GetVisible() != open_) {
    side_view_->SetVisible(open_);
    multi_contents_view_->InvalidateLayout();
  }
}

sidebar::SidebarWebPanelController*
OriginSideViewController::GetWebPanelController() const {
  if (!sidebar::IsWebPanelFeatureEnabled()) {
    return nullptr;
  }
  auto* sidebar_controller = sidebar::SidebarController::From(browser_);
  return sidebar_controller ? sidebar_controller->GetWebPanelController()
                            : nullptr;
}

void OriginSideViewController::FocusMainPageIfActive(
    content::WebContents* contents) {
  auto* model = browser_->tab_strip_model();
  if (!contents || model->GetActiveWebContents() != contents) {
    return;
  }
  const int main_index = space_controller_->GetMainTabIndex();
  if (main_index == TabStripModel::kNoTab ||
      model->GetWebContentsAt(main_index) == contents) {
    // Nothing else in the Space: the main area gets a blank page.
    chrome::AddTabAt(browser_, GURL(), -1, /*foreground=*/true);
    return;
  }
  model->ActivateTabAt(main_index);
}
