// Copyright 2026 The REBA Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_REBA_ADBLOCK_REBA_ADBLOCK_SERVICE_H_
#define CHROME_BROWSER_REBA_ADBLOCK_REBA_ADBLOCK_SERVICE_H_

#include <memory>
#include <set>
#include <string>
#include <string_view>
#include <vector>

#include "base/files/file_path.h"
#include "base/memory/raw_ptr.h"
#include "base/memory/ref_counted.h"
#include "base/memory/weak_ptr.h"
#include "base/observer_list.h"
#include "base/observer_list_types.h"
#include "base/timer/timer.h"
#include "chrome/browser/reba_adblock/filter_list_manager.h"
#include "chrome/browser/reba_adblock/resources_manager.h"
#include "components/prefs/pref_change_registrar.h"
#include "components/reba_adblock/core/blocked_request_log.h"
#include "components/reba_adblock/core/engine_set.h"
#include "components/reba_adblock/core/filter_list_catalog.h"
#include "services/network/public/cpp/shared_url_loader_factory.h"
#include "url/gurl.h"

class PrefService;

namespace base {
class SequencedTaskRunner;
class UpdateableSequencedTaskRunner;
}  // namespace base

namespace network {
class SharedURLLoaderFactory;
}

namespace reba_adblock {

struct EngineBuildResult;

// The most My filters may hold. More is refused, so a page (or a paste) can't
// make Local State and the user engine grow without bounds.
inline constexpr size_t kMaxUserFiltersBytes = 1024 * 1024;

class RebaAdblockServiceObserver : public base::CheckedObserver {
 public:
  // New compiled lists or user filters are in use.
  virtual void OnEnginesChanged() {}
  // A list was downloaded, started or failed downloading.
  virtual void OnListStatesChanged() {}
  // A setting changed.
  virtual void OnSettingsChanged() {}
  // Requests were blocked since the last time this was called.
  virtual void OnBlockedRequestsLogged() {}
  // The service is being destroyed. Observers must stop observing it here.
  virtual void OnAdblockServiceShutdown() = 0;
};

// What the filter lists page shows for one list.
struct ListView {
  FilterList list;
  bool enabled = false;
  ListFileState state;
};

// The ad blocker: owns the compiled filter lists, keeps them up to date, and
// answers "should this be blocked". There is one for the whole browser; use
// Get(). Lives on the UI thread, except for engine compilation.
class RebaAdblockService final : public FilterListManager::Delegate {
 public:
  // The running service, or null before startup and after shutdown.
  static RebaAdblockService* Get();

  RebaAdblockService(
      PrefService* local_state,
      const base::FilePath& data_dir,
      scoped_refptr<network::SharedURLLoaderFactory> url_loader_factory);
  RebaAdblockService(const RebaAdblockService&) = delete;
  RebaAdblockService& operator=(const RebaAdblockService&) = delete;
  ~RebaAdblockService();

  // Loads what is on disk and starts downloading what is missing.
  void Start();

  void AddObserver(RebaAdblockServiceObserver* observer);
  void RemoveObserver(RebaAdblockServiceObserver* observer);

  // --- Blocking ---

  bool IsEnabled() const { return enabled_; }
  bool IsCosmeticFilteringEnabled() const {
    return enabled_ && cosmetic_enabled_;
  }
  // The setting, whatever the main switch says.
  bool CosmeticFilteringSetting() const { return cosmetic_enabled_; }
  // Whether the user turned the ad blocker off for the page at `page_url`,
  // for good or (from an off-the-record window) until the browser closes.
  bool IsSiteTrusted(const GURL& page_url) const;

  // The compiled filters. Null until the first engine is built.
  scoped_refptr<const EngineSet> engines() const { return engines_; }

  // Whether the lists are being compiled (again).
  bool IsBuilding() const { return lists_build_in_flight_; }

  // Whether the ad blocker is on but the first engine is still being loaded,
  // as at startup. Pages that start loading now would go unfiltered.
  bool IsWaitingForFirstEngine() const {
    return enabled_ && !engines_ && lists_build_in_flight_;
  }

  // Decides what to do with a request, honouring the on/off switch and trusted
  // sites. `method` is the request's HTTP method. Doesn't log anything; see
  // RecordBlocked().
  Decision CheckRequest(const GURL& url,
                        const GURL& page_url,
                        RequestType type,
                        std::string_view method) const;

  // Counts and logs a blocked request. With `incognito`, the address is not
  // logged (the log can be read from any window), only counted.
  void RecordBlocked(const GURL& url,
                     RequestType type,
                     const GURL& page_url,
                     const Decision& decision,
                     bool incognito = false);

  // The blocked requests log, most recent first.
  std::vector<BlockedRequest> GetBlockedRequests() const {
    return log_.Snapshot();
  }
  void ClearBlockedRequests();
  // Requests blocked since the browser was first run.
  int64_t total_blocked() const { return total_blocked_; }

  // --- Settings ---

  void SetEnabled(bool enabled);
  void SetCosmeticFilteringEnabled(bool enabled);
  void SetAutoUpdate(bool enabled);
  void SetShowBlockedCount(bool enabled);
  void SetContextMenuEnabled(bool enabled);
  void SetPopupGuard(bool enabled);
  bool ShowBlockedCount() const;
  bool ContextMenuEnabled() const;
  // Whether pages are stopped from opening pop-ups on clicks that are not on a
  // link or control for them, whatever the filter lists say.
  bool PopupGuardEnabled() const;
  bool AutoUpdate() const;

  // Filter lists.
  std::vector<ListView> GetLists() const;
  void SetListEnabled(const std::string& list_id, bool enabled);
  // Adds a list by URL and turns it on. Returns false if the URL is not a valid
  // https URL or the list is there already.
  bool AddCustomList(const GURL& url, const std::string& title);
  void RemoveCustomList(const std::string& list_id);
  // Downloads all enabled lists again.
  void UpdateListsNow();
  bool IsUpdatingLists() const { return list_manager_->IsUpdating(); }
  // Turns on the lists that are on by default, and no others.
  void ResetListsToDefault();

  // My filters. SetUserFilters() returns false, and changes nothing, if `text`
  // is longer than kMaxUserFiltersBytes.
  std::string GetUserFilters() const;
  bool SetUserFilters(const std::string& text);

  // Trusted sites. GetTrustedSites() returns the ones kept in Local State, not
  // those trusted for the session.
  std::vector<std::string> GetTrustedSites() const;
  // Returns false if `site` is not a host name. Without `persist` (for
  // off-the-record windows) the site is trusted in memory, until the browser
  // closes.
  bool AddTrustedSite(const std::string& site, bool persist);
  // Removes `site` from both the saved and the session's trusted sites.
  void RemoveTrustedSite(const std::string& site);
  void SetTrustedSites(const std::vector<std::string>& sites);

  // --- Blocked pages and the request log ---

  // The answers on the page that says a website is blocked.
  void AllowDocumentForSession(const std::string& host);
  void AllowDocumentAlways(const std::string& host);
  void AlwaysBlockDocument(const std::string& host);
  // AlwaysBlockDocument() for off-the-record windows: kept in memory.
  void BlockDocumentForSession(const std::string& host);

  // Turns a blocked request in the log into a rule in My filters.
  void AllowRequestAlways(const GURL& url, const std::string& page_host);
  void AllowRequestForSession(const GURL& url, const std::string& page_host);
  void BlockRequestAlways(const GURL& url, const std::string& page_host);

  // Adds a rule to My filters (a `##selector` from the element picker, for
  // example). Without `persist` (for off-the-record windows) the rule is only
  // kept in memory, until the browser closes. Does nothing if My filters would
  // grow past kMaxUserFiltersBytes.
  void AddUserFilter(const std::string& rule, bool persist);

 private:
  // FilterListManager::Delegate:
  void OnListFilesChanged() override;
  void OnListStatesChanged() override;

  void OnPrefChanged(const std::string& pref_name);
  void NotifySettingsChanged();
  void LoadSettings();
  void UpdateWantedLists();
  std::vector<FilterList> EnabledLists() const;
  std::vector<FilterList> CustomLists() const;
  void SaveEnabledLists(const std::set<std::string>& ids);
  // The text compiled into the user engine: My filters plus the rules of the
  // session.
  std::string UserEngineText() const;
  // Saves `text` as My filters if it changed, and recompiles what that and a
  // change to the session's rules (`session_changed`) call for.
  void ApplyUserFilterChange(const std::string& text, bool session_changed);
  // My filters or the session's rules changed: recompiles what is needed.
  void OnUserFiltersChanged();

  // Update checks for the lists and the resources, while the ad blocker is on
  // and updates automatically.
  void OnStartupDelayPassed();
  void UpdateAutoUpdates();
  void CheckForUpdates();

  // Compiles lists in the background and swaps the result in.
  void RebuildAll();
  // Recompiles only the user filters.
  void RebuildUserFilters();
  void OnListsEngineBuilt(uint64_t generation,
                          std::string user_text,
                          EngineBuildResult result);
  void OnEnginesBuilt(uint64_t generation,
                      std::string user_text,
                      scoped_refptr<EngineSet> engines);
  void InstallEngines(scoped_refptr<EngineSet> engines);

  void NotifyBlockedRequestsSoon();
  void FlushBlockedNotification();
  void SaveTotalBlocked();

  const raw_ptr<PrefService> local_state_;
  const base::FilePath data_dir_;
  // The resources file the engines use: the signed copy that is downloaded,
  // or the --reba-adblock-resources-file one while that is given.
  base::FilePath resources_path_;
  bool uses_local_resources_ = false;
  // Downloads and other file chores.
  scoped_refptr<base::SequencedTaskRunner> file_task_runner_;
  // Compiling and loading engines. Pages wait for the first one, so it runs at
  // USER_BLOCKING, and later ones at USER_VISIBLE.
  scoped_refptr<base::UpdateableSequencedTaskRunner> engine_task_runner_;

  std::unique_ptr<FilterListManager> list_manager_;
  std::unique_ptr<ResourcesManager> resources_manager_;
  PrefChangeRegistrar pref_registrar_;
  base::ObserverList<RebaAdblockServiceObserver> observers_;

  scoped_refptr<const EngineSet> engines_;
  uint64_t build_generation_ = 0;
  bool lists_build_in_flight_ = false;
  bool lists_build_pending_ = false;
  // UserListExceptions() of the user text that the lists engine was last
  // asked to be built with.
  std::string lists_user_exceptions_;

  bool startup_delay_passed_ = false;
  base::RepeatingTimer update_timer_;

  bool enabled_ = true;
  bool cosmetic_enabled_ = true;
  std::set<std::string, std::less<>> trusted_sites_;
  // Sites trusted from off-the-record windows, until the browser closes.
  std::set<std::string, std::less<>> session_trusted_sites_;
  // Documents let through until the browser closes.
  std::set<std::string> session_allowed_documents_;
  // Rules added for the session: from the request log, the blocked page, or
  // off-the-record windows.
  std::vector<std::string> session_rules_;

  BlockedRequestLog log_;
  int64_t total_blocked_ = 0;
  int64_t saved_total_blocked_ = 0;
  bool blocked_notification_scheduled_ = false;
  base::RepeatingTimer save_total_timer_;

  base::WeakPtrFactory<RebaAdblockService> weak_factory_{this};
};

}  // namespace reba_adblock

#endif  // CHROME_BROWSER_REBA_ADBLOCK_REBA_ADBLOCK_SERVICE_H_
