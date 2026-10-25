// Copyright 2026 The REBA Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef COMPONENTS_REBA_ADBLOCK_CORE_BLOCKED_REQUEST_LOG_H_
#define COMPONENTS_REBA_ADBLOCK_CORE_BLOCKED_REQUEST_LOG_H_

#include <list>
#include <string>
#include <unordered_map>
#include <vector>

#include "base/time/time.h"

namespace reba_adblock {

// One URL that was blocked, and how often.
struct BlockedRequest {
  BlockedRequest();
  BlockedRequest(const BlockedRequest&);
  BlockedRequest(BlockedRequest&&);
  BlockedRequest& operator=(const BlockedRequest&);
  BlockedRequest& operator=(BlockedRequest&&);
  ~BlockedRequest();

  std::string url;
  // The kind of resource ("script", "image", "sub_frame", ...).
  std::string type;
  // Host of the page that made the request the last time.
  std::string page_host;
  // The filter that blocked it and the title of the list it came from. The list
  // is "My filters" for the user's own rules.
  std::string filter;
  std::string list_title;
  // How many times this URL was blocked while it was in the log.
  int count = 0;
  base::Time last_blocked;
};

// The most recently blocked URLs, each with the number of times it was
// blocked. When it is full the URL that was blocked longest ago is dropped.
// The log is kept in memory only and is not shared between browser runs.
//
// Not thread safe; use it on one sequence.
class BlockedRequestLog {
 public:
  static constexpr size_t kDefaultCapacity = 500;
  // Longer URLs are cut to this many bytes. A page can request URLs that are
  // megabytes long, and a full log of those would hold on to gigabytes. The
  // start is enough to show the URL and to write a filter for it.
  static constexpr size_t kMaxUrlLength = 2048;

  explicit BlockedRequestLog(size_t capacity = kDefaultCapacity);
  BlockedRequestLog(const BlockedRequestLog&) = delete;
  BlockedRequestLog& operator=(const BlockedRequestLog&) = delete;
  ~BlockedRequestLog();

  // Counts a blocked request. `request.count` is ignored, and `request.url` is
  // cut to kMaxUrlLength; URLs that start the same up to there count as one.
  void Record(BlockedRequest request);

  // The entries, most recently blocked first.
  std::vector<BlockedRequest> Snapshot() const;

  size_t size() const { return entries_.size(); }
  // Number of requests blocked in total since the browser started, including
  // those that have fallen out of the log.
  int64_t total_blocked() const { return total_blocked_; }

  void Clear();

 private:
  using Entries = std::list<BlockedRequest>;

  const size_t capacity_;
  // Most recently blocked at the front.
  Entries entries_;
  std::unordered_map<std::string, Entries::iterator> index_;
  int64_t total_blocked_ = 0;
};

}  // namespace reba_adblock

#endif  // COMPONENTS_REBA_ADBLOCK_CORE_BLOCKED_REQUEST_LOG_H_
