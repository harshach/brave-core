// Copyright (c) 2026 The Brave Authors. All rights reserved.
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this file,
// You can obtain one at https://mozilla.org/MPL/2.0/.

#include "brave/browser/ui/tabs/origin_space_controller.h"

#include <algorithm>
#include <optional>
#include <utility>
#include <vector>

#include "base/check.h"
#include "base/containers/map_util.h"
#include "base/functional/bind.h"
#include "base/memory/weak_ptr.h"
#include "base/strings/string_number_conversions.h"
#include "base/task/sequenced_task_runner.h"
#include "brave/browser/sessions/brave_session_keys.h"
#include "brave/browser/workspaces/pref_names.h"
#include "brave/browser/workspaces/workspace_service_factory.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/sessions/session_service.h"
#include "chrome/browser/sessions/session_service_factory.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/browser/ui/webui/ntp/new_tab_ui.h"
#include "components/prefs/pref_service.h"
#include "components/prefs/scoped_user_pref_update.h"
#include "components/sessions/content/session_tab_helper.h"
#include "content/public/browser/web_contents.h"
#include "content/public/browser/web_contents_user_data.h"
#include "url/url_constants.h"

namespace {

constexpr size_t kMaxSessionSpaceBackups = 4096;

std::string SessionIdKey(SessionID id) {
  return base::NumberToString(id.id());
}

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

 private:
  friend class content::WebContentsUserData<OriginSpaceTabData>;

  OriginSpaceTabData(content::WebContents* contents, std::string space_id)
      : content::WebContentsUserData<OriginSpaceTabData>(*contents),
        space_id_(std::move(space_id)) {}

  std::string space_id_;
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
    return true;
  }

  restoring_active_space_id_.reset();
  active_space_id_ = space_id;
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
  return IsTabInActiveSpace(contents) && !IsTabPlaceholder(contents);
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
        !tab_strip_model_->IsTabPinned(index) &&
        !IsTabPlaceholder(contents)) {
      ++page_count;
    }
  }
  return page_count;
}

bool OriginSpaceController::ActiveSpaceHasTabs() const {
  for (int index = 0; index < tab_strip_model_->count(); ++index) {
    if (IsTabInActiveSpace(tab_strip_model_->GetWebContentsAt(index))) {
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

bool OriginSpaceController::SelectAdjacentTab(bool next) {
  std::vector<int> indices;
  for (int index = 0; index < tab_strip_model_->count(); ++index) {
    if (IsTabInActiveSpace(tab_strip_model_->GetWebContentsAt(index))) {
      indices.push_back(index);
    }
  }
  if (indices.empty()) {
    return false;
  }

  const int active = tab_strip_model_->active_index();
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
  const auto find_replacement = [&](bool visible_page_only) {
    const auto is_candidate = [&](int index) {
      if (is_closing(index)) {
        return false;
      }
      auto* contents = tab_strip_model_->GetWebContentsAt(index);
      return visible_page_only ? ShouldShowTabInPageList(contents)
                               : IsTabInActiveSpace(contents);
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

void OriginSpaceController::MaybePopulateTabExtraData(
    int index,
    std::map<std::string, std::string>* extra_data) {
  CHECK(extra_data);
  if (!tab_strip_model_->ContainsIndex(index)) {
    return;
  }
  (*extra_data)[kBraveOriginSpaceIdKey] =
      GetSpaceIdForTab(tab_strip_model_->GetWebContentsAt(index));
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
  } else if (const auto* session_helper =
                 sessions::SessionTabHelper::FromWebContents(
                     restored_contents)) {
    std::optional<std::string> backup = GetSessionSpaceBackup(
        profile_->GetPrefs(), kOriginTabSessionSpacesPref,
        session_helper->session_id());
    if (backup && workspace_service_->GetOriginSpace(*backup)) {
      space_id = std::move(*backup);
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
  (*extra_data)[kBraveOriginActiveSpaceIdKey] = active_space_id_;
}

void OriginSpaceController::BeginWindowRestore(
    const std::map<std::string, std::string>& extra_data) {
  const std::string* restored_space_id =
      base::FindOrNull(extra_data, kBraveOriginActiveSpaceIdKey);
  if (!restored_space_id ||
      !workspace_service_->GetOriginSpace(*restored_space_id)) {
    const std::optional<std::string> backup = GetSessionSpaceBackup(
        profile_->GetPrefs(), kOriginWindowSessionSpacesPref, window_id_);
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
    if (GetSpaceIdForTab(contents) != space_id) {
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
  if (!contents) {
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
  if (active_contents && IsTabInActiveSpace(active_contents)) {
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
  if (!session_service) {
    return;
  }
  session_service->AddTabExtraData(window_id_, session_helper->session_id(),
                                   kBraveOriginSpaceIdKey, space_id);
}

void OriginSpaceController::WriteWindowSessionData() {
  SetSessionSpaceBackup(profile_->GetPrefs(), kOriginWindowSessionSpacesPref,
                        window_id_, active_space_id_);
  SessionService* session_service =
      SessionServiceFactory::GetForProfileIfExisting(profile_);
  if (!session_service) {
    return;
  }
  session_service->AddWindowExtraData(window_id_, kBraveOriginActiveSpaceIdKey,
                                      active_space_id_);
}

void OriginSpaceController::NotifyChanged() {
  observers_.Notify(&Observer::OnOriginSpaceControllerChanged);
}
