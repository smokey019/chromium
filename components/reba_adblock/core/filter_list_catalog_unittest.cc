// Copyright 2026 The REBA Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "components/reba_adblock/core/filter_list_catalog.h"

#include <set>
#include <string>

#include "base/time/time.h"
#include "testing/gmock/include/gmock/gmock.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "url/gurl.h"

using ::testing::IsEmpty;
using ::testing::UnorderedElementsAre;

namespace reba_adblock {

TEST(FilterListCatalogTest, BuiltinListsAreSane) {
  const std::vector<FilterList>& lists = BuiltinFilterLists();
  EXPECT_GT(lists.size(), 50u);
  std::set<std::string> ids;
  for (const FilterList& list : lists) {
    EXPECT_FALSE(list.id.empty());
    EXPECT_FALSE(list.title.empty()) << list.id;
    EXPECT_FALSE(list.group.empty()) << list.id;
    EXPECT_FALSE(list.urls.empty()) << list.id;
    for (const std::string& url : list.urls) {
      EXPECT_TRUE(GURL(url).SchemeIs("https")) << list.id << " " << url;
    }
    EXPECT_TRUE(ids.insert(list.id).second) << "duplicate id " << list.id;
    EXPECT_FALSE(list.is_custom);
  }
}

TEST(FilterListCatalogTest, DefaultsAreUBlockOriginsDefaults) {
  EXPECT_THAT(DefaultEnabledListIds(),
              UnorderedElementsAre("ublock-filters", "ublock-badware",
                                   "ublock-privacy", "ublock-unbreak",
                                   "ublock-quick-fixes", "easylist",
                                   "easyprivacy", "urlhaus-1", "plowe-0"));
}

TEST(FilterListCatalogTest, TrustedLists) {
  const FilterList* ublock = FindBuiltinFilterList("ublock-filters");
  ASSERT_TRUE(ublock);
  EXPECT_TRUE(ublock->trusted);
  const FilterList* easylist = FindBuiltinFilterList("easylist");
  ASSERT_TRUE(easylist);
  EXPECT_FALSE(easylist->trusted);
  EXPECT_FALSE(FindBuiltinFilterList("no-such-list"));
}

TEST(FilterListCatalogTest, CustomLists) {
  FilterList a = MakeCustomFilterList(GURL("https://lists.test/a.txt"), "Mine");
  FilterList b = MakeCustomFilterList(GURL("https://lists.test/b.txt"), "");
  EXPECT_TRUE(a.is_custom);
  EXPECT_EQ("Mine", a.title);
  EXPECT_EQ("https://lists.test/b.txt", b.title);
  EXPECT_NE(a.id, b.id);
  EXPECT_EQ(a.id,
            MakeCustomFilterList(GURL("https://lists.test/a.txt"), "x").id);
  EXPECT_TRUE(a.id.starts_with("custom-"));
}

TEST(FilterListCatalogTest, DetectsHostsFiles) {
  EXPECT_TRUE(LooksLikeHostsFile(
      "# comment\n127.0.0.1 localhost\n0.0.0.0 ads.test\n0.0.0.0 track.test\n"
      "0.0.0.0 more.test\n0.0.0.0 evenmore.test\n"));
  EXPECT_FALSE(LooksLikeHostsFile("! Title: x\n||ads.test^\n##.ad\n"));
  EXPECT_FALSE(LooksLikeHostsFile(""));
  EXPECT_FALSE(LooksLikeHostsFile("0.0.0.0 a.test\n||b.test^\n||c.test^\n"));
}

TEST(FilterListCatalogTest, ParsesMetadata) {
  FilterListMetadata m = ParseFilterListMetadata(
      "[Adblock Plus 2.0]\n"
      "! Title: My List\n"
      "! Homepage: https://lists.test/\n"
      "! Version: 202610221200\n"
      "! Expires: 2 days (update frequency)\n"
      "! Redirect: https://lists.test/new.txt\n"
      "||ads.test^\n"
      "! Title: not this one\n");
  EXPECT_EQ("My List", m.title);
  EXPECT_EQ("https://lists.test/", m.homepage);
  EXPECT_EQ("202610221200", m.version);
  EXPECT_EQ("https://lists.test/new.txt", m.redirect);
  ASSERT_TRUE(m.expires);
  EXPECT_EQ(base::Days(2), *m.expires);
  EXPECT_EQ(base::Days(2), UpdateIntervalFor(m));
}

TEST(FilterListCatalogTest, ParsesHourlyExpiry) {
  FilterListMetadata m = ParseFilterListMetadata("! Expires: 12 hours\n");
  ASSERT_TRUE(m.expires);
  EXPECT_EQ(base::Hours(12), *m.expires);
}

TEST(FilterListCatalogTest, UpdateIntervalIsClamped) {
  FilterListMetadata none;
  EXPECT_EQ(base::Days(4), UpdateIntervalFor(none));

  FilterListMetadata fast = ParseFilterListMetadata("! Expires: 1 hour\n");
  EXPECT_EQ(base::Hours(6), UpdateIntervalFor(fast));

  FilterListMetadata slow = ParseFilterListMetadata("! Expires: 30 days\n");
  EXPECT_EQ(base::Days(7), UpdateIntervalFor(slow));
}

TEST(FilterListCatalogTest, NoMetadataInFilterLines) {
  FilterListMetadata m = ParseFilterListMetadata("||ads.test^\n! Title: late\n");
  EXPECT_TRUE(m.title.empty());
  EXPECT_THAT(m.redirect, IsEmpty());
}

}  // namespace reba_adblock
