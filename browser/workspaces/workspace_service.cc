/* Copyright (c) 2026 The Brave Authors. All rights reserved.
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at https://mozilla.org/MPL/2.0/. */

#include "brave/browser/workspaces/workspace_service.h"

#include <algorithm>
#include <array>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "base/files/file_util.h"
#include "base/functional/bind.h"
#include "base/hash/hash.h"
#include "base/logging.h"
#include "base/strings/string_util.h"
#include "base/task/bind_post_task.h"
#include "base/task/sequenced_task_runner.h"
#include "base/task/thread_pool.h"
#include "base/time/time.h"
#include "base/uuid.h"
#include "base/values.h"
#include "brave/browser/workspaces/pref_names.h"
#include "brave/browser/workspaces/workspace_session_utils.h"
#include "brave/browser/workspaces/workspace_utils.h"
#include "chrome/browser/profiles/profile.h"
#include "components/prefs/pref_service.h"
#include "components/prefs/scoped_user_pref_update.h"
#include "components/sessions/core/session_id.h"
#include "net/base/registry_controlled_domains/registry_controlled_domain.h"
#include "third_party/abseil-cpp/absl/strings/str_format.h"
#include "url/gurl.h"

namespace {

std::string ComputeKey(const std::string& name) {
  return absl::StrFormat("%08x", base::PersistentHash(name));
}

constexpr char kSpaceIdKey[] = "id";
constexpr char kSpaceNameKey[] = "name";
constexpr char kSpaceIconKey[] = "icon";

struct DefaultOriginSpace {
  std::string_view name;
  std::string_view icon;
};

constexpr auto kDefaultOriginSpaces = std::to_array<DefaultOriginSpace>({
    {"Home", kOriginSpaceIconHome},
    {"Work", kOriginSpaceIconWork},
    {"Playground", kOriginSpaceIconPlayground},
    {"Reading", kOriginSpaceIconReading},
    {"Dev", kOriginSpaceIconTerminal},
});

bool IsOriginSpaceIcon(std::string_view icon) {
  constexpr auto kIconNames = std::to_array<std::string_view>({
      kOriginSpaceIconHome,
      kOriginSpaceIconWork,
      kOriginSpaceIconPlayground,
      kOriginSpaceIconReading,
      kOriginSpaceIconTerminal,
      kOriginSpaceIconIdeas,
      kOriginSpaceIconMessages,
      kOriginSpaceIconSchool,
      kOriginSpaceIconShopping,
      kOriginSpaceIconTravel,
  });
  return std::ranges::find(kIconNames, icon) != kIconNames.end();
}

std::string NormalizeOriginSpaceIcon(std::string_view icon,
                                     std::string_view name) {
  if (IsOriginSpaceIcon(icon)) {
    return std::string(icon);
  }

  const std::string lower_name = base::ToLowerASCII(name);
  if (lower_name.find("home") != std::string::npos ||
      lower_name.find("personal") != std::string::npos) {
    return kOriginSpaceIconHome;
  }
  if (lower_name.find("work") != std::string::npos ||
      lower_name.find("design") != std::string::npos) {
    return kOriginSpaceIconWork;
  }
  if (lower_name.find("play") != std::string::npos ||
      lower_name.find("lab") != std::string::npos) {
    return kOriginSpaceIconPlayground;
  }
  if (lower_name.find("read") != std::string::npos ||
      lower_name.find("research") != std::string::npos) {
    return kOriginSpaceIconReading;
  }
  if (lower_name.find("dev") != std::string::npos ||
      lower_name.find("code") != std::string::npos) {
    return kOriginSpaceIconTerminal;
  }
  return kOriginSpaceIconIdeas;
}

}  // namespace

WorkspaceService::WorkspaceService(Profile& profile)
    : profile_(profile),
      workspaces_path_(profile.GetPath().AppendASCII("workspaces")),
      pref_service_(*profile.GetPrefs()),
      io_task_runner_(base::ThreadPool::CreateSequencedTaskRunner(
          {base::MayBlock(), base::TaskPriority::USER_VISIBLE,
           base::TaskShutdownBehavior::BLOCK_SHUTDOWN})) {
  LoadOriginSpaces();
}

WorkspaceService::~WorkspaceService() = default;

std::vector<WorkspaceMetadata> WorkspaceService::ListWorkspaces() const {
  return ListWorkspacesFromDict(
      pref_service_->GetDict(kWorkspacesMetadataPref));
}

const std::vector<OriginSpaceMetadata>& WorkspaceService::GetOriginSpaces()
    const {
  return origin_spaces_;
}

const OriginSpaceMetadata* WorkspaceService::GetOriginSpace(
    const std::string& id) const {
  auto it = std::ranges::find(origin_spaces_, id, &OriginSpaceMetadata::id);
  return it == origin_spaces_.end() ? nullptr : &*it;
}

std::string WorkspaceService::CreateOriginSpace(std::string name,
                                                std::string icon) {
  if (name.empty()) {
    name = "Untitled";
  }
  icon = NormalizeOriginSpaceIcon(icon, name);
  OriginSpaceMetadata space{
      .id = base::Uuid::GenerateRandomV4().AsLowercaseString(),
      .name = std::move(name),
      .icon = std::move(icon)};
  origin_spaces_.push_back(std::move(space));
  SaveOriginSpaces();
  NotifyOriginSpacesChanged();
  return origin_spaces_.back().id;
}

bool WorkspaceService::UpdateOriginSpace(const OriginSpaceMetadata& space) {
  auto it =
      std::ranges::find(origin_spaces_, space.id, &OriginSpaceMetadata::id);
  if (it == origin_spaces_.end()) {
    return false;
  }
  it->name = space.name.empty() ? "Untitled" : space.name;
  it->icon = NormalizeOriginSpaceIcon(space.icon, it->name);
  SaveOriginSpaces();
  NotifyOriginSpacesChanged();
  return true;
}

bool WorkspaceService::DeleteOriginSpace(const std::string& id) {
  if (origin_spaces_.size() <= 1u) {
    return false;
  }
  auto it = std::ranges::find(origin_spaces_, id, &OriginSpaceMetadata::id);
  if (it == origin_spaces_.end()) {
    return false;
  }
  origin_spaces_.erase(it);
  {
    ScopedDictPrefUpdate rules(*pref_service_, kOriginDomainSpaceRulesPref);
    std::vector<std::string> stale_domains;
    for (const auto [domain, value] : *rules) {
      if (value.is_string() && value.GetString() == id) {
        stale_domains.emplace_back(domain);
      }
    }
    for (const auto& domain : stale_domains) {
      rules->Remove(domain);
    }
  }
  if (pref_service_->GetString(kOriginLastTemporaryLinkSpacePref) == id) {
    pref_service_->ClearPref(kOriginLastTemporaryLinkSpacePref);
  }
  SaveOriginSpaces();
  NotifyOriginSpacesChanged();
  return true;
}

// static
std::string WorkspaceService::GetOriginDomainKey(const GURL& url) {
  if (!url.is_valid() || !url.has_host()) {
    return {};
  }
  std::string domain = net::registry_controlled_domains::GetDomainAndRegistry(
      url, net::registry_controlled_domains::INCLUDE_PRIVATE_REGISTRIES);
  if (domain.empty()) {
    domain = url.host();
  }
  return base::ToLowerASCII(domain);
}

std::optional<std::string> WorkspaceService::GetOriginSpaceForDomain(
    const GURL& url) const {
  const std::string domain = GetOriginDomainKey(url);
  if (domain.empty()) {
    return std::nullopt;
  }
  const std::string* space_id =
      pref_service_->GetDict(kOriginDomainSpaceRulesPref).FindString(domain);
  if (!space_id || !GetOriginSpace(*space_id)) {
    return std::nullopt;
  }
  return *space_id;
}

bool WorkspaceService::SetOriginSpaceForDomain(const GURL& url,
                                               const std::string& space_id) {
  const std::string domain = GetOriginDomainKey(url);
  if (domain.empty() || !GetOriginSpace(space_id)) {
    return false;
  }
  ScopedDictPrefUpdate rules(*pref_service_, kOriginDomainSpaceRulesPref);
  rules->Set(domain, space_id);
  return true;
}

bool WorkspaceService::ClearOriginSpaceForDomain(const GURL& url) {
  const std::string domain = GetOriginDomainKey(url);
  if (domain.empty()) {
    return false;
  }
  ScopedDictPrefUpdate rules(*pref_service_, kOriginDomainSpaceRulesPref);
  return rules->Remove(domain);
}

std::optional<std::string> WorkspaceService::GetLastOriginTemporaryLinkSpace()
    const {
  const std::string& space_id =
      pref_service_->GetString(kOriginLastTemporaryLinkSpacePref);
  if (space_id.empty() || !GetOriginSpace(space_id)) {
    return std::nullopt;
  }
  return space_id;
}

bool WorkspaceService::SetLastOriginTemporaryLinkSpace(
    const std::string& space_id) {
  if (!GetOriginSpace(space_id)) {
    return false;
  }
  pref_service_->SetString(kOriginLastTemporaryLinkSpacePref, space_id);
  return true;
}

bool WorkspaceService::ReorderOriginSpace(const std::string& id,
                                          size_t target_index) {
  auto it = std::ranges::find(origin_spaces_, id, &OriginSpaceMetadata::id);
  if (it == origin_spaces_.end()) {
    return false;
  }
  target_index = std::min(target_index, origin_spaces_.size() - 1u);
  OriginSpaceMetadata space = std::move(*it);
  origin_spaces_.erase(it);
  origin_spaces_.insert(origin_spaces_.begin() + target_index,
                        std::move(space));
  SaveOriginSpaces();
  NotifyOriginSpacesChanged();
  return true;
}

void WorkspaceService::AddObserver(Observer* observer) {
  observers_.AddObserver(observer);
}

void WorkspaceService::RemoveObserver(Observer* observer) {
  observers_.RemoveObserver(observer);
}

void WorkspaceService::LoadOriginSpaces() {
  origin_spaces_.clear();
  bool migrated_icons = false;
  for (const auto& value : pref_service_->GetList(kOriginSpacesPref)) {
    const auto* dict = value.GetIfDict();
    if (!dict) {
      continue;
    }
    const std::string* id = dict->FindString(kSpaceIdKey);
    const std::string* name = dict->FindString(kSpaceNameKey);
    const std::string* icon = dict->FindString(kSpaceIconKey);
    if (!id || id->empty() || !name || !icon) {
      continue;
    }
    std::string normalized_icon = NormalizeOriginSpaceIcon(*icon, *name);
    migrated_icons |= normalized_icon != *icon;
    origin_spaces_.push_back(
        {.id = *id, .name = *name, .icon = std::move(normalized_icon)});
  }
  if (origin_spaces_.empty()) {
    for (const auto& default_space : kDefaultOriginSpaces) {
      origin_spaces_.push_back(
          {.id = base::Uuid::GenerateRandomV4().AsLowercaseString(),
           .name = std::string(default_space.name),
           .icon = std::string(default_space.icon)});
    }
    SaveOriginSpaces();
  } else if (origin_spaces_.size() == 1u &&
             origin_spaces_.front().name == kDefaultOriginSpaces.front().name &&
             origin_spaces_.front().icon == kDefaultOriginSpaces.front().icon) {
    // Early Origin profiles shipped with only Home. Complete that untouched
    // starter rail once so existing testers see the same defaults as a clean
    // profile without replacing any custom spaces.
    for (size_t i = 1; i < kDefaultOriginSpaces.size(); ++i) {
      origin_spaces_.push_back(
          {.id = base::Uuid::GenerateRandomV4().AsLowercaseString(),
           .name = std::string(kDefaultOriginSpaces[i].name),
           .icon = std::string(kDefaultOriginSpaces[i].icon)});
    }
    SaveOriginSpaces();
  } else if (migrated_icons) {
    SaveOriginSpaces();
  }
}

void WorkspaceService::SaveOriginSpaces() {
  base::ListValue list;
  for (const auto& space : origin_spaces_) {
    base::DictValue dict;
    dict.Set(kSpaceIdKey, space.id);
    dict.Set(kSpaceNameKey, space.name);
    dict.Set(kSpaceIconKey, space.icon);
    list.Append(std::move(dict));
  }
  pref_service_->SetList(kOriginSpacesPref, std::move(list));
}

void WorkspaceService::NotifyOriginSpacesChanged() {
  for (Observer& observer : observers_) {
    observer.OnOriginSpacesChanged();
  }
}

void WorkspaceService::SaveWorkspaceMetadata(const WorkspaceMetadata& meta) {
  ScopedDictPrefUpdate updated(*pref_service_, kWorkspacesMetadataPref);
  updated->Set(ComputeKey(meta.name), WorkspaceMetadataToDictEntry(meta));
}

void WorkspaceService::RemoveWorkspaceMetadata(const std::string& name) {
  ScopedDictPrefUpdate updated(*pref_service_, kWorkspacesMetadataPref);
  updated->Remove(ComputeKey(name));
}

void WorkspaceService::DeleteWorkspace(const std::string& name) {
  RemoveWorkspaceMetadata(name);
  io_task_runner_->PostTask(FROM_HERE, base::BindOnce(
                                           [](base::FilePath dir) {
                                             if (base::PathExists(dir)) {
                                               base::DeletePathRecursively(dir);
                                             }
                                           },
                                           GetWorkspacePathForName(name)));
}

base::FilePath WorkspaceService::GetWorkspacePathForName(
    const std::string& name) const {
  return workspaces_path_.AppendASCII(ComputeKey(name));
}

void WorkspaceService::SaveWorkspace(const std::string& name) {
  if (name.empty()) {
    return;
  }

  // Collect session commands on the UI thread, then write to disk on a
  // background task (WriteWorkspaceToDisk does blocking file I/O).
  WorkspaceMetadata workspace{.name = name, .modified_at = base::Time::Now()};
  std::vector<std::unique_ptr<sessions::SessionCommand>> commands =
      GenerateBrowserSessionCommandsForWorkspace(base::to_address(profile_),
                                                 workspace);
  base::FilePath workspace_path = GetWorkspacePathForName(name);

  auto backend = base::MakeRefCounted<sessions::CommandStorageBackend>(
      io_task_runner_, workspace_path, kWorkspaceSessionType,
      /*encryptor=*/nullptr);

  // Save metadata optimistically now; roll it back if the write fails.
  // BindPostTask ensures on_error always runs on the UI thread whether it is
  // invoked by AppendCommands or directly by WriteWorkspaceToDisk on a
  // directory-creation failure.
  SaveWorkspaceMetadata(workspace);

  // "Rolling back" is just removing the metadata from the dictionary.
  auto on_error = base::BindPostTask(
      base::SequencedTaskRunner::GetCurrentDefault(),
      base::BindOnce(&WorkspaceService::RemoveWorkspaceMetadata, GetWeakPtr(),
                     name));

  io_task_runner_->PostTask(
      FROM_HERE, base::BindOnce(&WriteWorkspaceToDisk, std::move(commands),
                                std::move(workspace_path), std::move(backend),
                                std::move(on_error)));
}

void WorkspaceService::RestoreWorkspace(const std::string& name) {
  base::FilePath path = GetWorkspacePathForName(name);
  auto backend = base::MakeRefCounted<sessions::CommandStorageBackend>(
      io_task_runner_, path, kWorkspaceSessionType,
      /*encryptor=*/nullptr);

  io_task_runner_->PostTaskAndReplyWithResult(
      FROM_HERE,
      base::BindOnce(&ReadWorkspaceFromDisk, std::move(path),
                     std::move(backend)),
      base::BindOnce(&WorkspaceService::DoRestoreWorkspace, GetWeakPtr()));
}

void WorkspaceService::DoRestoreWorkspace(
    std::vector<std::unique_ptr<sessions::SessionCommand>> commands) {
  if (commands.empty()) {
    DVLOG(1) << "Could not load workspace: no commands";
    return;
  }

  RestoreBrowserSessionCommandsForWorkspace(base::to_address(profile_),
                                            std::move(commands));
}

base::WeakPtr<WorkspaceService> WorkspaceService::GetWeakPtr() {
  return weak_ptr_factory_.GetWeakPtr();
}

void WorkspaceService::Shutdown() {
  observers_.Clear();
  weak_ptr_factory_.InvalidateWeakPtrs();
}
