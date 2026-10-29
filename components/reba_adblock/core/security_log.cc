// Copyright 2026 The REBA Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "components/reba_adblock/core/security_log.h"

#include <algorithm>
#include <utility>

#include "base/check_op.h"
#include "base/json/values_util.h"
#include "base/strings/string_util.h"

namespace reba_adblock {

namespace {

constexpr struct {
  SecurityEventKind kind;
  std::string_view name;
} kKindNames[] = {
    {SecurityEventKind::kPopup, "popup"},
    {SecurityEventKind::kDownloadHeld, "download"},
    {SecurityEventKind::kAntivirus, "antivirus"},
    {SecurityEventKind::kSafeBrowsing, "safebrowsing"},
};

// Strings from a file that may have been edited stay within reason.
std::string Cut(std::string text, size_t max) {
  if (text.size() > max) {
    text.resize(base::TruncateUTF8ToByteSize(text, max).size());
  }
  return text;
}

}  // namespace

std::string_view SecurityEventKindName(SecurityEventKind kind) {
  for (const auto& entry : kKindNames) {
    if (entry.kind == kind) {
      return entry.name;
    }
  }
  return "popup";
}

std::optional<SecurityEventKind> SecurityEventKindFromName(
    std::string_view name) {
  for (const auto& entry : kKindNames) {
    if (entry.name == name) {
      return entry.kind;
    }
  }
  return std::nullopt;
}

SecurityEvent::SecurityEvent() = default;
SecurityEvent::SecurityEvent(const SecurityEvent&) = default;
SecurityEvent::SecurityEvent(SecurityEvent&&) = default;
SecurityEvent& SecurityEvent::operator=(const SecurityEvent&) = default;
SecurityEvent& SecurityEvent::operator=(SecurityEvent&&) = default;
SecurityEvent::~SecurityEvent() = default;

SecurityLog::SecurityLog(size_t capacity) : capacity_(capacity) {
  CHECK_GT(capacity_, 0u);
}

SecurityLog::~SecurityLog() = default;

SecurityLog::Entries::iterator SecurityLog::Find(const SecurityEvent& event) {
  return std::ranges::find_if(entries_, [&](const SecurityEvent& e) {
    return e.kind == event.kind && e.url == event.url &&
           e.page_host == event.page_host;
  });
}

void SecurityLog::Record(SecurityEvent event, base::Time now) {
  event.url = Cut(std::move(event.url), kMaxUrlLength);
  event.file_name = Cut(std::move(event.file_name), 512);
  event.page_host = Cut(std::move(event.page_host), 256);
  event.detail = Cut(std::move(event.detail), 1024);
  event.last_seen = now;

  auto it = Find(event);
  if (it != entries_.end()) {
    // Seen before: move to the front, keeping the count going.
    event.count = it->count + 1;
    event.first_seen = it->first_seen;
    entries_.erase(it);
  } else {
    event.count = 1;
    event.first_seen = now;
  }
  entries_.push_front(std::move(event));
  while (entries_.size() > capacity_) {
    entries_.pop_back();
  }
  Prune(now);
}

std::vector<SecurityEvent> SecurityLog::Snapshot() const {
  return std::vector<SecurityEvent>(entries_.begin(), entries_.end());
}

void SecurityLog::Clear() {
  entries_.clear();
}

void SecurityLog::ClearBetween(base::Time begin, base::Time end) {
  std::erase_if(entries_, [&](const SecurityEvent& e) {
    return e.last_seen >= begin && e.last_seen < end;
  });
}

void SecurityLog::Prune(base::Time now) {
  const base::Time oldest = now - kMaxAge;
  std::erase_if(entries_,
                [&](const SecurityEvent& e) { return e.last_seen < oldest; });
}

base::ListValue SecurityLog::ToValue() const {
  base::ListValue list;
  for (const SecurityEvent& e : entries_) {
    list.Append(base::DictValue()
                    .Set("kind", SecurityEventKindName(e.kind))
                    .Set("url", e.url)
                    .Set("file", e.file_name)
                    .Set("site", e.page_host)
                    .Set("reason", e.reason)
                    .Set("detail", e.detail)
                    .Set("count", e.count)
                    .Set("first", base::TimeToValue(e.first_seen))
                    .Set("last", base::TimeToValue(e.last_seen)));
  }
  return list;
}

void SecurityLog::LoadFromValue(const base::ListValue& list, base::Time now) {
  entries_.clear();
  for (const base::Value& value : list) {
    const base::DictValue* dict = value.GetIfDict();
    if (!dict) {
      continue;
    }
    const std::string* kind_name = dict->FindString("kind");
    const std::string* url = dict->FindString("url");
    std::optional<SecurityEventKind> kind =
        kind_name ? SecurityEventKindFromName(*kind_name) : std::nullopt;
    std::optional<base::Time> first =
        dict->Find("first") ? base::ValueToTime(*dict->Find("first"))
                            : std::nullopt;
    std::optional<base::Time> last =
        dict->Find("last") ? base::ValueToTime(*dict->Find("last"))
                           : std::nullopt;
    if (!kind || !url || !first || !last) {
      continue;
    }
    SecurityEvent event;
    event.kind = *kind;
    event.url = Cut(*url, kMaxUrlLength);
    if (const std::string* s = dict->FindString("file")) {
      event.file_name = Cut(*s, 512);
    }
    if (const std::string* s = dict->FindString("site")) {
      event.page_host = Cut(*s, 256);
    }
    if (const std::string* s = dict->FindString("reason")) {
      event.reason = Cut(*s, 64);
    }
    if (const std::string* s = dict->FindString("detail")) {
      event.detail = Cut(*s, 1024);
    }
    event.count = std::max(1, dict->FindInt("count").value_or(1));
    event.first_seen = *first;
    event.last_seen = *last;
    entries_.push_back(std::move(event));
    if (entries_.size() >= capacity_) {
      break;
    }
  }
  // The file is written most recent first, but don't rely on it.
  entries_.sort([](const SecurityEvent& a, const SecurityEvent& b) {
    return a.last_seen > b.last_seen;
  });
  Prune(now);
}

}  // namespace reba_adblock
