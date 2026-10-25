// Copyright 2026 The REBA Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/reba_adblock/reba_adblock_service.h"

#include <algorithm>
#include <utility>

#include "base/check.h"
#include "base/command_line.h"
#include "base/functional/bind.h"
#include "base/strings/string_number_conversions.h"
#include "base/task/sequenced_task_runner.h"
#include "base/task/single_thread_task_runner.h"
#include "base/task/thread_pool.h"
#include "base/task/updateable_sequenced_task_runner.h"
#include "base/time/time.h"
#include "base/values.h"
#include "chrome/browser/reba_adblock/engine_builder.h"
#include "chrome/browser/reba_adblock/reba_adblock_prefs.h"
#include "components/prefs/pref_service.h"
#include "components/prefs/scoped_user_pref_update.h"
#include "components/reba_adblock/core/user_filters.h"
#include "services/network/public/cpp/shared_url_loader_factory.h"

namespace reba_adblock {

namespace {

RebaAdblockService* g_instance = nullptr;

// Development: path of a resources.json to use as it is (see
// components/reba_adblock/tools/build_resources.mjs).
constexpr char kResourcesFileSwitch[] = "reba-adblock-resources-file";

constexpr base::TimeDelta kBlockedNotificationDelay = base::Milliseconds(500);
constexpr base::TimeDelta kSaveTotalPeriod = base::Minutes(1);
// How long after startup lists are checked for updates, so the first page
// loads aren't competing with downloads.
constexpr base::TimeDelta kStartupUpdateDelay = base::Seconds(20);
// How often the lists and the resources are checked for updates. Each one is
// only downloaded when it is due.
constexpr base::TimeDelta kUpdateCheckPeriod = base::Minutes(30);

}  // namespace

// static
RebaAdblockService* RebaAdblockService::Get() {
  return g_instance;
}

RebaAdblockService::RebaAdblockService(
    PrefService* local_state,
    const base::FilePath& data_dir,
    scoped_refptr<network::SharedURLLoaderFactory> url_loader_factory)
    : local_state_(local_state),
      data_dir_(data_dir),
      // Content holds BEST_EFFORT tasks back until startup is over, which is
      // fine for downloads but not for the engine that restored tabs need.
      file_task_runner_(base::ThreadPool::CreateSequencedTaskRunner(
          {base::MayBlock(), base::TaskPriority::BEST_EFFORT,
           base::TaskShutdownBehavior::SKIP_ON_SHUTDOWN})),
      engine_task_runner_(base::ThreadPool::CreateUpdateableSequencedTaskRunner(
          {base::MayBlock(), base::TaskPriority::USER_BLOCKING,
           base::TaskShutdownBehavior::SKIP_ON_SHUTDOWN})) {
  CHECK(!g_instance);
  g_instance = this;

  // For development: use a resources file made by tools/build_resources.mjs
  // as it is, unsigned. It is read where it is and never copied over the
  // signed one, so it stops counting as soon as the switch is left out.
  const base::CommandLine* command_line =
      base::CommandLine::ForCurrentProcess();
  uses_local_resources_ = command_line->HasSwitch(kResourcesFileSwitch);
  resources_path_ = uses_local_resources_
                        ? command_line->GetSwitchValuePath(kResourcesFileSwitch)
                        : data_dir_.AppendASCII("resources.json");

  list_manager_ = std::make_unique<FilterListManager>(
      data_dir_.AppendASCII("lists"), url_loader_factory, file_task_runner_,
      local_state_, this);
  resources_manager_ = std::make_unique<ResourcesManager>(
      data_dir_.AppendASCII("resources.json"), std::move(url_loader_factory),
      file_task_runner_, local_state_,
      base::BindRepeating(&RebaAdblockService::RebuildAll,
                          base::Unretained(this)));
}

RebaAdblockService::~RebaAdblockService() {
  // Observers can outlive the service at shutdown (pages, the toolbar
  // button), so they stop observing first, like TemplateURLService's
  // OnTemplateURLServiceShuttingDown().
  for (RebaAdblockServiceObserver& observer : observers_) {
    observer.OnAdblockServiceShutdown();
  }
  SaveTotalBlocked();
  CHECK_EQ(g_instance, this);
  g_instance = nullptr;
}

void RebaAdblockService::Start() {
  pref_registrar_.Init(local_state_);
  for (const char* pref :
       {prefs::kEnabled, prefs::kCosmeticFilteringEnabled, prefs::kEnabledLists,
        prefs::kCustomLists, prefs::kUserFilters, prefs::kTrustedSites,
        prefs::kShowBlockedCount, prefs::kContextMenuEnabled,
        prefs::kPopupGuard, prefs::kAutoUpdate}) {
    pref_registrar_.Add(pref,
                        base::BindRepeating(&RebaAdblockService::OnPrefChanged,
                                            base::Unretained(this), pref));
  }

  LoadSettings();
  total_blocked_ = local_state_->GetInteger(prefs::kTotalBlocked);
  saved_total_blocked_ = total_blocked_;
  save_total_timer_.Start(
      FROM_HERE, kSaveTotalPeriod,
      base::BindRepeating(&RebaAdblockService::SaveTotalBlocked,
                          base::Unretained(this)));

  UpdateWantedLists();
  RebuildAll();

  // First run has nothing to filter with, so don't wait.
  bool any_downloaded = false;
  for (const FilterList& list : EnabledLists()) {
    any_downloaded |= list_manager_->HasFile(list.id);
  }
  const base::TimeDelta delay =
      any_downloaded ? kStartupUpdateDelay : base::Seconds(1);
  base::SingleThreadTaskRunner::GetCurrentDefault()->PostDelayedTask(
      FROM_HERE,
      base::BindOnce(&RebaAdblockService::OnStartupDelayPassed,
                     weak_factory_.GetWeakPtr()),
      delay);
}

void RebaAdblockService::OnStartupDelayPassed() {
  startup_delay_passed_ = true;
  UpdateAutoUpdates();
}

void RebaAdblockService::UpdateAutoUpdates() {
  if (!startup_delay_passed_) {
    // OnStartupDelayPassed() gets here in time.
    return;
  }
  if (!enabled_ || !AutoUpdate()) {
    update_timer_.Stop();
    return;
  }
  if (update_timer_.IsRunning()) {
    return;
  }
  CheckForUpdates();
  update_timer_.Start(FROM_HERE, kUpdateCheckPeriod,
                      base::BindRepeating(&RebaAdblockService::CheckForUpdates,
                                          base::Unretained(this)));
}

void RebaAdblockService::CheckForUpdates() {
  list_manager_->Update(/*force=*/false);
  if (!uses_local_resources_) {
    resources_manager_->Update(/*force=*/false);
  }
}

void RebaAdblockService::AddObserver(RebaAdblockServiceObserver* observer) {
  observers_.AddObserver(observer);
}

void RebaAdblockService::RemoveObserver(RebaAdblockServiceObserver* observer) {
  observers_.RemoveObserver(observer);
}

// --- Blocking ---

bool RebaAdblockService::IsSiteTrusted(const GURL& page_url) const {
  return HostMatchesSites(page_url.host(), trusted_sites_) ||
         HostMatchesSites(page_url.host(), session_trusted_sites_);
}

Decision RebaAdblockService::CheckRequest(const GURL& url,
                                          const GURL& page_url,
                                          RequestType type,
                                          std::string_view method) const {
  if (!enabled_ || !engines_ || IsSiteTrusted(page_url)) {
    return Decision();
  }
  return engines_->CheckRequest(url, page_url, type, method);
}

void RebaAdblockService::RecordBlocked(const GURL& url,
                                       RequestType type,
                                       const GURL& page_url,
                                       const Decision& decision,
                                       bool incognito) {
  ++total_blocked_;
  if (incognito) {
    return;
  }
  BlockedRequest request;
  request.url = url.spec();
  request.type = RequestTypeName(type);
  request.page_host = page_url.host();
  request.filter = decision.filter;
  request.list_title = decision.list_title;
  request.last_blocked = base::Time::Now();
  log_.Record(std::move(request));
  NotifyBlockedRequestsSoon();
}

void RebaAdblockService::ClearBlockedRequests() {
  log_.Clear();
  for (RebaAdblockServiceObserver& observer : observers_) {
    observer.OnBlockedRequestsLogged();
  }
}

void RebaAdblockService::NotifyBlockedRequestsSoon() {
  if (blocked_notification_scheduled_) {
    return;
  }
  blocked_notification_scheduled_ = true;
  base::SingleThreadTaskRunner::GetCurrentDefault()->PostDelayedTask(
      FROM_HERE,
      base::BindOnce(&RebaAdblockService::FlushBlockedNotification,
                     weak_factory_.GetWeakPtr()),
      kBlockedNotificationDelay);
}

void RebaAdblockService::FlushBlockedNotification() {
  blocked_notification_scheduled_ = false;
  for (RebaAdblockServiceObserver& observer : observers_) {
    observer.OnBlockedRequestsLogged();
  }
}

void RebaAdblockService::SaveTotalBlocked() {
  if (total_blocked_ == saved_total_blocked_) {
    return;
  }
  saved_total_blocked_ = total_blocked_;
  local_state_->SetInteger(
      prefs::kTotalBlocked,
      static_cast<int>(std::min<int64_t>(total_blocked_, INT32_MAX)));
}

// --- Settings ---

void RebaAdblockService::LoadSettings() {
  enabled_ = local_state_->GetBoolean(prefs::kEnabled);
  cosmetic_enabled_ =
      local_state_->GetBoolean(prefs::kCosmeticFilteringEnabled);
  trusted_sites_.clear();
  for (const base::Value& site : local_state_->GetList(prefs::kTrustedSites)) {
    if (site.is_string()) {
      trusted_sites_.insert(site.GetString());
    }
  }
}

void RebaAdblockService::OnPrefChanged(const std::string& pref_name) {
  if (pref_name == prefs::kEnabledLists || pref_name == prefs::kCustomLists) {
    UpdateWantedLists();
    RebuildAll();
    list_manager_->Update(/*force=*/false);
  } else if (pref_name == prefs::kUserFilters) {
    OnUserFiltersChanged();
  } else {
    LoadSettings();
    if (pref_name == prefs::kEnabled || pref_name == prefs::kAutoUpdate) {
      UpdateAutoUpdates();
    }
  }
  NotifySettingsChanged();
}

void RebaAdblockService::NotifySettingsChanged() {
  for (RebaAdblockServiceObserver& observer : observers_) {
    observer.OnSettingsChanged();
  }
}

void RebaAdblockService::SetEnabled(bool enabled) {
  local_state_->SetBoolean(prefs::kEnabled, enabled);
}

void RebaAdblockService::SetCosmeticFilteringEnabled(bool enabled) {
  local_state_->SetBoolean(prefs::kCosmeticFilteringEnabled, enabled);
}

void RebaAdblockService::SetAutoUpdate(bool enabled) {
  local_state_->SetBoolean(prefs::kAutoUpdate, enabled);
}

void RebaAdblockService::SetShowBlockedCount(bool enabled) {
  local_state_->SetBoolean(prefs::kShowBlockedCount, enabled);
}

void RebaAdblockService::SetContextMenuEnabled(bool enabled) {
  local_state_->SetBoolean(prefs::kContextMenuEnabled, enabled);
}

void RebaAdblockService::SetPopupGuard(bool enabled) {
  local_state_->SetBoolean(prefs::kPopupGuard, enabled);
}

bool RebaAdblockService::ShowBlockedCount() const {
  return local_state_->GetBoolean(prefs::kShowBlockedCount);
}

bool RebaAdblockService::ContextMenuEnabled() const {
  return local_state_->GetBoolean(prefs::kContextMenuEnabled);
}

bool RebaAdblockService::PopupGuardEnabled() const {
  return local_state_->GetBoolean(prefs::kPopupGuard);
}

bool RebaAdblockService::AutoUpdate() const {
  return local_state_->GetBoolean(prefs::kAutoUpdate);
}

// --- Filter lists ---

std::vector<FilterList> RebaAdblockService::CustomLists() const {
  std::vector<FilterList> lists;
  for (const base::Value& entry : local_state_->GetList(prefs::kCustomLists)) {
    const base::DictValue* dict = entry.GetIfDict();
    if (!dict) {
      continue;
    }
    const std::string* url = dict->FindString("url");
    const std::string* title = dict->FindString("title");
    if (!url) {
      continue;
    }
    GURL gurl(*url);
    if (!gurl.is_valid()) {
      continue;
    }
    lists.push_back(MakeCustomFilterList(gurl, title ? *title : std::string()));
  }
  return lists;
}

std::vector<FilterList> RebaAdblockService::EnabledLists() const {
  std::set<std::string> enabled;
  for (const base::Value& id : local_state_->GetList(prefs::kEnabledLists)) {
    if (id.is_string()) {
      enabled.insert(id.GetString());
    }
  }
  std::vector<FilterList> lists;
  for (const FilterList& list : BuiltinFilterLists()) {
    if (enabled.contains(list.id)) {
      lists.push_back(list);
    }
  }
  for (FilterList& list : CustomLists()) {
    if (enabled.contains(list.id)) {
      lists.push_back(std::move(list));
    }
  }
  return lists;
}

std::vector<ListView> RebaAdblockService::GetLists() const {
  std::set<std::string> enabled;
  for (const base::Value& id : local_state_->GetList(prefs::kEnabledLists)) {
    if (id.is_string()) {
      enabled.insert(id.GetString());
    }
  }
  std::vector<ListView> views;
  auto add = [&](const FilterList& list) {
    ListView view;
    view.list = list;
    view.enabled = enabled.contains(list.id);
    view.state = list_manager_->GetState(list.id);
    views.push_back(std::move(view));
  };
  for (const FilterList& list : BuiltinFilterLists()) {
    add(list);
  }
  for (const FilterList& list : CustomLists()) {
    add(list);
  }
  return views;
}

void RebaAdblockService::SaveEnabledLists(const std::set<std::string>& ids) {
  base::ListValue list;
  for (const std::string& id : ids) {
    list.Append(id);
  }
  local_state_->SetList(prefs::kEnabledLists, std::move(list));
}

void RebaAdblockService::SetListEnabled(const std::string& list_id,
                                        bool enabled) {
  std::set<std::string> ids;
  for (const base::Value& id : local_state_->GetList(prefs::kEnabledLists)) {
    if (id.is_string()) {
      ids.insert(id.GetString());
    }
  }
  if (enabled) {
    ids.insert(list_id);
  } else {
    ids.erase(list_id);
  }
  SaveEnabledLists(ids);
}

bool RebaAdblockService::AddCustomList(const GURL& url,
                                       const std::string& title) {
  if (!url.is_valid() || !url.SchemeIs("https")) {
    return false;
  }
  FilterList list = MakeCustomFilterList(url, title);
  for (const FilterList& existing : CustomLists()) {
    if (existing.id == list.id) {
      return false;
    }
  }
  {
    ScopedListPrefUpdate update(local_state_, prefs::kCustomLists);
    base::DictValue entry;
    entry.Set("url", url.spec());
    entry.Set("title", title);
    update->Append(std::move(entry));
  }
  SetListEnabled(list.id, true);
  return true;
}

void RebaAdblockService::RemoveCustomList(const std::string& list_id) {
  // The id comes from a page; only act on lists that are really there, since
  // forgetting a list deletes the file named after it.
  if (!std::ranges::contains(CustomLists(), list_id, &FilterList::id)) {
    return;
  }
  {
    ScopedListPrefUpdate update(local_state_, prefs::kCustomLists);
    base::ListValue kept;
    for (const base::Value& entry : *update) {
      const base::DictValue* dict = entry.GetIfDict();
      const std::string* url = dict ? dict->FindString("url") : nullptr;
      if (url && MakeCustomFilterList(GURL(*url), "").id == list_id) {
        continue;
      }
      kept.Append(entry.Clone());
    }
    *update = std::move(kept);
  }
  SetListEnabled(list_id, false);
  list_manager_->Forget(list_id);
}

void RebaAdblockService::ResetListsToDefault() {
  std::set<std::string> ids;
  for (std::string& id : DefaultEnabledListIds()) {
    ids.insert(std::move(id));
  }
  SaveEnabledLists(ids);
}

void RebaAdblockService::UpdateListsNow() {
  list_manager_->Update(/*force=*/true);
  if (!uses_local_resources_) {
    resources_manager_->Update(/*force=*/true);
  }
}

void RebaAdblockService::UpdateWantedLists() {
  list_manager_->SetWantedLists(EnabledLists());
}

void RebaAdblockService::OnListFilesChanged() {
  RebuildAll();
}

void RebaAdblockService::OnListStatesChanged() {
  for (RebaAdblockServiceObserver& observer : observers_) {
    observer.OnListStatesChanged();
  }
}

// --- My filters and trusted sites ---

std::string RebaAdblockService::GetUserFilters() const {
  return local_state_->GetString(prefs::kUserFilters);
}

bool RebaAdblockService::SetUserFilters(const std::string& text) {
  if (text.size() > kMaxUserFiltersBytes) {
    return false;
  }
  local_state_->SetString(prefs::kUserFilters, text);
  return true;
}

void RebaAdblockService::AddUserFilter(const std::string& rule, bool persist) {
  if (persist) {
    std::string text = GetUserFilters();
    if (AddRule(text, rule)) {
      SetUserFilters(text);
    }
    return;
  }
  if (rule.empty() || rule.find_first_of("\r\n") != std::string::npos ||
      std::ranges::contains(session_rules_, rule)) {
    return;
  }
  size_t session_size = rule.size();
  for (const std::string& existing : session_rules_) {
    session_size += existing.size() + 1;
  }
  if (session_size > kMaxUserFiltersBytes) {
    return;
  }
  session_rules_.push_back(rule);
  OnUserFiltersChanged();
}

std::vector<std::string> RebaAdblockService::GetTrustedSites() const {
  return std::vector<std::string>(trusted_sites_.begin(), trusted_sites_.end());
}

bool RebaAdblockService::AddTrustedSite(const std::string& site, bool persist) {
  const std::string host = NormalizeSiteEntry(site);
  if (host.empty()) {
    return false;
  }
  if (!persist) {
    if (session_trusted_sites_.insert(host).second) {
      NotifySettingsChanged();
    }
    return true;
  }
  std::vector<std::string> sites = GetTrustedSites();
  if (!std::ranges::contains(sites, host)) {
    sites.push_back(host);
    SetTrustedSites(sites);
  }
  return true;
}

void RebaAdblockService::RemoveTrustedSite(const std::string& site) {
  if (session_trusted_sites_.erase(site)) {
    NotifySettingsChanged();
  }
  if (trusted_sites_.contains(site)) {
    std::vector<std::string> sites = GetTrustedSites();
    std::erase(sites, site);
    SetTrustedSites(sites);
  }
}

void RebaAdblockService::SetTrustedSites(
    const std::vector<std::string>& sites) {
  base::ListValue list;
  std::set<std::string> seen;
  for (const std::string& site : sites) {
    const std::string host = NormalizeSiteEntry(site);
    if (!host.empty() && seen.insert(host).second) {
      list.Append(host);
    }
  }
  local_state_->SetList(prefs::kTrustedSites, std::move(list));
}

// --- Blocked pages ---

void RebaAdblockService::AllowDocumentForSession(const std::string& host) {
  const std::string normalized = NormalizeSiteEntry(host);
  if (normalized.empty()) {
    return;
  }
  std::erase(session_rules_, BlockDocumentRule(normalized));
  session_allowed_documents_.insert(normalized);
  OnUserFiltersChanged();
}

void RebaAdblockService::AllowDocumentAlways(const std::string& host) {
  const std::string normalized = NormalizeSiteEntry(host);
  if (normalized.empty()) {
    return;
  }
  bool session_changed = session_allowed_documents_.erase(normalized) > 0;
  session_changed |=
      std::erase(session_rules_, BlockDocumentRule(normalized)) > 0;
  std::string text = GetUserFilters();
  RemoveRule(text, BlockDocumentRule(normalized));
  AddRule(text, AllowDocumentRule(normalized));
  ApplyUserFilterChange(text, session_changed);
}

void RebaAdblockService::AlwaysBlockDocument(const std::string& host) {
  const std::string normalized = NormalizeSiteEntry(host);
  if (normalized.empty()) {
    return;
  }
  bool session_changed = session_allowed_documents_.erase(normalized) > 0;
  session_changed |=
      std::erase(session_rules_, BlockDocumentRule(normalized)) > 0;
  std::string text = GetUserFilters();
  RemoveRule(text, AllowDocumentRule(normalized));
  AddRule(text, BlockDocumentRule(normalized));
  ApplyUserFilterChange(text, session_changed);
}

void RebaAdblockService::BlockDocumentForSession(const std::string& host) {
  const std::string normalized = NormalizeSiteEntry(host);
  if (normalized.empty()) {
    return;
  }
  // A rule in My filters that allows the site still wins over this one; only
  // Local State could change that, and this must not write there.
  session_allowed_documents_.erase(normalized);
  const std::string rule = BlockDocumentRule(normalized);
  if (!std::ranges::contains(session_rules_, rule)) {
    session_rules_.push_back(rule);
  }
  OnUserFiltersChanged();
}

void RebaAdblockService::AllowRequestAlways(const GURL& url,
                                            const std::string& page_host) {
  AddUserFilter(AllowRequestRule(url, page_host), /*persist=*/true);
}

void RebaAdblockService::AllowRequestForSession(const GURL& url,
                                                const std::string& page_host) {
  AddUserFilter(AllowRequestRule(url, page_host), /*persist=*/false);
}

void RebaAdblockService::BlockRequestAlways(const GURL& url,
                                            const std::string& page_host) {
  AddUserFilter(BlockRequestRule(url, page_host), /*persist=*/true);
}

// --- Engines ---

void RebaAdblockService::ApplyUserFilterChange(const std::string& text,
                                               bool session_changed) {
  if (text != GetUserFilters() && SetUserFilters(text)) {
    // OnPrefChanged() recompiles, with the session's rules as they are now.
    return;
  }
  if (session_changed) {
    OnUserFiltersChanged();
  }
}

void RebaAdblockService::OnUserFiltersChanged() {
  // The lists engine is compiled with the exceptions of the user text (see
  // UserListExceptions()) so that they beat the lists' own rules. A change
  // to those needs the lists compiled again; anything else only needs the
  // small user engine.
  if (UserListExceptions(UserEngineText()) != lists_user_exceptions_) {
    RebuildAll();
  } else {
    RebuildUserFilters();
  }
}

std::string RebaAdblockService::UserEngineText() const {
  std::string text = GetUserFilters();
  if (session_allowed_documents_.empty() && session_rules_.empty()) {
    return text;
  }
  if (!text.empty() && !text.ends_with('\n')) {
    text += '\n';
  }
  text += "! For this session only\n";
  for (const std::string& host : session_allowed_documents_) {
    text += AllowDocumentRule(host) + "\n";
  }
  for (const std::string& rule : session_rules_) {
    text += rule + "\n";
  }
  return text;
}

void RebaAdblockService::RebuildAll() {
  if (lists_build_in_flight_) {
    lists_build_pending_ = true;
    return;
  }
  EngineBuildInput input;
  input.user_filters = UserEngineText();
  input.resources_path = resources_path_;
  input.cache_dir = data_dir_.AppendASCII("engine");
  lists_user_exceptions_ = UserListExceptions(input.user_filters);
  for (const FilterList& list : EnabledLists()) {
    const ListFileState state = list_manager_->GetState(list.id);
    if (state.updated == base::Time()) {
      continue;
    }
    ListSource source;
    source.list = list;
    source.path = list_manager_->PathFor(list.id);
    source.content_stamp =
        base::NumberToString(state.size) + "-" +
        base::NumberToString(state.updated.ToInternalValue());
    input.lists.push_back(std::move(source));
  }

  lists_build_in_flight_ = true;
  const uint64_t generation = ++build_generation_;
  std::string user_text = input.user_filters;
  engine_task_runner_->PostTaskAndReplyWithResult(
      FROM_HERE, base::BindOnce(&BuildEngineSet, std::move(input)),
      base::BindOnce(&RebaAdblockService::OnListsEngineBuilt,
                     weak_factory_.GetWeakPtr(), generation,
                     std::move(user_text)));
}

void RebaAdblockService::OnListsEngineBuilt(uint64_t generation,
                                            std::string user_text,
                                            EngineBuildResult result) {
  // A list whose file is gone (deleted by hand or by a cleaning tool) was
  // left out. Forget it was downloaded, so that it is downloaded again.
  if (!result.missing_list_ids.empty()) {
    for (const std::string& list_id : result.missing_list_ids) {
      list_manager_->MarkMissing(list_id);
    }
    if (enabled_ && AutoUpdate()) {
      list_manager_->Update(/*force=*/false);
    }
  }
  OnEnginesBuilt(generation, std::move(user_text), std::move(result.engines));
}

void RebaAdblockService::RebuildUserFilters() {
  if (lists_build_in_flight_) {
    // The result of that build is brought up to date when it arrives.
    return;
  }
  if (!engines_) {
    RebuildAll();
    return;
  }
  const uint64_t generation = ++build_generation_;
  std::string text = UserEngineText();
  engine_task_runner_->PostTaskAndReplyWithResult(
      FROM_HERE,
      base::BindOnce(
          [](scoped_refptr<const EngineSet> engines, std::string text) {
            return engines->WithUserFilters(text);
          },
          engines_, text),
      base::BindOnce(&RebaAdblockService::OnEnginesBuilt,
                     weak_factory_.GetWeakPtr(), generation, text));
}

void RebaAdblockService::OnEnginesBuilt(uint64_t generation,
                                        std::string user_text,
                                        scoped_refptr<EngineSet> engines) {
  const bool was_lists_build =
      lists_build_in_flight_ && generation == build_generation_;
  if (was_lists_build) {
    lists_build_in_flight_ = false;
  }
  if (generation == build_generation_ && engines) {
    InstallEngines(engines);
    if (UserEngineText() != user_text && !lists_build_pending_) {
      // The user filters changed while the lists were compiling.
      RebuildUserFilters();
    }
  }
  if (lists_build_pending_ && !lists_build_in_flight_) {
    lists_build_pending_ = false;
    RebuildAll();
  }
}

void RebaAdblockService::InstallEngines(scoped_refptr<EngineSet> engines) {
  if (!engines_) {
    // Pages no longer wait for the engine; later builds can make way for
    // them.
    engine_task_runner_->UpdatePriority(base::TaskPriority::USER_VISIBLE);
  }
  engines_ = std::move(engines);
  for (RebaAdblockServiceObserver& observer : observers_) {
    observer.OnEnginesChanged();
  }
}

}  // namespace reba_adblock
