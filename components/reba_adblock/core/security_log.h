// Copyright 2026 The REBA Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef COMPONENTS_REBA_ADBLOCK_CORE_SECURITY_LOG_H_
#define COMPONENTS_REBA_ADBLOCK_CORE_SECURITY_LOG_H_

#include <list>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "base/time/time.h"
#include "base/values.h"

namespace reba_adblock {

// What the browser protected the user from. Unlike blocked ad requests, these
// are few and worth keeping for a while.
enum class SecurityEventKind {
  // A pop-up that the pop-up guard or a $popup filter refused.
  kPopup,
  // A download that the download guard held for the user to keep or delete.
  kDownloadHeld,
  // Windows' antivirus found a threat in a file a page fetched or in a
  // download.
  kAntivirus,
  // Safe Browsing warned about a page or a download.
  kSafeBrowsing,
};

std::string_view SecurityEventKindName(SecurityEventKind kind);
std::optional<SecurityEventKind> SecurityEventKindFromName(
    std::string_view name);

struct SecurityEvent {
  SecurityEvent();
  SecurityEvent(const SecurityEvent&);
  SecurityEvent(SecurityEvent&&);
  SecurityEvent& operator=(const SecurityEvent&);
  SecurityEvent& operator=(SecurityEvent&&);
  ~SecurityEvent();

  SecurityEventKind kind = SecurityEventKind::kPopup;
  // The address of what was stopped: the pop-up, the file, the page.
  std::string url;
  // For downloads, the name of the file.
  std::string file_name;
  // The site it happened on.
  std::string page_host;
  // Why, as a short code the settings page turns into words ("guard",
  // "list", "no-click", "other-site-frame", "popup-tab", "download",
  // "page-file", "too-large", "malware", "phishing", ...).
  std::string reason;
  // More about it: the filter and list, the danger type.
  std::string detail;
  // How many times the same thing happened (same kind, address and site).
  int count = 0;
  base::Time first_seen;
  base::Time last_seen;
};

// The security events of the last 30 days (at most 500), most recent first,
// kept on disk by the ad blocker service. Events with the same kind, address
// and site are merged and counted.
class SecurityLog {
 public:
  static constexpr size_t kDefaultCapacity = 500;
  static constexpr base::TimeDelta kMaxAge = base::Days(30);
  // Longer addresses are cut to this many bytes.
  static constexpr size_t kMaxUrlLength = 2048;

  explicit SecurityLog(size_t capacity = kDefaultCapacity);
  SecurityLog(const SecurityLog&) = delete;
  SecurityLog& operator=(const SecurityLog&) = delete;
  ~SecurityLog();

  // Adds an event that happened at `now`. Its count, first_seen and last_seen
  // are set here.
  void Record(SecurityEvent event, base::Time now);

  // The events, most recent first.
  std::vector<SecurityEvent> Snapshot() const;
  size_t size() const { return entries_.size(); }

  void Clear();
  // Forgets the events last seen in [begin, end), for "Clear browsing data".
  void ClearBetween(base::Time begin, base::Time end);
  // Forgets the events last seen more than kMaxAge before `now`.
  void Prune(base::Time now);

  // For the file on disk.
  base::ListValue ToValue() const;
  // Replaces the contents with what ToValue() made. Unknown or damaged entries
  // are skipped, and old ones pruned.
  void LoadFromValue(const base::ListValue& list, base::Time now);

 private:
  using Entries = std::list<SecurityEvent>;
  Entries::iterator Find(const SecurityEvent& event);

  const size_t capacity_;
  // Most recent at the front.
  Entries entries_;
};

}  // namespace reba_adblock

#endif  // COMPONENTS_REBA_ADBLOCK_CORE_SECURITY_LOG_H_
