// Copyright (c) 2026 The Brave Authors. All rights reserved.
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this file,
// You can obtain one at https://mozilla.org/MPL/2.0/.

#include "brave/browser/ui/tabs/origin_space_controller.h"

#include <algorithm>
#include <optional>
#include <set>
#include <string_view>
#include <utility>
#include <vector>

#include "base/check.h"
#include "base/containers/map_util.h"
#include "base/functional/bind.h"
#include "base/memory/weak_ptr.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/string_util.h"
#include "base/strings/utf_string_conversions.h"
#include "base/task/sequenced_task_runner.h"
#include "brave/browser/sessions/brave_session_keys.h"
#include "brave/browser/workspaces/pref_names.h"
#include "brave/browser/workspaces/workspace_service_factory.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/sessions/session_service.h"
#include "chrome/browser/sessions/session_service_factory.h"
#include "chrome/browser/ui/tab_ui_helper.h"
#include "chrome/browser/ui/tabs/tab_enums.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/browser/ui/webui/ntp/new_tab_ui.h"
#include "components/prefs/pref_service.h"
#include "components/prefs/scoped_user_pref_update.h"
#include "components/sessions/content/session_tab_helper.h"
#include "components/tabs/public/tab_interface.h"
#include "content/public/browser/navigation_controller.h"
#include "content/public/browser/navigation_entry.h"
#include "content/public/browser/restore_type.h"
#include "content/public/browser/web_contents.h"
#include "content/public/browser/web_contents_observer.h"
#include "content/public/browser/web_contents_user_data.h"
#include "content/public/common/referrer.h"
#include "services/network/public/cpp/shared_url_loader_factory.h"
#include "ui/base/page_transition_types.h"
#include "url/origin.h"
#include "url/url_constants.h"

namespace {

constexpr size_t kMaxSessionSpaceBackups = 4096;

// The icon `contents` showed at `url`, so a pin that sleeps there keeps its
// face, as a discarded tab does. Any page from the same origin will do when
// the exact address has left the history.
std::optional<content::FaviconStatus> FindFavicon(
    content::WebContents* contents,
    const GURL& url) {
  content::NavigationController& history = contents->GetController();
  std::optional<content::FaviconStatus> same_origin;
  for (int i = history.GetEntryCount() - 1; i >= 0; --i) {
    content::NavigationEntry* entry = history.GetEntryAtIndex(i);
    if (!entry->GetFavicon().valid) {
      continue;
    }
    if (entry->GetURL() == url) {
      return entry->GetFavicon();
    }
    if (!same_origin &&
        url::Origin::Create(entry->GetURL()).IsSameOriginWith(url)) {
      same_origin = entry->GetFavicon();
    }
  }
  return same_origin;
}

// Holds an unloading pin's place while its page closes the usual way. If the
// page's "Leave site?" prompt keeps it, nothing happens; once it has gone,
// `restore_pin` puts an unloaded pin back.
class PendingKeptPin : public content::WebContentsUserData<PendingKeptPin>,
                       public content::WebContentsObserver {
 public:
  ~PendingKeptPin() override = default;

  // content::WebContentsObserver:
  void BeforeUnloadFired(bool proceed) override {
    if (!proceed) {
      // Destroys `this`.
      web_contents()->RemoveUserData(UserDataKey());
    }
  }

  void WebContentsDestroyed() override {
    // Not from inside the tab strip's close.
    base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
        FROM_HERE, std::move(restore_pin_));
  }

 private:
  friend class content::WebContentsUserData<PendingKeptPin>;

  PendingKeptPin(content::WebContents* contents, base::OnceClosure restore_pin)
      : content::WebContentsUserData<PendingKeptPin>(*contents),
        content::WebContentsObserver(contents),
        restore_pin_(std::move(restore_pin)) {}

  base::OnceClosure restore_pin_;

  WEB_CONTENTS_USER_DATA_KEY_DECL();
};

WEB_CONTENTS_USER_DATA_KEY_IMPL(PendingKeptPin);

std::string SessionIdKey(SessionID id) {
  return base::NumberToString(id.id());
}

// The SessionID an entry had in the session file being restored, if the
// restore passed it along. See kBraveOriginRestoredTabIdKey.
std::optional<SessionID> RestoredSessionId(
    const std::map<std::string, std::string>& extra_data,
    const char* key) {
  const std::string* value = base::FindOrNull(extra_data, key);
  int id = 0;
  if (!value || !base::StringToInt(*value, &id)) {
    return std::nullopt;
  }
  const SessionID session_id = SessionID::FromSerializedValue(id);
  return session_id.is_valid() ? std::optional<SessionID>(session_id)
                               : std::nullopt;
}

bool g_simulate_session_without_space_data = false;

void SetSessionSpaceBackup(PrefService* prefs,
                           const char* pref_name,
                           SessionID session_id,
                           const std::string& space_id) {
  if (!prefs || !session_id.is_valid()) {
    return;
  }
  const std::string key = SessionIdKey(session_id);
  ScopedDictPrefUpdate backups(*prefs, pref_name);
  if (backups->size() >= kMaxSessionSpaceBackups && !backups->Find(key)) {
    const std::string pruned_key = backups->begin()->first;
    backups->Remove(pruned_key);
  }
  backups->Set(key, space_id);
}

std::optional<std::string> GetSessionSpaceBackup(PrefService* prefs,
                                                 const char* pref_name,
                                                 SessionID session_id) {
  if (!prefs || !session_id.is_valid()) {
    return std::nullopt;
  }
  const std::string* space_id =
      prefs->GetDict(pref_name).FindString(SessionIdKey(session_id));
  return space_id ? std::optional<std::string>(*space_id) : std::nullopt;
}

void SetTabSessionSpaceBackup(PrefService* prefs,
                              content::WebContents* contents,
                              const std::string& space_id) {
  const auto* session_helper =
      sessions::SessionTabHelper::FromWebContents(contents);
  if (!session_helper) {
    return;
  }
  SetSessionSpaceBackup(prefs, kOriginTabSessionSpacesPref,
                        session_helper->session_id(), space_id);
}

std::string GetBackedUpTabSpaceOrFallback(
    PrefService* prefs,
    const WorkspaceService* workspace_service,
    content::WebContents* contents,
    const std::string& fallback_space_id) {
  const auto* session_helper =
      sessions::SessionTabHelper::FromWebContents(contents);
  if (!session_helper) {
    return fallback_space_id;
  }
  std::optional<std::string> backup = GetSessionSpaceBackup(
      prefs, kOriginTabSessionSpacesPref, session_helper->session_id());
  return backup && workspace_service->GetOriginSpace(*backup)
             ? std::move(*backup)
             : fallback_space_id;
}

class OriginSpaceTabData
    : public content::WebContentsUserData<OriginSpaceTabData> {
 public:
  ~OriginSpaceTabData() override = default;

  const std::string& space_id() const { return space_id_; }
  void set_space_id(std::string space_id) { space_id_ = std::move(space_id); }

  bool side_page() const { return side_page_; }
  void set_side_page(bool side_page) { side_page_ = side_page; }

  const GURL& pinned_home_url() const { return pinned_home_url_; }
  void set_pinned_home_url(GURL url) { pinned_home_url_ = std::move(url); }

  const std::u16string& custom_title() const { return custom_title_; }
  void set_custom_title(std::u16string title) {
    custom_title_ = std::move(title);
  }

 private:
  friend class content::WebContentsUserData<OriginSpaceTabData>;

  OriginSpaceTabData(content::WebContents* contents, std::string space_id)
      : content::WebContentsUserData<OriginSpaceTabData>(*contents),
        space_id_(std::move(space_id)) {}

  std::string space_id_;
  bool side_page_ = false;
  GURL pinned_home_url_;
  std::u16string custom_title_;
  WEB_CONTENTS_USER_DATA_KEY_DECL();
};

WEB_CONTENTS_USER_DATA_KEY_IMPL(OriginSpaceTabData);

}  // namespace

OriginSpaceController::OriginSpaceController(Profile* profile,
                                             TabStripModel* tab_strip_model,
                                             SessionID window_id)
    : profile_(profile),
      tab_strip_model_(tab_strip_model),
      workspace_service_(WorkspaceServiceFactory::GetForProfile(profile)),
      window_id_(window_id),
      active_space_id_(DefaultSpaceId()) {
  CHECK(profile_);
  CHECK(tab_strip_model_);
  CHECK(workspace_service_);
  workspace_observation_.Observe(workspace_service_);
  tab_strip_model_->AddObserver(this);

  for (int index = 0; index < tab_strip_model_->count(); ++index) {
    content::WebContents* contents =
        tab_strip_model_->GetWebContentsAt(index);
    EnsureSpaceForTab(contents,
                      GetBackedUpTabSpaceOrFallback(
                          profile_->GetPrefs(), workspace_service_, contents,
                          active_space_id_));
  }
}

OriginSpaceController::~OriginSpaceController() {
  tab_strip_model_->RemoveObserver(this);
}

bool OriginSpaceController::SelectSpace(const std::string& space_id) {
  if (!workspace_service_->GetOriginSpace(space_id)) {
    return false;
  }
  if (active_space_id_ == space_id) {
    restoring_active_space_id_.reset();
    if (preview_space_id_) {
      preview_space_id_.reset();
      NotifyChanged();
    }
    return true;
  }

  restoring_active_space_id_.reset();
  active_space_id_ = space_id;
  preview_space_id_.reset();
  WriteWindowSessionData();
  const int target_index = FindPreferredTabIndex(space_id);
  if (target_index != TabStripModel::kNoTab) {
    tab_strip_model_->ActivateTabAt(target_index);
  }
  NotifyChanged();
  return true;
}

bool OriginSpaceController::SelectSpaceAtIndex(size_t index) {
  const auto& spaces = workspace_service_->GetOriginSpaces();
  return index < spaces.size() && SelectSpace(spaces[index].id);
}

std::string OriginSpaceController::GetSpaceIdForTab(
    content::WebContents* contents) const {
  if (const auto* data = OriginSpaceTabData::FromWebContents(contents)) {
    return data->space_id();
  }
  return DefaultSpaceId();
}

bool OriginSpaceController::IsTabInActiveSpace(
    content::WebContents* contents) const {
  return GetSpaceIdForTab(contents) == active_space_id_;
}

void OriginSpaceController::SetPreviewSpace(
    std::optional<std::string> space_id) {
  if (space_id && (*space_id == active_space_id_ ||
                   !workspace_service_->GetOriginSpace(*space_id))) {
    space_id.reset();
  }
  if (preview_space_id_ == space_id) {
    return;
  }
  preview_space_id_ = std::move(space_id);
  NotifyChanged();
}

bool OriginSpaceController::IsTabPlaceholder(
    content::WebContents* contents) const {
  if (!contents) {
    return false;
  }

  const GURL& url = contents->GetVisibleURL();
  return url.is_empty() || NewTabUI::IsNewTab(url) ||
         url == GURL(url::kAboutBlankURL);
}

bool OriginSpaceController::ShouldShowTabInPageList(
    content::WebContents* contents) const {
  const std::string& listed_space_id =
      preview_space_id_ ? *preview_space_id_ : active_space_id_;
  return GetSpaceIdForTab(contents) == listed_space_id &&
         !IsTabPlaceholder(contents) && !IsSidePage(contents);
}

size_t OriginSpaceController::GetPageCountForSpace(
    const std::string& space_id) const {
  size_t page_count = 0;
  for (int index = 0; index < tab_strip_model_->count(); ++index) {
    auto* contents = tab_strip_model_->GetWebContentsAt(index);
    // Pinned pages are presented in their own section and do not contribute
    // to the Space's Pages count. This keeps the header count identical to the
    // Pages section count shown in the sidebar.
    if (GetSpaceIdForTab(contents) == space_id &&
        !tab_strip_model_->IsTabPinned(index) && !IsTabPlaceholder(contents) &&
        !IsSidePage(contents)) {
      ++page_count;
    }
  }
  return page_count;
}

bool OriginSpaceController::ActiveSpaceHasTabs() const {
  for (int index = 0; index < tab_strip_model_->count(); ++index) {
    auto* contents = tab_strip_model_->GetWebContentsAt(index);
    // The side page can't be the main area's page, so alone it leaves the
    // Space without one.
    if (IsTabInActiveSpace(contents) && !IsSidePage(contents)) {
      return true;
    }
  }
  return false;
}

void OriginSpaceController::MoveTabToSpace(content::WebContents* contents,
                                           const std::string& space_id) {
  MoveTabsToSpace(base::span_from_ref(contents), space_id);
}

void OriginSpaceController::MoveTabsToSpace(
    base::span<content::WebContents* const> contents,
    const std::string& space_id) {
  if (!workspace_service_->GetOriginSpace(space_id)) {
    return;
  }

  content::WebContents* active_contents =
      tab_strip_model_->GetActiveWebContents();
  bool moved_active_out_of_space = false;
  bool changed = false;
  for (content::WebContents* tab_contents : contents) {
    if (!tab_contents ||
        tab_strip_model_->GetIndexOfWebContents(tab_contents) ==
            TabStripModel::kNoTab ||
        GetSpaceIdForTab(tab_contents) == space_id) {
      continue;
    }

    moved_active_out_of_space |=
        tab_contents == active_contents && space_id != active_space_id_;
    auto* data = OriginSpaceTabData::FromWebContents(tab_contents);
    if (!data) {
      OriginSpaceTabData::CreateForWebContents(tab_contents, space_id);
    } else {
      data->set_space_id(space_id);
    }
    WriteTabSessionData(tab_contents);
    changed = true;
  }

  if (!changed) {
    return;
  }
  if (moved_active_out_of_space) {
    EnsureActiveTabInActiveSpace();
  } else {
    NotifyChanged();
  }
}

bool OriginSpaceController::IsSidePage(content::WebContents* contents) const {
  const auto* data =
      contents ? OriginSpaceTabData::FromWebContents(contents) : nullptr;
  return data && data->side_page();
}

content::WebContents* OriginSpaceController::GetSidePage(
    const std::string& space_id) const {
  for (int index = 0; index < tab_strip_model_->count(); ++index) {
    auto* contents = tab_strip_model_->GetWebContentsAt(index);
    if (IsSidePage(contents) && GetSpaceIdForTab(contents) == space_id) {
      return contents;
    }
  }
  return nullptr;
}

void OriginSpaceController::SetSidePage(content::WebContents* contents,
                                        bool side_page) {
  if (!contents || tab_strip_model_->GetIndexOfWebContents(contents) ==
                       TabStripModel::kNoTab) {
    return;
  }
  const std::string space_id = EnsureSpaceForTab(contents, active_space_id_);
  if (side_page) {
    // One side page per Space: the one it replaces goes back to the list.
    for (int index = 0; index < tab_strip_model_->count(); ++index) {
      auto* other = tab_strip_model_->GetWebContentsAt(index);
      if (other != contents && IsSidePage(other) &&
          GetSpaceIdForTab(other) == space_id) {
        OriginSpaceTabData::FromWebContents(other)->set_side_page(false);
        WriteTabSessionData(other);
      }
    }
  }
  auto* data = OriginSpaceTabData::FromWebContents(contents);
  CHECK(data);
  if (data->side_page() == side_page) {
    return;
  }
  data->set_side_page(side_page);
  WriteTabSessionData(contents);
  NotifyChanged();
}

GURL OriginSpaceController::GetPinnedHomeUrl(
    content::WebContents* contents) const {
  const auto* data =
      contents ? OriginSpaceTabData::FromWebContents(contents) : nullptr;
  return data ? data->pinned_home_url() : GURL();
}

void OriginSpaceController::SetPinnedHomeUrl(content::WebContents* contents,
                                             const GURL& url) {
  const int index = tab_strip_model_->GetIndexOfWebContents(contents);
  if (index == TabStripModel::kNoTab) {
    return;
  }
  EnsureSpaceForTab(contents, active_space_id_);
  auto* data = OriginSpaceTabData::FromWebContents(contents);
  // Only pinned pages keep an address, and only a real one.
  GURL home = tab_strip_model_->IsTabPinned(index) && url.is_valid() &&
                      !IsTabPlaceholder(contents)
                  ? url
                  : GURL();
  if (data->pinned_home_url() == home) {
    return;
  }
  data->set_pinned_home_url(std::move(home));
  WriteTabSessionData(contents);
  // The tile's dot is painted from this state, so the list repaints.
  NotifyChanged();
}

bool OriginSpaceController::IsPinnedPageAway(
    content::WebContents* contents) const {
  const GURL home = GetPinnedHomeUrl(contents);
  return home.is_valid() && contents->GetLastCommittedURL().GetWithoutRef() !=
                                home.GetWithoutRef();
}

bool OriginSpaceController::ReturnPinnedPageHome(
    content::WebContents* contents) {
  if (!contents || !IsPinnedPageAway(contents)) {
    return false;
  }
  contents->GetController().LoadURL(
      GetPinnedHomeUrl(contents), content::Referrer(),
      ui::PAGE_TRANSITION_AUTO_BOOKMARK, std::string());
  return true;
}

bool OriginSpaceController::SeparateFromPin(content::WebContents* contents) {
  const int index = tab_strip_model_->GetIndexOfWebContents(contents);
  if (index == TabStripModel::kNoTab || !tab_strip_model_->IsTabPinned(index) ||
      !IsPinnedPageAway(contents)) {
    return false;
  }
  const GURL home = GetPinnedHomeUrl(contents);
  const std::string space_id = GetSpaceIdForTab(contents);
  const std::u16string* custom_title = GetCustomTitle(contents);
  const std::u16string name = custom_title ? *custom_title : std::u16string();

  // Unpinning moves the page into the list and forgets its home; a name given
  // to the pin stays with the pin.
  tab_strip_model_->SetTabPinned(index, false);
  SetCustomTitle(contents, std::u16string());
  InsertUnloadedPin(index, home, space_id, name, FindFavicon(contents, home));
  return true;
}

bool OriginSpaceController::UnloadPinnedPage(content::WebContents* contents) {
  const int index = tab_strip_model_->GetIndexOfWebContents(contents);
  if (index == TabStripModel::kNoTab || !tab_strip_model_->IsTabPinned(index) ||
      index == tab_strip_model_->active_index() || contents->WasDiscarded() ||
      IsSidePage(contents)) {
    return false;
  }
  GURL home = GetPinnedHomeUrl(contents);
  if (!home.is_valid()) {
    home = contents->GetLastCommittedURL();
  }
  if (!home.is_valid()) {
    return false;
  }
  if (contents->NeedToFireBeforeUnloadOrUnloadEvents()) {
    // Swapping it out would skip the page's unload handlers and any prompt
    // to keep unsaved work. Close it as d does; it isn't kept for z, since
    // the pin comes back.
    const std::u16string* custom_title = GetCustomTitle(contents);
    PendingKeptPin::CreateForWebContents(
        contents,
        base::BindOnce(&OriginSpaceController::InsertUnloadedPin,
                       weak_factory_.GetWeakPtr(), index, home,
                       GetSpaceIdForTab(contents),
                       custom_title ? *custom_title : std::u16string(),
                       FindFavicon(contents, home)));
    tab_strip_model_->CloseWebContentsAt(index, TabCloseTypes::CLOSE_NONE);
    return true;
  }
  // Swapping in unloaded contents keeps the tile, its place and what Origin
  // knows about the page (see the kReplaced handling).
  tab_strip_model_->DiscardWebContents(
      contents, CreateUnloadedPage(home, FindFavicon(contents, home)));
  return true;
}

// static
int OriginSpaceController::GetUnreadCount(std::u16string_view title) {
  const size_t open = title.find(u'(');
  if (open == std::u16string_view::npos || open > 32) {
    return 0;
  }
  const size_t close = title.find(u')', open);
  if (close == std::u16string_view::npos || close == open + 1 ||
      close > open + 6) {
    return 0;
  }
  int count = 0;
  if (!base::StringToInt(title.substr(open + 1, close - open - 1), &count)) {
    return 0;
  }
  return std::max(count, 0);
}

void OriginSpaceController::InsertUnloadedPin(
    int index,
    const GURL& home,
    const std::string& space_id,
    const std::u16string& name,
    std::optional<content::FaviconStatus> favicon) {
  if (tab_strip_model_->closing_all()) {
    return;
  }
  index = std::clamp(index, 0, tab_strip_model_->IndexOfFirstNonPinnedTab());
  tab_strip_model_->InsertWebContentsAt(
      index, CreateUnloadedPage(home, favicon), AddTabTypes::ADD_PINNED);
  content::WebContents* pin = tab_strip_model_->GetWebContentsAt(index);
  // Inserting records the tab without its history, and a restore drops tabs
  // without history. Record it as duplicating a tab does.
  if (SessionService* session_service =
          SessionServiceFactory::GetForProfileIfExisting(profile_)) {
    session_service->TabRestored(pin, /*pinned=*/true);
  }
  MoveTabToSpace(pin, space_id);
  SetPinnedHomeUrl(pin, home);
  SetCustomTitle(pin, name);
}

std::unique_ptr<content::WebContents> OriginSpaceController::CreateUnloadedPage(
    const GURL& url,
    const std::optional<content::FaviconStatus>& favicon) const {
  content::WebContents::CreateParams params(profile_);
  params.initially_hidden = true;
  params.desired_renderer_state =
      content::WebContents::CreateParams::kNoRendererProcess;
  std::unique_ptr<content::WebContents> page =
      content::WebContents::Create(params);
  std::vector<std::unique_ptr<content::NavigationEntry>> entries;
  entries.push_back(content::NavigationController::CreateNavigationEntry(
      url, content::Referrer(), /*initiator_origin=*/std::nullopt,
      /*initiator_base_url=*/std::nullopt, ui::PAGE_TRANSITION_AUTO_BOOKMARK,
      /*is_renderer_initiated=*/false, std::string(), profile_,
      /*blob_url_loader_factory=*/nullptr));
  if (favicon) {
    entries.back()->GetFavicon() = *favicon;
  }
  page->GetController().Restore(0, content::RestoreType::kRestored, &entries);
  // Loads when it is next shown, as a discarded tab does.
  page->SetWasDiscarded(true);
  return page;
}

// static
const std::u16string* OriginSpaceController::GetCustomTitle(
    const content::WebContents* contents) {
  const auto* data =
      contents ? OriginSpaceTabData::FromWebContents(contents) : nullptr;
  return data && !data->custom_title().empty() ? &data->custom_title()
                                               : nullptr;
}

void OriginSpaceController::SetCustomTitle(content::WebContents* contents,
                                           std::u16string title) {
  const int index = tab_strip_model_->GetIndexOfWebContents(contents);
  if (index == TabStripModel::kNoTab) {
    return;
  }
  EnsureSpaceForTab(contents, active_space_id_);
  auto* data = OriginSpaceTabData::FromWebContents(contents);
  title = std::u16string(base::TrimWhitespace(title, base::TRIM_ALL));
  // Typing the page's own title back is the same as clearing the name.
  if (title == contents->GetTitle()) {
    title.clear();
  }
  if (data->custom_title() == title) {
    return;
  }
  data->set_custom_title(std::move(title));
  WriteTabSessionData(contents);
  // Tab rows rebuild their data when the tab's UI says its title changed;
  // the name is applied there (see chromium_src tab_data.cc).
  if (auto* tab_ui_helper =
          TabUIHelper::From(tab_strip_model_->GetTabAtIndex(index))) {
    tab_ui_helper->TitleWasSet(nullptr);
  }
}

int OriginSpaceController::GetMainTabIndex() const {
  const int active = tab_strip_model_->active_index();
  if (active != TabStripModel::kNoTab &&
      !IsSidePage(tab_strip_model_->GetWebContentsAt(active))) {
    return active;
  }
  return FindPreferredTabIndex(active_space_id_);
}

bool OriginSpaceController::SelectAdjacentTab(bool next) {
  std::vector<int> indices;
  for (int index = 0; index < tab_strip_model_->count(); ++index) {
    auto* contents = tab_strip_model_->GetWebContentsAt(index);
    if (IsTabInActiveSpace(contents) && !IsSidePage(contents)) {
      indices.push_back(index);
    }
  }
  if (indices.empty()) {
    return false;
  }

  // From the side page, j/k continue from the page beside it.
  const int active = GetMainTabIndex();
  auto current = std::ranges::find(indices, active);
  size_t target = 0;
  if (current != indices.end()) {
    const size_t offset = static_cast<size_t>(current - indices.begin());
    target = next ? (offset + 1) % indices.size()
                  : (offset + indices.size() - 1) % indices.size();
  } else if (!next) {
    target = indices.size() - 1;
  }
  tab_strip_model_->ActivateTabAt(indices[target]);
  return true;
}

bool OriginSpaceController::SelectReplacementTabForClose(
    base::span<const int> closing_indices) {
  const int active_index = tab_strip_model_->active_index();
  if (active_index == TabStripModel::kNoTab) {
    return false;
  }

  const auto is_closing = [&closing_indices](int index) {
    return std::ranges::find(closing_indices, index) != closing_indices.end();
  };

  // Closing the side page leaves the main area on the page it already shows.
  if (IsSidePage(tab_strip_model_->GetWebContentsAt(active_index))) {
    const int main_index = FindPreferredTabIndex(active_space_id_);
    if (main_index != TabStripModel::kNoTab && !is_closing(main_index)) {
      tab_strip_model_->ActivateTabAt(main_index);
      return true;
    }
  }
  const auto find_replacement = [&](bool visible_page_only) {
    const auto is_candidate = [&](int index) {
      if (is_closing(index)) {
        return false;
      }
      auto* contents = tab_strip_model_->GetWebContentsAt(index);
      if (IsSidePage(contents)) {
        return false;
      }
      // Not ShouldShowTabInPageList(): that follows a hovered Space, and
      // closing a page must not switch Spaces.
      return IsTabInActiveSpace(contents) &&
             (!visible_page_only || !IsTabPlaceholder(contents));
    };

    for (int index = active_index + 1; index < tab_strip_model_->count();
         ++index) {
      if (is_candidate(index)) {
        return index;
      }
    }
    for (int index = active_index - 1; index >= 0; --index) {
      if (is_candidate(index)) {
        return index;
      }
    }
    return TabStripModel::kNoTab;
  };

  int replacement_index = find_replacement(/*visible_page_only=*/true);
  if (replacement_index == TabStripModel::kNoTab) {
    replacement_index = find_replacement(/*visible_page_only=*/false);
  }
  if (replacement_index == TabStripModel::kNoTab) {
    return false;
  }

  tab_strip_model_->ActivateTabAt(replacement_index);
  return true;
}

bool OriginSpaceController::SelectAdjacentSpace(bool next) {
  const auto& spaces = workspace_service_->GetOriginSpaces();
  if (spaces.empty()) {
    return false;
  }

  const auto current =
      std::ranges::find(spaces, active_space_id_, &OriginSpaceMetadata::id);
  size_t target = 0;
  if (current != spaces.end()) {
    const size_t offset = static_cast<size_t>(current - spaces.begin());
    target = next ? (offset + 1) % spaces.size()
                  : (offset + spaces.size() - 1) % spaces.size();
  } else if (!next) {
    target = spaces.size() - 1;
  }
  return SelectSpace(spaces[target].id);
}

// static
void OriginSpaceController::SetSimulateSessionWithoutSpaceDataForTesting(
    bool simulate) {
  g_simulate_session_without_space_data = simulate;
}

void OriginSpaceController::MaybePopulateTabExtraData(
    int index,
    std::map<std::string, std::string>* extra_data) {
  CHECK(extra_data);
  if (g_simulate_session_without_space_data ||
      !tab_strip_model_->ContainsIndex(index)) {
    return;
  }
  content::WebContents* contents = tab_strip_model_->GetWebContentsAt(index);
  (*extra_data)[kBraveOriginSpaceIdKey] = GetSpaceIdForTab(contents);
  if (IsSidePage(contents)) {
    (*extra_data)[kBraveOriginSidePageKey] = "1";
  }
  if (const GURL home = GetPinnedHomeUrl(contents); home.is_valid()) {
    (*extra_data)[kBraveOriginPinnedHomeUrlKey] = home.spec();
  }
  if (const std::u16string* title = GetCustomTitle(contents)) {
    (*extra_data)[kBraveOriginCustomTitleKey] = base::UTF16ToUTF8(*title);
  }
}

void OriginSpaceController::MaybeRestoreTabSpace(
    content::WebContents* restored_contents,
    const std::map<std::string, std::string>& extra_data) {
  const std::string* restored_space_id =
      base::FindOrNull(extra_data, kBraveOriginSpaceIdKey);
  std::string space_id;
  if (restored_space_id &&
      workspace_service_->GetOriginSpace(*restored_space_id)) {
    space_id = *restored_space_id;
  }
  // A session file can lack Space data -- older builds lost it whenever the
  // session was rebuilt -- but every move is also backed up under the tab's
  // SessionID. The restored tab has a new ID, so look the backup up under the
  // one it had in the file first.
  if (space_id.empty()) {
    if (const std::optional<SessionID> saved_id =
            RestoredSessionId(extra_data, kBraveOriginRestoredTabIdKey)) {
      std::optional<std::string> backup = GetSessionSpaceBackup(
          profile_->GetPrefs(), kOriginTabSessionSpacesPref, *saved_id);
      if (backup && workspace_service_->GetOriginSpace(*backup)) {
        space_id = std::move(*backup);
      }
    }
  }
  if (space_id.empty()) {
    if (const auto* session_helper =
            sessions::SessionTabHelper::FromWebContents(restored_contents)) {
      std::optional<std::string> backup = GetSessionSpaceBackup(
          profile_->GetPrefs(), kOriginTabSessionSpacesPref,
          session_helper->session_id());
      if (backup && workspace_service_->GetOriginSpace(*backup)) {
        space_id = std::move(*backup);
      }
    }
  }
  if (space_id.empty()) {
    space_id = DefaultSpaceId();
  }
  if (GetSpaceIdForTab(restored_contents) == space_id) {
    // Restored tabs start in the default Space. Persist that assignment even
    // when no move is needed, otherwise a later full session rebuild loses it.
    WriteTabSessionData(restored_contents);
  } else {
    MoveTabToSpace(restored_contents, space_id);
  }
  if (const std::string* side_page =
          base::FindOrNull(extra_data, kBraveOriginSidePageKey);
      side_page && *side_page == "1") {
    SetSidePage(restored_contents, true);
  }
  if (const std::string* home =
          base::FindOrNull(extra_data, kBraveOriginPinnedHomeUrlKey);
      home && !home->empty()) {
    // The tab is pinned by the time its extra data is applied.
    SetPinnedHomeUrl(restored_contents, GURL(*home));
  }
  if (const std::string* title =
          base::FindOrNull(extra_data, kBraveOriginCustomTitleKey);
      title && !title->empty()) {
    SetCustomTitle(restored_contents, base::UTF8ToUTF16(*title));
  }
  if (tab_strip_model_->GetActiveWebContents() == restored_contents) {
    if (!restoring_active_space_id_) {
      active_space_id_ = space_id;
      WriteWindowSessionData();
      RememberActiveTab(restored_contents);
      NotifyChanged();
    }
  }
}

void OriginSpaceController::MaybePopulateWindowExtraData(
    std::map<std::string, std::string>* extra_data) const {
  CHECK(extra_data);
  if (g_simulate_session_without_space_data) {
    return;
  }
  (*extra_data)[kBraveOriginActiveSpaceIdKey] = active_space_id_;
}

void OriginSpaceController::BeginWindowRestore(
    const std::map<std::string, std::string>& extra_data) {
  const std::string* restored_space_id =
      base::FindOrNull(extra_data, kBraveOriginActiveSpaceIdKey);
  if (!restored_space_id ||
      !workspace_service_->GetOriginSpace(*restored_space_id)) {
    // As for tabs: prefer the backup under the window's ID in the file.
    std::optional<std::string> backup;
    if (const std::optional<SessionID> saved_id =
            RestoredSessionId(extra_data, kBraveOriginRestoredWindowIdKey)) {
      backup = GetSessionSpaceBackup(profile_->GetPrefs(),
                                     kOriginWindowSessionSpacesPref, *saved_id);
    }
    if (!backup || !workspace_service_->GetOriginSpace(*backup)) {
      backup = GetSessionSpaceBackup(
          profile_->GetPrefs(), kOriginWindowSessionSpacesPref, window_id_);
    }
    if (backup && workspace_service_->GetOriginSpace(*backup)) {
      restoring_active_space_id_ = *backup;
      active_space_id_ = *backup;
      NotifyChanged();
      return;
    }
    restoring_active_space_id_.reset();
    return;
  }

  restoring_active_space_id_ = *restored_space_id;
  active_space_id_ = *restored_space_id;
  NotifyChanged();
}

void OriginSpaceController::FinishWindowRestore() {
  if (!restoring_active_space_id_) {
    return;
  }

  active_space_id_ = *restoring_active_space_id_;
  const int target_index = FindPreferredTabIndex(active_space_id_);
  if (target_index != TabStripModel::kNoTab) {
    tab_strip_model_->ActivateTabAt(target_index);
  }
  restoring_active_space_id_.reset();
  WriteWindowSessionData();
  NotifyChanged();
}

void OriginSpaceController::AddObserver(Observer* observer) {
  observers_.AddObserver(observer);
}

void OriginSpaceController::RemoveObserver(Observer* observer) {
  observers_.RemoveObserver(observer);
}

void OriginSpaceController::OnTabStripModelChanged(
    TabStripModel* tab_strip_model,
    const TabStripModelChange& change,
    const TabStripSelectionChange& selection) {
  if (change.type() == TabStripModelChange::Type::kInserted) {
    for (const auto& inserted : change.GetInsert()->contents) {
      EnsureSpaceForTab(inserted.contents,
                        GetBackedUpTabSpaceOrFallback(
                            profile_->GetPrefs(), workspace_service_,
                            inserted.contents, active_space_id_));
      WriteTabSessionData(inserted.contents);
    }
  } else if (change.type() == TabStripModelChange::Type::kReplaced) {
    const auto* replace = change.GetReplace();
    const std::string space_id = GetSpaceIdForTab(replace->old_contents);
    EnsureSpaceForTab(replace->new_contents, space_id);
    // A discarded page comes back as new contents and keeps what Origin knows
    // about it.
    const auto* old_data =
        OriginSpaceTabData::FromWebContents(replace->old_contents);
    auto* new_data = OriginSpaceTabData::FromWebContents(replace->new_contents);
    if (old_data && new_data) {
      new_data->set_side_page(old_data->side_page());
      new_data->set_pinned_home_url(old_data->pinned_home_url());
      new_data->set_custom_title(old_data->custom_title());
    }
    WriteTabSessionData(replace->new_contents);
  } else if (change.type() == TabStripModelChange::Type::kRemoved) {
    for (const auto& removed : change.GetRemove()->contents) {
      if (!removed.session_id) {
        continue;
      }
      std::erase_if(last_active_tab_by_space_, [&](const auto& entry) {
        return entry.second == *removed.session_id;
      });
    }
  }

  if (selection.active_tab_changed() && selection.new_contents) {
    if (change.type() == TabStripModelChange::Type::kRemoved) {
      // Chromium may select the nearest tab after a close even when it belongs
      // to another space. Preserve the selected space and repair selection
      // after the current model notification finishes.
      base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
          FROM_HERE,
          base::BindOnce(&OriginSpaceController::EnsureActiveTabInActiveSpace,
                         weak_factory_.GetWeakPtr()));
    } else if (!restoring_active_space_id_) {
      const std::string selected_space_id =
          EnsureSpaceForTab(selection.new_contents, active_space_id_);
      if (workspace_service_->GetOriginSpace(selected_space_id)) {
        const bool active_space_changed = active_space_id_ != selected_space_id;
        active_space_id_ = selected_space_id;
        if (active_space_changed) {
          // Picking a page from a previewed Space makes that Space active.
          preview_space_id_.reset();
          WriteWindowSessionData();
        }
      }
      RememberActiveTab(selection.new_contents);
    }
  }

  if (change.type() != TabStripModelChange::Type::kSelectionOnly ||
      selection.active_tab_changed()) {
    NotifyChanged();
  }
}

void OriginSpaceController::OnTabChangedAt(tabs::TabInterface* tab,
                                           TabChangeType change_type) {
  // Navigation can turn the hidden New Tab renderer into a real page (or the
  // reverse) without changing TabStripModel membership. Refresh sidebar rows
  // and counts whenever the tab's renderer data changes.
  NotifyChanged();
}

void OriginSpaceController::OnTabPinnedStateChanged(tabs::TabInterface* tab,
                                                    int index) {
  // Pinning records where the page is now; unpinning forgets it.
  content::WebContents* contents = tab->GetContents();
  SetPinnedHomeUrl(contents,
                   tab->IsPinned() ? contents->GetLastCommittedURL() : GURL());
  NotifyChanged();
}

void OriginSpaceController::OnOriginSpacesChanged() {
  if (restoring_active_space_id_ &&
      !workspace_service_->GetOriginSpace(*restoring_active_space_id_)) {
    restoring_active_space_id_.reset();
  }
  if (!workspace_service_->GetOriginSpace(active_space_id_)) {
    active_space_id_ = DefaultSpaceId();
    WriteWindowSessionData();
  }
  for (int index = 0; index < tab_strip_model_->count(); ++index) {
    auto* contents = tab_strip_model_->GetWebContentsAt(index);
    if (!workspace_service_->GetOriginSpace(GetSpaceIdForTab(contents))) {
      MoveTabToSpace(contents, DefaultSpaceId());
    }
  }
  // A deleted Space's side page joins the default Space, which may already
  // have one. Keep the first and return the rest to the list.
  std::set<std::string> spaces_with_side_page;
  for (int index = 0; index < tab_strip_model_->count(); ++index) {
    auto* contents = tab_strip_model_->GetWebContentsAt(index);
    if (IsSidePage(contents) &&
        !spaces_with_side_page.insert(GetSpaceIdForTab(contents)).second) {
      OriginSpaceTabData::FromWebContents(contents)->set_side_page(false);
      WriteTabSessionData(contents);
    }
  }
  NotifyChanged();
}

const std::string& OriginSpaceController::DefaultSpaceId() const {
  CHECK(workspace_service_);
  CHECK(!workspace_service_->GetOriginSpaces().empty());
  return workspace_service_->GetOriginSpaces().front().id;
}

std::string OriginSpaceController::EnsureSpaceForTab(
    content::WebContents* contents,
    const std::string& preferred_space_id) {
  if (auto* data = OriginSpaceTabData::FromWebContents(contents)) {
    if (workspace_service_->GetOriginSpace(data->space_id())) {
      return data->space_id();
    }
    data->set_space_id(DefaultSpaceId());
    return data->space_id();
  }

  const std::string space_id =
      workspace_service_->GetOriginSpace(preferred_space_id)
          ? preferred_space_id
          : DefaultSpaceId();
  OriginSpaceTabData::CreateForWebContents(contents, space_id);
  return space_id;
}

int OriginSpaceController::FindPreferredTabIndex(
    const std::string& space_id) const {
  const auto remembered = last_active_tab_by_space_.find(space_id);
  int first_index = TabStripModel::kNoTab;
  for (int index = 0; index < tab_strip_model_->count(); ++index) {
    auto* contents = tab_strip_model_->GetWebContentsAt(index);
    // The side page is never the main area's page.
    if (GetSpaceIdForTab(contents) != space_id || IsSidePage(contents)) {
      continue;
    }
    if (first_index == TabStripModel::kNoTab) {
      first_index = index;
    }
    if (remembered == last_active_tab_by_space_.end()) {
      continue;
    }
    const auto* helper = sessions::SessionTabHelper::FromWebContents(contents);
    if (helper && helper->session_id() == remembered->second) {
      return index;
    }
  }
  return first_index;
}

void OriginSpaceController::RememberActiveTab(content::WebContents* contents) {
  if (!contents || IsSidePage(contents)) {
    return;
  }
  const auto* helper = sessions::SessionTabHelper::FromWebContents(contents);
  if (!helper) {
    return;
  }
  last_active_tab_by_space_.insert_or_assign(GetSpaceIdForTab(contents),
                                             helper->session_id());
}

void OriginSpaceController::EnsureActiveTabInActiveSpace() {
  auto* active_contents = tab_strip_model_->GetActiveWebContents();
  // After a close, Chromium may select a neighbouring side page. That page
  // stays in the side view, which would leave the main area empty.
  if (active_contents && IsTabInActiveSpace(active_contents) &&
      !IsSidePage(active_contents)) {
    RememberActiveTab(active_contents);
    NotifyChanged();
    return;
  }

  const int target_index = FindPreferredTabIndex(active_space_id_);
  if (target_index != TabStripModel::kNoTab) {
    tab_strip_model_->ActivateTabAt(target_index);
  }
  NotifyChanged();
}

void OriginSpaceController::WriteTabSessionData(
    content::WebContents* contents) {
  auto* session_helper = sessions::SessionTabHelper::FromWebContents(contents);
  if (!session_helper) {
    return;
  }
  const std::string space_id = GetSpaceIdForTab(contents);
  SetTabSessionSpaceBackup(profile_->GetPrefs(), contents, space_id);
  SessionService* session_service =
      SessionServiceFactory::GetForProfileIfExisting(profile_);
  if (!session_service || g_simulate_session_without_space_data) {
    return;
  }
  session_service->AddTabExtraData(window_id_, session_helper->session_id(),
                                   kBraveOriginSpaceIdKey, space_id);
  session_service->AddTabExtraData(window_id_, session_helper->session_id(),
                                   kBraveOriginSidePageKey,
                                   IsSidePage(contents) ? "1" : "0");
  session_service->AddTabExtraData(window_id_, session_helper->session_id(),
                                   kBraveOriginPinnedHomeUrlKey,
                                   GetPinnedHomeUrl(contents).spec());
  const std::u16string* title = GetCustomTitle(contents);
  session_service->AddTabExtraData(
      window_id_, session_helper->session_id(), kBraveOriginCustomTitleKey,
      title ? base::UTF16ToUTF8(*title) : std::string());
}

void OriginSpaceController::WriteWindowSessionData() {
  SetSessionSpaceBackup(profile_->GetPrefs(), kOriginWindowSessionSpacesPref,
                        window_id_, active_space_id_);
  SessionService* session_service =
      SessionServiceFactory::GetForProfileIfExisting(profile_);
  if (!session_service || g_simulate_session_without_space_data) {
    return;
  }
  session_service->AddWindowExtraData(window_id_, kBraveOriginActiveSpaceIdKey,
                                      active_space_id_);
}

void OriginSpaceController::NotifyChanged() {
  observers_.Notify(&Observer::OnOriginSpaceControllerChanged);
}
