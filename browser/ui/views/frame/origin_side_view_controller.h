// Copyright (c) 2026 The Brave Authors. All rights reserved.
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this file,
// You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef BRAVE_BROWSER_UI_VIEWS_FRAME_ORIGIN_SIDE_VIEW_CONTROLLER_H_
#define BRAVE_BROWSER_UI_VIEWS_FRAME_ORIGIN_SIDE_VIEW_CONTROLLER_H_

#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "base/scoped_observation.h"
#include "brave/browser/ui/tabs/origin_later_service.h"
#include "brave/browser/ui/tabs/origin_space_controller.h"
#include "brave/browser/ui/views/frame/origin_side_view.h"
#include "url/gurl.h"

class Browser;
class BraveMultiContentsView;

namespace content {
class WebContents;
}

namespace sidebar {
class SidebarWebPanelController;
}

// Runs one window's side view. The panel follows the active Space: in Page
// mode it shows that Space's side page, a real tab kept out of the page list
// and drawn by Brave's web panel; in the other modes it shows a native tool.
class OriginSideViewController : public OriginSideView::Delegate,
                                 public OriginSpaceController::Observer,
                                 public OriginLaterService::Observer {
 public:
  OriginSideViewController(Browser* browser,
                           BraveMultiContentsView* multi_contents_view);
  OriginSideViewController(const OriginSideViewController&) = delete;
  OriginSideViewController& operator=(const OriginSideViewController&) = delete;
  ~OriginSideViewController() override;

  bool IsOpen() const { return open_; }
  OriginSideViewMode mode() const { return mode_; }

  // Shows the panel as it was last, or hides it.
  void Toggle();
  void Show(OriginSideViewMode mode);
  // Shows `mode`, or hides the panel when it is already showing it.
  void ToggleMode(OriginSideViewMode mode);
  void Hide();

  // Moves the active page to the side, or the side page back to the list
  // when it is the one with focus.
  void ToggleActivePageSide();
  // Makes `contents` its Space's side page and shows it. Fails for the Space's
  // blank canvas, for split pages, and for pages with subpages, which would
  // otherwise be left hanging under a page that is no longer in the list.
  bool MovePageToSide(content::WebContents* contents);

  OriginSideView* side_view() { return side_view_; }

  // OriginSideView::Delegate:
  void SelectSideViewMode(OriginSideViewMode mode) override;
  void CloseSideView() override;
  void ReturnSidePageToMain() override;
  void CloseSidePage() override;
  void ResizeSideView(int width, bool done) override;

 private:
  // OriginSpaceController::Observer:
  void OnOriginSpaceControllerChanged() override;

  // OriginLaterService::Observer:
  void OnOriginLaterChanged() override;

  // Opens a page from a tool: the open copy if there is one, otherwise a new
  // page in the active Space.
  void OpenPage(const GURL& url);
  std::u16string GetSpaceName(const std::string& space_id) const;

  // Brings the panel, the web panel's contents and the header in line with
  // `open_`, `mode_` and the active Space.
  void Sync();
  sidebar::SidebarWebPanelController* GetWebPanelController() const;
  // Moves focus out of `contents` when it is in the panel, so the panel can
  // let go of it without leaving the main area without a page.
  void FocusMainPageIfActive(content::WebContents* contents);

  raw_ptr<Browser> browser_ = nullptr;
  raw_ptr<BraveMultiContentsView> multi_contents_view_ = nullptr;
  raw_ptr<OriginSpaceController> space_controller_ = nullptr;
  // Owned by `multi_contents_view_`.
  raw_ptr<OriginSideView> side_view_ = nullptr;

  bool open_ = false;
  OriginSideViewMode mode_ = OriginSideViewMode::kPage;
  // The side page this controller put in the web panel. Anything else there
  // belongs to a Brave sidebar item and is left alone.
  base::WeakPtr<content::WebContents> attached_side_page_;
  int width_ = OriginSideView::kDefaultWidth;
  // Opens the panel once for a side page restored with the window.
  bool showed_restored_side_page_ = false;
  bool syncing_ = false;

  raw_ptr<OriginLaterService> later_service_ = nullptr;

  base::ScopedObservation<OriginSpaceController,
                          OriginSpaceController::Observer>
      space_observation_{this};
  base::ScopedObservation<OriginLaterService, OriginLaterService::Observer>
      later_observation_{this};
  base::WeakPtrFactory<OriginSideViewController> weak_factory_{this};
};

#endif  // BRAVE_BROWSER_UI_VIEWS_FRAME_ORIGIN_SIDE_VIEW_CONTROLLER_H_
