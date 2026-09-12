// Copyright (c) 2026 The Brave Authors. All rights reserved.
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this file,
// You can obtain one at https://mozilla.org/MPL/2.0/.

#include "brave/browser/ui/tabs/origin_media_monitor.h"

#include <algorithm>
#include <set>
#include <string_view>
#include <utility>

#include "base/check.h"
#include "base/functional/bind.h"
#include "base/strings/utf_string_conversions.h"
#include "chrome/browser/ui/tabs/tab_muted_utils.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "components/sessions/content/session_tab_helper.h"
#include "components/tabs/public/tab_interface.h"
#include "content/public/browser/media_session.h"
#include "content/public/browser/web_contents.h"
#include "services/media_session/public/cpp/media_metadata.h"
#include "services/media_session/public/cpp/media_position.h"

namespace {

using MediaSessionAction = media_session::mojom::MediaSessionAction;

bool HostMatches(std::string_view host, std::string_view domain) {
  if (host == domain) {
    return true;
  }
  return host.size() > domain.size() && host.ends_with(domain) &&
         host[host.size() - domain.size() - 1] == '.';
}

// Ranks a page for "what should the media widget show". A page that is playing
// out loud wins over one that is playing muted, which wins over one that is
// merely paused with a live session. Everything else is not worth showing.
int MediaPriority(const OriginMediaMonitor::MediaItem& item) {
  if (item.playing && item.audible) {
    return 3;
  }
  if (item.playing) {
    return 2;
  }
  return 1;
}

}  // namespace

OriginMediaMonitor::TabMedia::TabMedia(content::WebContents* contents,
                                       base::RepeatingClosure on_changed)
    : on_changed_(std::move(on_changed)) {
  content::MediaSession::Get(contents)->AddObserver(
      receiver_.BindNewPipeAndPassRemote());
}

OriginMediaMonitor::TabMedia::~TabMedia() = default;

void OriginMediaMonitor::TabMedia::MediaSessionInfoChanged(
    media_session::mojom::MediaSessionInfoPtr info) {
  on_changed_.Run();
}

void OriginMediaMonitor::TabMedia::MediaSessionMetadataChanged(
    const std::optional<media_session::MediaMetadata>& metadata) {
  on_changed_.Run();
}

void OriginMediaMonitor::TabMedia::MediaSessionActionsChanged(
    const std::vector<MediaSessionAction>& actions) {
  on_changed_.Run();
}

void OriginMediaMonitor::TabMedia::MediaSessionPositionChanged(
    const std::optional<media_session::MediaPosition>& position) {
  on_changed_.Run();
}

OriginMediaMonitor::OriginMediaMonitor(TabStripModel* tab_strip_model,
                                       OriginSpaceController* space_controller,
                                       WorkspaceService* workspace_service)
    : tab_strip_model_(tab_strip_model),
      space_controller_(space_controller),
      workspace_service_(workspace_service) {
  CHECK(tab_strip_model_);
  CHECK(space_controller_);
  CHECK(workspace_service_);
  tab_strip_model_->AddObserver(this);
  space_observation_.Observe(space_controller_);
  SyncTrackedTabs();
}

OriginMediaMonitor::~OriginMediaMonitor() = default;

// static
OriginMediaMonitor::MediaSource OriginMediaMonitor::ClassifySource(
    const GURL& url) {
  if (!url.SchemeIsHTTPOrHTTPS()) {
    return MediaSource::kOther;
  }
  const std::string_view host = url.host();
  if (HostMatches(host, "youtube.com") || HostMatches(host, "youtu.be") ||
      HostMatches(host, "youtube-nocookie.com")) {
    return MediaSource::kYouTube;
  }
  if (HostMatches(host, "spotify.com")) {
    return MediaSource::kSpotify;
  }
  return MediaSource::kOther;
}

bool OriginMediaMonitor::IsSpaceAudible(const std::string& space_id) const {
  for (int i = 0; i < tab_strip_model_->count(); ++i) {
    auto* contents = tab_strip_model_->GetWebContentsAt(i);
    if (contents->IsCurrentlyAudible() && !contents->IsAudioMuted() &&
        space_controller_->GetSpaceIdForTab(contents) == space_id) {
      return true;
    }
  }
  return false;
}

bool OriginMediaMonitor::IsSpaceMuted(const std::string& space_id) const {
  bool found = false;
  for (int i = 0; i < tab_strip_model_->count(); ++i) {
    auto* contents = tab_strip_model_->GetWebContentsAt(i);
    if (!contents->IsCurrentlyAudible() && !contents->WasEverAudible()) {
      continue;
    }
    if (space_controller_->GetSpaceIdForTab(contents) != space_id) {
      continue;
    }
    found = true;
    if (!contents->IsAudioMuted()) {
      return false;
    }
  }
  return found;
}

void OriginMediaMonitor::SetSpaceMuted(const std::string& space_id,
                                       bool muted) {
  for (int i = 0; i < tab_strip_model_->count(); ++i) {
    auto* contents = tab_strip_model_->GetWebContentsAt(i);
    if (space_controller_->GetSpaceIdForTab(contents) != space_id) {
      continue;
    }
    // Mute the whole Space, including pages that are only silent for now, so
    // a video that starts later doesn't escape the user's mute.
    SetTabAudioMuted(contents, muted, TabMutedReason::kAudioIndicator,
                     /*extension_id=*/std::string());
  }
  NotifyChanged();
}

void OriginMediaMonitor::ToggleSpaceMuted(const std::string& space_id) {
  SetSpaceMuted(space_id, !IsSpaceMuted(space_id));
}

std::optional<OriginMediaMonitor::MediaItem> OriginMediaMonitor::GetActiveMedia(
    MediaSource source) const {
  std::optional<MediaItem> best;
  int best_priority = 0;
  for (int i = 0; i < tab_strip_model_->count(); ++i) {
    auto item = BuildItem(tab_strip_model_->GetWebContentsAt(i));
    if (!item) {
      continue;
    }
    if (source != MediaSource::kAny && item->source != source) {
      continue;
    }
    const int priority = MediaPriority(*item);
    if (!best || priority > best_priority) {
      best_priority = priority;
      best = std::move(item);
    }
  }
  return best;
}

void OriginMediaMonitor::TogglePlayPause(SessionID tab_id) {
  auto* contents = FindContents(tab_id);
  if (!contents) {
    return;
  }
  auto* session = content::MediaSession::GetIfExists(contents);
  if (!session) {
    return;
  }
  auto info = session->GetMediaSessionInfoSync();
  const bool playing =
      info && info->playback_state ==
                  media_session::mojom::MediaPlaybackState::kPlaying;
  // Resume/Suspend route site handlers and also control players whose sites
  // have not registered Media Session handlers.
  if (playing) {
    session->Suspend(content::MediaSession::SuspendType::kUI);
  } else {
    session->Resume(content::MediaSession::SuspendType::kUI);
  }
}

content::WebContents* OriginMediaMonitor::GetMediaContents(
    SessionID tab_id) const {
  return FindContents(tab_id);
}

void OriginMediaMonitor::ToggleMuted(SessionID tab_id) {
  if (auto* contents = FindContents(tab_id)) {
    SetTabAudioMuted(contents, !contents->IsAudioMuted(),
                     TabMutedReason::kAudioIndicator,
                     /*extension_id=*/std::string());
    NotifyChanged();
  }
}

void OriginMediaMonitor::SkipToNextTrack(SessionID tab_id) {
  if (auto* contents = FindContents(tab_id)) {
    if (auto* session = content::MediaSession::GetIfExists(contents)) {
      session->DidReceiveAction(MediaSessionAction::kNextTrack);
    }
  }
}

void OriginMediaMonitor::SkipToPreviousTrack(SessionID tab_id) {
  if (auto* contents = FindContents(tab_id)) {
    if (auto* session = content::MediaSession::GetIfExists(contents)) {
      session->DidReceiveAction(MediaSessionAction::kPreviousTrack);
    }
  }
}

void OriginMediaMonitor::ActivateTab(SessionID tab_id) {
  auto* contents = FindContents(tab_id);
  if (!contents) {
    return;
  }
  // Selecting the Space restores its own last active page, so the page we came
  // for has to be selected afterwards, not before.
  space_controller_->SelectSpace(space_controller_->GetSpaceIdForTab(contents));
  const int index = tab_strip_model_->GetIndexOfWebContents(contents);
  if (index != TabStripModel::kNoTab) {
    tab_strip_model_->ActivateTabAt(index);
  }
}

void OriginMediaMonitor::AddObserver(Observer* observer) {
  observers_.AddObserver(observer);
}

void OriginMediaMonitor::RemoveObserver(Observer* observer) {
  observers_.RemoveObserver(observer);
}

void OriginMediaMonitor::OnTabStripModelChanged(
    TabStripModel* tab_strip_model,
    const TabStripModelChange& change,
    const TabStripSelectionChange& selection) {
  SyncTrackedTabs();
  NotifyChanged();
}

void OriginMediaMonitor::OnTabChangedAt(tabs::TabInterface* tab,
                                        TabChangeType change_type) {
  // Audio state changes arrive here as kAll, and so does the navigation that
  // turns an ordinary page into a media host.
  SyncTrackedTabs();
  NotifyChanged();
}

void OriginMediaMonitor::OnOriginSpaceControllerChanged() {
  NotifyChanged();
}

void OriginMediaMonitor::SyncTrackedTabs() {
  std::set<SessionID> live;
  for (int i = 0; i < tab_strip_model_->count(); ++i) {
    auto* contents = tab_strip_model_->GetWebContentsAt(i);
    const SessionID tab_id = sessions::SessionTabHelper::IdForTab(contents);
    if (!tab_id.is_valid()) {
      continue;
    }
    live.insert(tab_id);
    if (!tracked_.contains(tab_id) && ShouldTrackTab(contents)) {
      TrackTab(contents);
    }
  }
  std::erase_if(tracked_, [&live](const auto& entry) {
    return !live.contains(entry.first);
  });
}

void OriginMediaMonitor::TrackTab(content::WebContents* contents) {
  const SessionID tab_id = sessions::SessionTabHelper::IdForTab(contents);
  tracked_[tab_id] = std::make_unique<TabMedia>(
      contents, base::BindRepeating(&OriginMediaMonitor::NotifyChanged,
                                    weak_factory_.GetWeakPtr()));
}

bool OriginMediaMonitor::ShouldTrackTab(content::WebContents* contents) const {
  // Attaching a media-session observer to every page in the window would be
  // wasteful, so only pages that have made sound or that come from a host with
  // a widget get one.
  return contents->WasEverAudible() || contents->IsCurrentlyAudible() ||
         ClassifySource(contents->GetLastCommittedURL()) != MediaSource::kOther;
}

content::WebContents* OriginMediaMonitor::FindContents(SessionID tab_id) const {
  for (int i = 0; i < tab_strip_model_->count(); ++i) {
    auto* contents = tab_strip_model_->GetWebContentsAt(i);
    if (sessions::SessionTabHelper::IdForTab(contents) == tab_id) {
      return contents;
    }
  }
  return nullptr;
}

std::optional<OriginMediaMonitor::MediaItem> OriginMediaMonitor::BuildItem(
    content::WebContents* contents) const {
  const SessionID tab_id = sessions::SessionTabHelper::IdForTab(contents);
  if (!tracked_.contains(tab_id)) {
    return std::nullopt;
  }
  auto* session = content::MediaSession::GetIfExists(contents);
  if (!session) {
    return std::nullopt;
  }
  auto info = session->GetMediaSessionInfoSync();
  // An inactive session usually means the page has media code loaded but
  // nothing the user could act on, which is not worth a card. Muting a tab can
  // also drop it out of audio focus, though, and the card is exactly what the
  // user needs to unmute from -- so a page that has actually played keeps its
  // card regardless.
  const bool inactive =
      !info ||
      info->state ==
          media_session::mojom::MediaSessionInfo::SessionState::kInactive;
  if (inactive && !contents->WasEverAudible()) {
    return std::nullopt;
  }
  if (!info) {
    return std::nullopt;
  }

  const auto& metadata = session->GetMediaSessionMetadata();
  const auto actions = session->GetMediaSessionActionsSync();
  const auto has_action = [&actions](MediaSessionAction action) {
    return std::ranges::contains(actions, action);
  };

  MediaItem item;
  item.tab_id = tab_id;
  item.space_id = space_controller_->GetSpaceIdForTab(contents);
  if (const auto* space = workspace_service_->GetOriginSpace(item.space_id)) {
    item.space_name = base::UTF8ToUTF16(space->name);
  }
  item.source = ClassifySource(contents->GetLastCommittedURL());
  // Media sessions frequently omit a title; the tab title is the same string
  // the user already sees in the page list, so it is a safe fallback.
  item.title = metadata.title.empty() ? contents->GetTitle() : metadata.title;
  // Prefer the artist when the page supplies one; a video site that only fills
  // in source_title still gets a useful second line.
  item.artist =
      metadata.artist.empty() ? metadata.source_title : metadata.artist;
  item.playing = info->playback_state ==
                 media_session::mojom::MediaPlaybackState::kPlaying;
  item.audible = contents->IsCurrentlyAudible() && !contents->IsAudioMuted();
  item.muted = contents->IsAudioMuted();
  const auto* tab = tabs::TabInterface::GetFromContents(contents);
  const auto* active = tab_strip_model_->GetActiveTab();
  // Capturing a background page keeps its WebContents visible to the renderer.
  // Use the selected page and its split partner to describe what the user sees.
  item.page_visible =
      tab && active &&
      (tab == active ||
       (tab->GetSplit() && tab->GetSplit() == active->GetSplit()));
  item.can_play = has_action(MediaSessionAction::kPlay);
  item.can_pause = has_action(MediaSessionAction::kPause);
  item.can_skip_to_next = has_action(MediaSessionAction::kNextTrack);
  item.can_skip_to_previous = has_action(MediaSessionAction::kPreviousTrack);
  if (auto position = session->GetMediaSessionPosition()) {
    item.position = position->GetPosition();
    item.duration = position->duration();
  }
  return item;
}

void OriginMediaMonitor::NotifyChanged() {
  observers_.Notify(&Observer::OnOriginMediaChanged);
}
