// Copyright (c) 2026 The Brave Authors. All rights reserved.
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this file,
// You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef BRAVE_BROWSER_UI_TABS_ORIGIN_SPACE_CONTROLLER_H_
#define BRAVE_BROWSER_UI_TABS_ORIGIN_SPACE_CONTROLLER_H_

#include <cstddef>
#include <map>
#include <memory>
#include <optional>
#include <string>

#include "base/containers/span.h"
#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "base/observer_list.h"
#include "base/observer_list_types.h"
#include "base/scoped_observation.h"
#include "brave/browser/workspaces/workspace_service.h"
#include "chrome/browser/ui/tabs/tab_strip_model_observer.h"
#include "components/sessions/core/session_id.h"
#include "url/gurl.h"

class Profile;
class TabStripModel;

namespace content {
class WebContents;
}

// Owns the live space state for one browser window. Space definitions live in
// WorkspaceService and are shared by all windows in the profile; the selected
// space and each tab's membership intentionally remain window-local.
class OriginSpaceController : public TabStripModelObserver,
                              public WorkspaceService::Observer {
 public:
  class Observer : public base::CheckedObserver {
   public:
    virtual void OnOriginSpaceControllerChanged() = 0;
  };

  OriginSpaceController(Profile* profile,
                        TabStripModel* tab_strip_model,
                        SessionID window_id);
  OriginSpaceController(const OriginSpaceController&) = delete;
  OriginSpaceController& operator=(const OriginSpaceController&) = delete;
  ~OriginSpaceController() override;

  const std::string& active_space_id() const { return active_space_id_; }
  bool SelectSpace(const std::string& space_id);
  bool SelectSpaceAtIndex(size_t index);

  std::string GetSpaceIdForTab(content::WebContents* contents) const;
  bool IsTabInActiveSpace(content::WebContents* contents) const;

  // Shows another Space's pages in the page list while the pointer rests on
  // its icon, as SigmaOS's "fast page switching" does. Only the list follows
  // it: the active Space, its pages, j/k and counts stay where they are, and
  // the preview ends when that Space becomes active or the preview is
  // cleared.
  void SetPreviewSpace(std::optional<std::string> space_id);
  const std::optional<std::string>& preview_space_id() const {
    return preview_space_id_;
  }
  // The renderer backing a newly-created Space starts on Brave's New Tab
  // page. Keep that renderer available as the Space canvas, but do not expose
  // it as a page row or include it in the user-facing page count until it has
  // navigated somewhere meaningful.
  bool IsTabPlaceholder(content::WebContents* contents) const;
  bool ShouldShowTabInPageList(content::WebContents* contents) const;
  size_t GetPageCountForSpace(const std::string& space_id) const;
  bool ActiveSpaceHasTabs() const;
  void MoveTabToSpace(content::WebContents* contents,
                      const std::string& space_id);
  // Moves a dragged page selection as one transaction. Observers see the
  // complete result, so a tree subtree never briefly renders split across two
  // Spaces.
  void MoveTabsToSpace(base::span<content::WebContents* const> contents,
                       const std::string& space_id);

  // A Space can keep one page in the window's right-hand side view instead of
  // its page list. That page is left out of the list, the page count, j/k and
  // replacement selection, and comes back with the tab after a restart.
  bool IsSidePage(content::WebContents* contents) const;
  content::WebContents* GetSidePage(const std::string& space_id) const;
  // Makes `contents` its Space's side page and returns the Space's previous
  // side page to the list. `side_page` false returns `contents` to the list.
  void SetSidePage(content::WebContents* contents, bool side_page);
  // The page the main area shows for the active Space: the active tab, or the
  // Space's last main page while focus is in its side page.
  int GetMainTabIndex() const;

  // A pinned page remembers the address it was pinned at, after SigmaOS's
  // locked pages. `h` takes it back there and its tile shows when it has
  // wandered off.
  GURL GetPinnedHomeUrl(content::WebContents* contents) const;
  void SetPinnedHomeUrl(content::WebContents* contents, const GURL& url);
  bool IsPinnedPageAway(content::WebContents* contents) const;
  bool ReturnPinnedPageHome(content::WebContents* contents);
  // Arc's "Separate From Pinned Tab": the page a pin has wandered to moves
  // into the page list with its history, and the pin starts over, unloaded,
  // at its home address. False unless `contents` is a pinned page away from
  // home.
  bool SeparateFromPin(content::WebContents* contents);
  // Arc's "Close and Keep Pinned": the pinned page unloads back at its home
  // address and loads again when its tile is opened. `contents` must not be
  // the active tab. False for a page that isn't pinned or is already unloaded.
  // A page with unload handlers closes the usual way instead, so its "Leave
  // site?" prompt can keep it, and the pin returns once it has gone.
  bool UnloadPinnedPage(content::WebContents* contents);

  // The count mail and chat put in brackets near the start of a title:
  // "(3) Slack", "Inbox (12) - me@example.com - Gmail". 0 without one.
  static int GetUnreadCount(std::u16string_view title);

  // A name the user gave the page, shown in the page list in place of its
  // title. Null when the page shows its own title.
  static const std::u16string* GetCustomTitle(
      const content::WebContents* contents);
  // An empty title gives the page its own title back.
  void SetCustomTitle(content::WebContents* contents, std::u16string title);

  // Selects the previous/next page in the active space, skipping all tabs
  // belonging to other spaces. Navigation wraps at the ends.
  bool SelectAdjacentTab(bool next);

  // Selects the page that should replace the active page while
  // `closing_indices` are closed. Prefers the next visible page, then the
  // previous one, and only falls back to the Space's hidden New Tab canvas
  // when no visible page survives.
  bool SelectReplacementTabForClose(base::span<const int> closing_indices);

  // Selects the previous/next space in profile order. Navigation wraps at the
  // ends and restores the last selected page in the destination space.
  bool SelectAdjacentSpace(bool next);

  void MaybePopulateTabExtraData(
      int index,
      std::map<std::string, std::string>* extra_data);
  void MaybeRestoreTabSpace(
      content::WebContents* restored_contents,
      const std::map<std::string, std::string>& extra_data);
  // Leaves Space data out of the session file while still recording the
  // backups, as builds that lost it on every session rebuild did.
  static void SetSimulateSessionWithoutSpaceDataForTesting(bool simulate);

  void MaybePopulateWindowExtraData(
      std::map<std::string, std::string>* extra_data) const;
  void BeginWindowRestore(const std::map<std::string, std::string>& extra_data);
  void FinishWindowRestore();

  void AddObserver(Observer* observer);
  void RemoveObserver(Observer* observer);

 private:
  // TabStripModelObserver:
  void OnTabStripModelChanged(
      TabStripModel* tab_strip_model,
      const TabStripModelChange& change,
      const TabStripSelectionChange& selection) override;
  void OnTabChangedAt(tabs::TabInterface* tab,
                      TabChangeType change_type) override;
  void OnTabPinnedStateChanged(tabs::TabInterface* tab, int index) override;

  // WorkspaceService::Observer:
  void OnOriginSpacesChanged() override;

  const std::string& DefaultSpaceId() const;
  std::string EnsureSpaceForTab(content::WebContents* contents,
                                const std::string& preferred_space_id);
  int FindPreferredTabIndex(const std::string& space_id) const;
  void RememberActiveTab(content::WebContents* contents);
  void EnsureActiveTabInActiveSpace();
  void WriteTabSessionData(content::WebContents* contents);
  // A page at `url` that loads only when it is first shown.
  std::unique_ptr<content::WebContents> CreateUnloadedPage(
      const GURL& url) const;
  // Pins an unloaded page at `home` near `index`, in `space_id`, named
  // `name` when it isn't empty.
  void InsertUnloadedPin(int index,
                         const GURL& home,
                         const std::string& space_id,
                         const std::u16string& name);
  void WriteWindowSessionData();
  void NotifyChanged();

  raw_ptr<Profile> profile_ = nullptr;
  raw_ptr<TabStripModel> tab_strip_model_ = nullptr;
  raw_ptr<WorkspaceService> workspace_service_ = nullptr;
  const SessionID window_id_;
  std::string active_space_id_;
  std::optional<std::string> preview_space_id_;
  std::optional<std::string> restoring_active_space_id_;
  std::map<std::string, SessionID> last_active_tab_by_space_;
  base::ObserverList<Observer> observers_;
  base::ScopedObservation<WorkspaceService, WorkspaceService::Observer>
      workspace_observation_{this};
  base::WeakPtrFactory<OriginSpaceController> weak_factory_{this};
};

#endif  // BRAVE_BROWSER_UI_TABS_ORIGIN_SPACE_CONTROLLER_H_
