// Copyright 2026 The REBA Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "components/reba_adblock/core/security_log.h"

#include "base/strings/string_number_conversions.h"
#include "base/time/time.h"
#include "base/values.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace reba_adblock {

namespace {

SecurityEvent Event(SecurityEventKind kind,
                    const std::string& url,
                    const std::string& site = "site.test") {
  SecurityEvent event;
  event.kind = kind;
  event.url = url;
  event.page_host = site;
  event.reason = "guard";
  return event;
}

base::Time T(int minutes) {
  return base::Time::FromSecondsSinceUnixEpoch(1'800'000'000) +
         base::Minutes(minutes);
}

}  // namespace

TEST(SecurityLogTest, MergesTheSameEventAndCountsIt) {
  SecurityLog log;
  log.Record(Event(SecurityEventKind::kPopup, "https://ad.test/"), T(0));
  log.Record(Event(SecurityEventKind::kPopup, "https://other.test/"), T(1));
  log.Record(Event(SecurityEventKind::kPopup, "https://ad.test/"), T(2));
  // Same address, other kind or other site: separate events.
  log.Record(Event(SecurityEventKind::kAntivirus, "https://ad.test/"), T(3));
  log.Record(Event(SecurityEventKind::kPopup, "https://ad.test/", "b.test"),
             T(4));

  std::vector<SecurityEvent> events = log.Snapshot();
  ASSERT_EQ(4u, events.size());
  EXPECT_EQ("b.test", events[0].page_host);
  EXPECT_EQ(SecurityEventKind::kAntivirus, events[1].kind);
  EXPECT_EQ("https://ad.test/", events[2].url);
  EXPECT_EQ(2, events[2].count);
  EXPECT_EQ(T(0), events[2].first_seen);
  EXPECT_EQ(T(2), events[2].last_seen);
  EXPECT_EQ("https://other.test/", events[3].url);
}

TEST(SecurityLogTest, KeepsAtMostItsCapacity) {
  SecurityLog log(3);
  for (int i = 0; i < 5; ++i) {
    log.Record(Event(SecurityEventKind::kPopup,
                     "https://ad" + base::NumberToString(i) + ".test/"),
               T(i));
  }
  std::vector<SecurityEvent> events = log.Snapshot();
  ASSERT_EQ(3u, events.size());
  EXPECT_EQ("https://ad4.test/", events[0].url);
  EXPECT_EQ("https://ad2.test/", events[2].url);
}

TEST(SecurityLogTest, ForgetsEventsOlderThanThirtyDays) {
  SecurityLog log;
  log.Record(Event(SecurityEventKind::kPopup, "https://old.test/"), T(0));
  log.Record(Event(SecurityEventKind::kPopup, "https://new.test/"),
             T(0) + base::Days(29));
  log.Prune(T(0) + base::Days(31));
  std::vector<SecurityEvent> events = log.Snapshot();
  ASSERT_EQ(1u, events.size());
  EXPECT_EQ("https://new.test/", events[0].url);
}

TEST(SecurityLogTest, ClearsATimeRange) {
  SecurityLog log;
  log.Record(Event(SecurityEventKind::kPopup, "https://a.test/"), T(0));
  log.Record(Event(SecurityEventKind::kPopup, "https://b.test/"), T(10));
  log.Record(Event(SecurityEventKind::kPopup, "https://c.test/"), T(20));
  log.ClearBetween(T(5), T(15));
  std::vector<SecurityEvent> events = log.Snapshot();
  ASSERT_EQ(2u, events.size());
  EXPECT_EQ("https://c.test/", events[0].url);
  EXPECT_EQ("https://a.test/", events[1].url);
  log.Clear();
  EXPECT_EQ(0u, log.size());
}

TEST(SecurityLogTest, SavesAndLoadsAndSkipsDamagedEntries) {
  SecurityLog log;
  SecurityEvent download =
      Event(SecurityEventKind::kDownloadHeld, "https://files.test/setup.zip");
  download.file_name = "setup.zip";
  download.reason = "no-click";
  download.detail = "x";
  log.Record(download, T(0));
  log.Record(download, T(1));
  log.Record(Event(SecurityEventKind::kSafeBrowsing, "https://bad.test/"),
             T(2));

  base::ListValue saved = log.ToValue();
  saved.Append(base::DictValue().Set("kind", "nonsense").Set("url", "x"));
  saved.Append("not a dict");

  SecurityLog loaded;
  loaded.LoadFromValue(saved, T(3));
  std::vector<SecurityEvent> events = loaded.Snapshot();
  ASSERT_EQ(2u, events.size());
  EXPECT_EQ(SecurityEventKind::kSafeBrowsing, events[0].kind);
  EXPECT_EQ(SecurityEventKind::kDownloadHeld, events[1].kind);
  EXPECT_EQ("setup.zip", events[1].file_name);
  EXPECT_EQ("no-click", events[1].reason);
  EXPECT_EQ(2, events[1].count);
  EXPECT_EQ(T(0), events[1].first_seen);
  EXPECT_EQ(T(1), events[1].last_seen);

  // Loading a month later drops everything.
  SecurityLog later;
  later.LoadFromValue(saved, T(0) + base::Days(40));
  EXPECT_EQ(0u, later.size());
}

}  // namespace reba_adblock
