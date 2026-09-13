// Copyright (c) 2026 The Brave Authors. All rights reserved.
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this file,
// You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef BRAVE_BROWSER_UI_TABS_ORIGIN_MEDIA_MONITOR_H_
#define BRAVE_BROWSER_UI_TABS_ORIGIN_MEDIA_MONITOR_H_

#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "base/functional/callback.h"
#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "base/observer_list.h"
#include "base/observer_list_types.h"
#include "base/scoped_observation.h"
#include "base/time/time.h"
#include "brave/browser/ui/tabs/origin_space_controller.h"
#include "chrome/browser/ui/tabs/tab_strip_model_observer.h"
#include "components/sessions/core/session_id.h"
#include "mojo/public/cpp/bindings/receiver.h"
#include "services/media_session/public/mojom/media_session.mojom.h"
#include "url/gurl.h"

class TabStripModel;

namespace content {
class WebContents;
}

// Tracks audio and media-session state for every page in one browser window,
// grouped by the Space each page belongs to.
//
// Two consumers depend on this: the rail's per-Space sound indicator, which
// only needs the aggregate audible/muted state, and the media widgets, which
// need the metadata of whatever is playing regardless of which Space owns it.
// Both read the same state so the rail badge and the widget never disagree.
class OriginMediaMonitor : public TabStripModelObserver,
                           public OriginSpaceController::Observer {
 public:
  // Media hosts the widgets recognize. Anything else is still tracked for the
  // Space sound indicator but has no dedicated widget. kAny is a query
  // wildcard only; ClassifySource() never returns it.
  enum class MediaSource {
    kAny,
    kOther,
    kYouTube,
    kSpotify,
  };

  // A single page that owns a media session. `playing` reflects the media
  // session's playback state, which stays meaningful while paused; `audible`
  // reflects whether sound is actually reaching the speakers right now.
  struct MediaItem {
    SessionID tab_id = SessionID::InvalidValue();
    std::string space_id;
    std::u16string space_name;
    std::u16string title;
    std::u16string artist;
    MediaSource source = MediaSource::kOther;
    bool playing = false;
    bool audible = false;
    bool muted = false;
    bool page_visible = false;
    bool can_play = false;
    bool can_pause = false;
    bool can_skip_to_next = false;
    bool can_skip_to_previous = false;
    std::optional<base::TimeDelta> position;
    std::optional<base::TimeDelta> duration;
  };

  class Observer : public base::CheckedObserver {
   public:
    virtual void OnOriginMediaChanged() = 0;
  };

  OriginMediaMonitor(TabStripModel* tab_strip_model,
                     OriginSpaceController* space_controller,
                     WorkspaceService* workspace_service);
  OriginMediaMonitor(const OriginMediaMonitor&) = delete;
  OriginMediaMonitor& operator=(const OriginMediaMonitor&) = delete;
  ~OriginMediaMonitor() override;

  // A Space is audible only while an unmuted page is producing sound.
  // Paused, silent and muted pages never keep the rail badge visible.
  bool IsSpaceAudible(const std::string& space_id) const;
  bool IsSpaceMuted(const std::string& space_id) const;
  void SetSpaceMuted(const std::string& space_id, bool muted);
  void ToggleSpaceMuted(const std::string& space_id);

  // Returns the most relevant media item for `source`, preferring one that is
  // currently playing over one that is merely paused. Media deliberately
  // outlives a Space switch, so the returned item may belong to a Space other
  // than the selected one.
  std::optional<MediaItem> GetActiveMedia(MediaSource source) const;

  // Controls addressed by tab so a widget keeps working after the tab strip
  // reorders underneath it. All no-op when the tab has gone away.
  void TogglePlayPause(SessionID tab_id);
  void ToggleMuted(SessionID tab_id);
  void SkipToNextTrack(SessionID tab_id);
  void SkipToPreviousTrack(SessionID tab_id);
  // Selects the page, switching to its Space first when it lives elsewhere.
  void ActivateTab(SessionID tab_id);
  content::WebContents* GetMediaContents(SessionID tab_id) const;

  void AddObserver(Observer* observer);
  void RemoveObserver(Observer* observer);

  static MediaSource ClassifySource(const GURL& url);

 private:
  // Subscribes to one page's media session purely for change notifications;
  // the state itself is read back synchronously when a caller asks, which
  // keeps a single source of truth in the session rather than a shadow copy
  // here.
  class TabMedia : public media_session::mojom::MediaSessionObserver {
   public:
    TabMedia(content::WebContents* contents, base::RepeatingClosure on_changed);
    TabMedia(const TabMedia&) = delete;
    TabMedia& operator=(const TabMedia&) = delete;
    ~TabMedia() override;

    // media_session::mojom::MediaSessionObserver:
    void MediaSessionInfoChanged(
        media_session::mojom::MediaSessionInfoPtr info) override;
    void MediaSessionMetadataChanged(
        const std::optional<media_session::MediaMetadata>& metadata) override;
    void MediaSessionActionsChanged(
        const std::vector<media_session::mojom::MediaSessionAction>& actions)
        override;
    void MediaSessionImagesChanged(
        const base::flat_map<media_session::mojom::MediaSessionImageType,
                             std::vector<media_session::MediaImage>>& images)
        override {}
    void MediaSessionPositionChanged(
        const std::optional<media_session::MediaPosition>& position) override;

   private:
    base::RepeatingClosure on_changed_;
    mojo::Receiver<media_session::mojom::MediaSessionObserver> receiver_{this};
  };

  // TabStripModelObserver:
  void OnTabStripModelChanged(
      TabStripModel* tab_strip_model,
      const TabStripModelChange& change,
      const TabStripSelectionChange& selection) override;
  void OnTabChangedAt(tabs::TabInterface* tab,
                      TabChangeType change_type) override;

  // OriginSpaceController::Observer:
  void OnOriginSpaceControllerChanged() override;

  // Attaches a media-session observer to any page that now deserves one, and
  // drops observers for pages that have left the window.
  void SyncTrackedTabs();
  void TrackTab(content::WebContents* contents);
  bool ShouldTrackTab(content::WebContents* contents) const;
  content::WebContents* FindContents(SessionID tab_id) const;
  std::optional<MediaItem> BuildItem(content::WebContents* contents) const;
  void NotifyChanged();

  raw_ptr<TabStripModel> tab_strip_model_ = nullptr;
  raw_ptr<OriginSpaceController> space_controller_ = nullptr;
  raw_ptr<WorkspaceService> workspace_service_ = nullptr;
  // Keyed by session id rather than WebContents* so a page torn down without
  // a tab-strip notification can never leave a dangling key behind.
  std::map<SessionID, std::unique_ptr<TabMedia>> tracked_;
  base::ObserverList<Observer> observers_;
  base::ScopedObservation<OriginSpaceController,
                          OriginSpaceController::Observer>
      space_observation_{this};
  base::WeakPtrFactory<OriginMediaMonitor> weak_factory_{this};
};

#endif  // BRAVE_BROWSER_UI_TABS_ORIGIN_MEDIA_MONITOR_H_
