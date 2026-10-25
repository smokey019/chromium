// Copyright 2026 The REBA Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/reba_adblock/filter_list_manager.h"

#include <algorithm>
#include <utility>

#include "base/files/file_util.h"
#include "base/files/important_file_writer.h"
#include "base/functional/bind.h"
#include "base/logging.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/string_util.h"
#include "base/task/sequenced_task_runner.h"
#include "base/task/task_traits.h"
#include "base/values.h"
#include "chrome/browser/reba_adblock/engine_builder.h"
#include "chrome/browser/reba_adblock/reba_adblock_prefs.h"
#include "components/prefs/pref_service.h"
#include "components/prefs/scoped_user_pref_update.h"
#include "net/base/load_flags.h"
#include "net/traffic_annotation/network_traffic_annotation.h"
#include "services/network/public/cpp/resource_request.h"
#include "services/network/public/cpp/shared_url_loader_factory.h"
#include "services/network/public/cpp/simple_url_loader.h"
#include "services/network/public/mojom/fetch_api.mojom.h"
#include "url/gurl.h"

namespace reba_adblock {

namespace {

constexpr size_t kMaxListSize = 64 * 1024 * 1024;
constexpr base::TimeDelta kDownloadTimeout = base::Seconds(60);
constexpr base::TimeDelta kRetryAfterFailure = base::Hours(1);
constexpr size_t kMaxParallelDownloads = 3;

constexpr net::NetworkTrafficAnnotationTag kTrafficAnnotation =
    net::DefineNetworkTrafficAnnotation("reba_adblock_filter_list", R"(
      semantics {
        sender: "REBA ad blocker"
        description:
          "Downloads a filter list (for example EasyList) that tells the "
          "built-in ad blocker what to block. Lists are fetched when they are "
          "first turned on and then again when they are out of date."
        trigger:
          "The browser starts, a list is turned on or added, the user presses "
          "'Update now', or a list has passed its update interval."
        data: "The request has no cookies and carries no user data."
        destination: OTHER
        internal {
          contacts {
            email: "reba-browser@example.invalid"
          }
        }
        user_data {
          type: NONE
        }
        last_reviewed: "2026-10-22"
      }
      policy {
        cookies_allowed: NO
        setting:
          "Settings > Ad blocker: turn off 'Update the lists automatically' or "
          "turn the ad blocker off."
        policy_exception_justification: "Not implemented."
      })");

// Counts the rules of a downloaded list, reads its header and writes it to
// `path`; null if it couldn't be written. Lists are up to 64 MB, so this runs
// where blocking is allowed, not on the UI thread.
std::optional<FilterListManager::ListInfo> WriteListFile(
    const base::FilePath& path,
    std::string body) {
  FilterListManager::ListInfo info;
  info.rules = CountRules(body);
  info.metadata = ParseFilterListMetadata(body);
  if (!base::CreateDirectory(path.DirName()) ||
      !base::ImportantFileWriter::WriteFileAtomically(path, body)) {
    return std::nullopt;
  }
  return info;
}

// Ids name files in the lists folder, so they must not reach outside it.
bool IsValidListId(std::string_view list_id) {
  return !list_id.empty() && std::ranges::all_of(list_id, [](char c) {
    return base::IsAsciiAlphaNumeric(c) || c == '-' || c == '_';
  });
}

bool LooksLikeAList(std::string_view body) {
  const std::string_view head =
      base::TrimWhitespaceASCII(body.substr(0, 512), base::TRIM_LEADING);
  if (head.empty()) {
    return false;
  }
  // An error page from a CDN or captive portal.
  const std::string lower = base::ToLowerASCII(head.substr(0, 64));
  return !lower.starts_with("<!doctype") && !lower.starts_with("<html") &&
         !lower.starts_with("<?xml");
}

base::DictValue ToDict(const ListFileState& state) {
  base::DictValue dict;
  dict.Set("updated", state.updated.InSecondsFSinceUnixEpoch());
  dict.Set("size", static_cast<double>(state.size));
  dict.Set("rules", state.rules);
  dict.Set("version", state.version);
  dict.Set("interval", state.update_interval.InSecondsF());
  dict.Set("error", state.error);
  dict.Set("failed", state.last_failure.InSecondsFSinceUnixEpoch());
  return dict;
}

ListFileState FromDict(const base::DictValue& dict) {
  ListFileState state;
  if (std::optional<double> v = dict.FindDouble("updated"); v && *v > 0) {
    state.updated = base::Time::FromSecondsSinceUnixEpoch(*v);
  }
  state.size = static_cast<int64_t>(dict.FindDouble("size").value_or(0));
  state.rules = dict.FindInt("rules").value_or(0);
  if (const std::string* v = dict.FindString("version")) {
    state.version = *v;
  }
  state.update_interval =
      base::Seconds(dict.FindDouble("interval").value_or(0));
  if (const std::string* v = dict.FindString("error")) {
    state.error = *v;
  }
  if (std::optional<double> v = dict.FindDouble("failed"); v && *v > 0) {
    state.last_failure = base::Time::FromSecondsSinceUnixEpoch(*v);
  }
  return state;
}

}  // namespace

struct FilterListManager::Download {
  std::unique_ptr<network::SimpleURLLoader> loader;
};

FilterListManager::FilterListManager(
    const base::FilePath& lists_dir,
    scoped_refptr<network::SharedURLLoaderFactory> url_loader_factory,
    scoped_refptr<base::SequencedTaskRunner> file_task_runner,
    PrefService* local_state,
    Delegate* delegate)
    : lists_dir_(lists_dir),
      url_loader_factory_(std::move(url_loader_factory)),
      file_task_runner_(std::move(file_task_runner)),
      local_state_(local_state),
      delegate_(delegate) {}

FilterListManager::~FilterListManager() = default;

void FilterListManager::SetWantedLists(std::vector<FilterList> lists) {
  wanted_.clear();
  for (FilterList& list : lists) {
    std::string id = list.id;
    wanted_.emplace(std::move(id), std::move(list));
  }
  // Drop queued lists nobody wants any more.
  std::erase_if(queue_,
                [&](const std::string& id) { return !wanted_.contains(id); });
}

base::FilePath FilterListManager::PathFor(const std::string& list_id) const {
  return lists_dir_.AppendASCII(list_id + ".txt");
}

bool FilterListManager::HasFile(const std::string& list_id) const {
  return GetState(list_id).updated != base::Time();
}

ListFileState FilterListManager::GetState(const std::string& list_id) const {
  ListFileState state;
  if (const base::DictValue* dict =
          local_state_->GetDict(prefs::kListStates).FindDict(list_id)) {
    state = FromDict(*dict);
  }
  state.updating =
      in_progress_.contains(list_id) || std::ranges::contains(queue_, list_id);
  return state;
}

void FilterListManager::SaveState(const std::string& list_id,
                                  const ListFileState& state) {
  ScopedDictPrefUpdate update(local_state_, prefs::kListStates);
  update->Set(list_id, ToDict(state));
}

void FilterListManager::Forget(const std::string& list_id) {
  if (!IsValidListId(list_id)) {
    return;
  }
  {
    ScopedDictPrefUpdate update(local_state_, prefs::kListStates);
    update->Remove(list_id);
  }
  // Queued after a write that may be under way, so the file doesn't come
  // back.
  file_task_runner_->PostTask(
      FROM_HERE,
      base::BindOnce(base::IgnoreResult(&base::DeleteFile), PathFor(list_id)));
  // A download in progress is dropped with its loader; one whose file is
  // being written is told by OnWritten() not to save what it knows.
  std::erase(queue_, list_id);
  if (in_progress_.contains(list_id)) {
    EndDownload(list_id);
  }
}

void FilterListManager::MarkMissing(const std::string& list_id) {
  if (in_progress_.contains(list_id) ||
      std::ranges::contains(queue_, list_id)) {
    // Being downloaded already.
    return;
  }
  {
    ScopedDictPrefUpdate update(local_state_, prefs::kListStates);
    if (!update->Remove(list_id)) {
      return;
    }
  }
  delegate_->OnListStatesChanged();
}

bool FilterListManager::IsDue(const FilterList& list, bool force) const {
  if (in_progress_.contains(list.id) ||
      std::ranges::contains(queue_, list.id)) {
    return false;
  }
  const ListFileState state = GetState(list.id);
  if (state.updated == base::Time()) {
    // Missing. Don't hammer a server that keeps failing, though.
    return force || state.last_failure == base::Time() ||
           base::Time::Now() - state.last_failure >= kRetryAfterFailure;
  }
  if (force) {
    return true;
  }
  if (state.last_failure != base::Time() &&
      base::Time::Now() - state.last_failure < kRetryAfterFailure) {
    return false;
  }
  const base::TimeDelta interval = state.update_interval.is_zero()
                                       ? UpdateIntervalFor({})
                                       : state.update_interval;
  return base::Time::Now() - state.updated >= interval;
}

void FilterListManager::Update(bool force) {
  for (const auto& [id, list] : wanted_) {
    if (IsDue(list, force)) {
      queue_.push_back(id);
    }
  }
  if (!queue_.empty()) {
    delegate_->OnListStatesChanged();
  }
  StartNext();
}

void FilterListManager::StartNext() {
  while (!queue_.empty() && in_progress_.size() < kMaxParallelDownloads) {
    const std::string id = queue_.front();
    queue_.erase(queue_.begin());
    auto it = wanted_.find(id);
    if (it == wanted_.end()) {
      continue;
    }
    StartDownload(it->second);
  }
}

void FilterListManager::StartDownload(const FilterList& list) {
  in_progress_[list.id] = std::make_unique<Download>();
  TryUrl(list.id, 0);
}

void FilterListManager::TryUrl(const std::string& list_id, size_t url_index) {
  auto wanted = wanted_.find(list_id);
  if (wanted == wanted_.end() || url_index >= wanted->second.urls.size()) {
    Fail(list_id, "Could not download the list.");
    return;
  }
  const GURL url(wanted->second.urls[url_index]);
  if (!url.is_valid() || !url.SchemeIs("https")) {
    TryUrl(list_id, url_index + 1);
    return;
  }

  auto request = std::make_unique<network::ResourceRequest>();
  request->url = url;
  request->method = "GET";
  request->credentials_mode = network::mojom::CredentialsMode::kOmit;
  auto loader =
      network::SimpleURLLoader::Create(std::move(request), kTrafficAnnotation);
  loader->SetTimeoutDuration(kDownloadTimeout);
  loader->SetRetryOptions(1, network::SimpleURLLoader::RETRY_ON_NETWORK_CHANGE);
  network::SimpleURLLoader* raw_loader = loader.get();
  in_progress_[list_id]->loader = std::move(loader);
  raw_loader->DownloadToString(
      url_loader_factory_.get(),
      base::BindOnce(&FilterListManager::OnDownloaded,
                     weak_factory_.GetWeakPtr(), list_id, url_index),
      kMaxListSize);
}

void FilterListManager::OnDownloaded(const std::string& list_id,
                                     size_t url_index,
                                     std::optional<std::string> body) {
  if (!body || !LooksLikeAList(*body)) {
    TryUrl(list_id, url_index + 1);
    return;
  }

  ListFileState state = GetState(list_id);
  state.updating = false;
  state.updated = base::Time::Now();
  state.size = static_cast<int64_t>(body->size());
  state.error.clear();
  state.last_failure = base::Time();

  file_task_runner_->PostTaskAndReplyWithResult(
      FROM_HERE,
      base::BindOnce(&WriteListFile, PathFor(list_id), std::move(*body)),
      base::BindOnce(&FilterListManager::OnWritten, weak_factory_.GetWeakPtr(),
                     list_id, state));
}

void FilterListManager::OnWritten(const std::string& list_id,
                                  ListFileState state,
                                  std::optional<ListInfo> info) {
  if (!in_progress_.contains(list_id)) {
    // Forgotten while the file was written. The deletion queued after the
    // write removes the file; don't record anything about it.
    return;
  }
  if (!info) {
    Fail(list_id, "Could not save the list.");
    return;
  }
  state.rules = info->rules;
  state.version = info->metadata.version;
  state.update_interval = UpdateIntervalFor(info->metadata);
  SaveState(list_id, state);
  files_changed_ = true;
  EndDownload(list_id);
}

void FilterListManager::Fail(const std::string& list_id,
                             const std::string& error) {
  // A list that was turned off or removed during the download isn't wanted;
  // a failure would only leave state behind for it.
  if (wanted_.contains(list_id)) {
    LOG(WARNING) << "REBA adblock: " << list_id << ": " << error;
    ListFileState state = GetState(list_id);
    state.updating = false;
    state.error = error;
    state.last_failure = base::Time::Now();
    SaveState(list_id, state);
  }
  EndDownload(list_id);
}

void FilterListManager::EndDownload(const std::string& list_id) {
  in_progress_.erase(list_id);
  delegate_->OnListStatesChanged();
  StartNext();
  if (in_progress_.empty() && queue_.empty() && files_changed_) {
    files_changed_ = false;
    delegate_->OnListFilesChanged();
  }
}

}  // namespace reba_adblock
