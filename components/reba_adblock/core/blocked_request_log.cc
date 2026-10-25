// Copyright 2026 The REBA Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "components/reba_adblock/core/blocked_request_log.h"

#include <utility>

#include "base/check_op.h"
#include "base/strings/string_util.h"

namespace reba_adblock {

BlockedRequest::BlockedRequest() = default;
BlockedRequest::BlockedRequest(const BlockedRequest&) = default;
BlockedRequest::BlockedRequest(BlockedRequest&&) = default;
BlockedRequest& BlockedRequest::operator=(const BlockedRequest&) = default;
BlockedRequest& BlockedRequest::operator=(BlockedRequest&&) = default;
BlockedRequest::~BlockedRequest() = default;

BlockedRequestLog::BlockedRequestLog(size_t capacity) : capacity_(capacity) {
  CHECK_GT(capacity_, 0u);
}

BlockedRequestLog::~BlockedRequestLog() = default;

void BlockedRequestLog::Record(BlockedRequest request) {
  ++total_blocked_;

  if (request.url.size() > kMaxUrlLength) {
    request.url.resize(
        base::TruncateUTF8ToByteSize(request.url, kMaxUrlLength).size());
  }

  auto it = index_.find(request.url);
  if (it != index_.end()) {
    // Seen before: move to the front, keeping the count going.
    request.count = it->second->count + 1;
    entries_.erase(it->second);
    index_.erase(it);
  } else {
    request.count = 1;
  }

  entries_.push_front(std::move(request));
  index_[entries_.front().url] = entries_.begin();

  if (entries_.size() > capacity_) {
    index_.erase(entries_.back().url);
    entries_.pop_back();
  }
}

std::vector<BlockedRequest> BlockedRequestLog::Snapshot() const {
  return std::vector<BlockedRequest>(entries_.begin(), entries_.end());
}

void BlockedRequestLog::Clear() {
  entries_.clear();
  index_.clear();
}

}  // namespace reba_adblock
