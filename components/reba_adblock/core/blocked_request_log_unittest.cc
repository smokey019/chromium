// Copyright 2026 The REBA Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "components/reba_adblock/core/blocked_request_log.h"

#include <string>
#include <vector>

#include "testing/gmock/include/gmock/gmock.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace reba_adblock {

namespace {

BlockedRequest Blocked(const std::string& url) {
  BlockedRequest r;
  r.url = url;
  r.type = "script";
  r.filter = "||ads^";
  r.list_title = "EasyList";
  return r;
}

std::vector<std::string> Urls(const BlockedRequestLog& log) {
  std::vector<std::string> urls;
  for (const BlockedRequest& r : log.Snapshot()) {
    urls.push_back(r.url);
  }
  return urls;
}

}  // namespace

TEST(BlockedRequestLogTest, CountsRepeatsAndOrdersByRecency) {
  BlockedRequestLog log(10);
  log.Record(Blocked("https://a.test/1"));
  log.Record(Blocked("https://b.test/1"));
  log.Record(Blocked("https://a.test/1"));
  log.Record(Blocked("https://a.test/1"));

  EXPECT_THAT(Urls(log),
              ::testing::ElementsAre("https://a.test/1", "https://b.test/1"));
  std::vector<BlockedRequest> entries = log.Snapshot();
  EXPECT_EQ(3, entries[0].count);
  EXPECT_EQ(1, entries[1].count);
  EXPECT_EQ(4, log.total_blocked());
  EXPECT_EQ(2u, log.size());
}

TEST(BlockedRequestLogTest, DropsTheLeastRecentlyBlockedWhenFull) {
  BlockedRequestLog log(3);
  log.Record(Blocked("1"));
  log.Record(Blocked("2"));
  log.Record(Blocked("3"));
  // Blocking "1" again makes "2" the oldest.
  log.Record(Blocked("1"));
  log.Record(Blocked("4"));

  EXPECT_THAT(Urls(log), ::testing::ElementsAre("4", "1", "3"));
  EXPECT_EQ(5, log.total_blocked());

  // "2" comes back with a fresh count.
  log.Record(Blocked("2"));
  EXPECT_EQ(1, log.Snapshot()[0].count);
}

TEST(BlockedRequestLogTest, HoldsFiveHundredByDefault) {
  BlockedRequestLog log;
  for (int i = 0; i < 700; ++i) {
    log.Record(Blocked("https://t.test/" + std::to_string(i)));
  }
  EXPECT_EQ(500u, log.size());
  EXPECT_EQ(700, log.total_blocked());
  EXPECT_EQ("https://t.test/699", log.Snapshot().front().url);
  EXPECT_EQ("https://t.test/200", log.Snapshot().back().url);
}

TEST(BlockedRequestLogTest, KeepsOnlyTheStartOfLongUrls) {
  BlockedRequestLog log(10);
  const std::string start = "https://t.test/?q=";
  const std::string long_url = start + std::string(3 * 1024 * 1024, 'a');
  log.Record(Blocked(long_url));
  log.Record(Blocked(long_url + "b"));
  log.Record(Blocked("https://t.test/short"));

  std::vector<BlockedRequest> entries = log.Snapshot();
  ASSERT_EQ(2u, entries.size());
  EXPECT_EQ("https://t.test/short", entries[0].url);
  EXPECT_EQ(BlockedRequestLog::kMaxUrlLength, entries[1].url.size());
  EXPECT_TRUE(entries[1].url.starts_with(start));
  // The two long URLs only differ after the part that is kept.
  EXPECT_EQ(2, entries[1].count);
}

TEST(BlockedRequestLogTest, Clear) {
  BlockedRequestLog log(3);
  log.Record(Blocked("1"));
  log.Clear();
  EXPECT_EQ(0u, log.size());
  log.Record(Blocked("1"));
  EXPECT_EQ(1, log.Snapshot()[0].count);
}

}  // namespace reba_adblock
