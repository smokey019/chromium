// Copyright 2026 The REBA Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_REBA_ADBLOCK_FILTER_LIST_MANAGER_H_
#define CHROME_BROWSER_REBA_ADBLOCK_FILTER_LIST_MANAGER_H_

#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <vector>

#include "base/files/file_path.h"
#include "base/memory/raw_ptr.h"
#include "base/memory/ref_counted.h"
#include "base/memory/weak_ptr.h"
#include "base/time/time.h"
#include "components/reba_adblock/core/filter_list_catalog.h"

class PrefService;

namespace base {
class SequencedTaskRunner;
}

namespace network {
class SharedURLLoaderFactory;
class SimpleURLLoader;
}  // namespace network

namespace reba_adblock {

// What is known about the downloaded copy of a list.
struct ListFileState {
  // Zero if the list was never downloaded.
  base::Time updated;
  int64_t size = 0;
  int rules = 0;
  std::string version;
  // How long after `updated` the list asks to be checked again.
  base::TimeDelta update_interval;
  // Why the last download failed, or empty.
  std::string error;
  // A download is in progress.
  bool updating = false;
  // Time of the last failed download.
  base::Time last_failure;
};

// Downloads the filter lists that are in use, keeps them on disk, and updates
// them when they get old. The files are in `lists_dir`, one per list. What is
// known about each list is kept in Local State.
//
// Lives on the UI thread.
class FilterListManager {
 public:
  class Delegate {
   public:
    // The file of at least one list has changed on disk.
    virtual void OnListFilesChanged() = 0;
    // A download started or ended, or a list's state changed otherwise.
    virtual void OnListStatesChanged() = 0;

   protected:
    ~Delegate() = default;
  };

  FilterListManager(
      const base::FilePath& lists_dir,
      scoped_refptr<network::SharedURLLoaderFactory> url_loader_factory,
      scoped_refptr<base::SequencedTaskRunner> file_task_runner,
      PrefService* local_state,
      Delegate* delegate);
  FilterListManager(const FilterListManager&) = delete;
  FilterListManager& operator=(const FilterListManager&) = delete;
  ~FilterListManager();

  // The lists to keep downloaded. Lists that are not here any more stay on
  // disk but are no longer updated.
  void SetWantedLists(std::vector<FilterList> lists);

  // Downloads the wanted lists that are missing or due. With `force`, all of
  // them.
  void Update(bool force);

  ListFileState GetState(const std::string& list_id) const;
  base::FilePath PathFor(const std::string& list_id) const;
  bool HasFile(const std::string& list_id) const;
  bool IsUpdating() const { return !in_progress_.empty() || !queue_.empty(); }

  // Deletes a list's file and what is known about it, and stops downloading
  // it.
  void Forget(const std::string& list_id);

  // The file of `list_id` turned out to be missing: forgets that it was
  // downloaded, so that the next Update() downloads it again.
  void MarkMissing(const std::string& list_id);

  // What a downloaded list says about itself; worked out off the UI thread.
  struct ListInfo {
    int rules = 0;
    FilterListMetadata metadata;
  };

 private:
  struct Download;

  bool IsDue(const FilterList& list, bool force) const;
  void StartNext();
  void StartDownload(const FilterList& list);
  void TryUrl(const std::string& list_id, size_t url_index);
  void OnDownloaded(const std::string& list_id,
                    size_t url_index,
                    std::optional<std::string> body);
  void OnWritten(const std::string& list_id,
                 ListFileState state,
                 std::optional<ListInfo> info);
  void Fail(const std::string& list_id, const std::string& error);
  // Ends the download of `list_id` and starts the next one.
  void EndDownload(const std::string& list_id);
  void SaveState(const std::string& list_id, const ListFileState& state);

  const base::FilePath lists_dir_;
  scoped_refptr<network::SharedURLLoaderFactory> url_loader_factory_;
  scoped_refptr<base::SequencedTaskRunner> file_task_runner_;
  const raw_ptr<PrefService> local_state_;
  const raw_ptr<Delegate> delegate_;

  std::map<std::string, FilterList> wanted_;
  std::vector<std::string> queue_;
  std::map<std::string, std::unique_ptr<Download>> in_progress_;
  // Lists that changed on disk during this batch of downloads.
  bool files_changed_ = false;

  base::WeakPtrFactory<FilterListManager> weak_factory_{this};
};

}  // namespace reba_adblock

#endif  // CHROME_BROWSER_REBA_ADBLOCK_FILTER_LIST_MANAGER_H_
