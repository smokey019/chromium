// Copyright 2026 The REBA Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "components/reba_adblock/core/user_filters.h"

#include <set>
#include <string>
#include <vector>

#include "testing/gmock/include/gmock/gmock.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "url/gurl.h"

using ::testing::ElementsAre;
using ::testing::IsEmpty;

namespace reba_adblock {

TEST(UserFiltersTest, NormalizeSiteEntry) {
  EXPECT_EQ("example.com", NormalizeSiteEntry("example.com"));
  EXPECT_EQ("example.com", NormalizeSiteEntry("  Example.COM "));
  EXPECT_EQ("example.com", NormalizeSiteEntry("https://example.com/a/b?c#d"));
  EXPECT_EQ("example.com", NormalizeSiteEntry("*.example.com"));
  EXPECT_EQ("example.com", NormalizeSiteEntry("http://user:pw@example.com:8080/"));
  EXPECT_EQ("example.com", NormalizeSiteEntry("example.com."));
  EXPECT_EQ("localhost", NormalizeSiteEntry("localhost"));
  EXPECT_EQ("", NormalizeSiteEntry(""));
  EXPECT_EQ("", NormalizeSiteEntry("not a host"));
  EXPECT_EQ("", NormalizeSiteEntry("exa mple.com"));
  EXPECT_EQ("", NormalizeSiteEntry("<script>"));
}

TEST(UserFiltersTest, HostMatchesSites) {
  const std::set<std::string, std::less<>> sites = {"example.com", "a.b.test"};
  EXPECT_TRUE(HostMatchesSites("example.com", sites));
  EXPECT_TRUE(HostMatchesSites("www.example.com", sites));
  EXPECT_TRUE(HostMatchesSites("x.y.example.com", sites));
  EXPECT_TRUE(HostMatchesSites("a.b.test", sites));
  EXPECT_FALSE(HostMatchesSites("b.test", sites));
  EXPECT_FALSE(HostMatchesSites("notexample.com", sites));
  EXPECT_FALSE(HostMatchesSites("example.com.evil.test", sites));
  EXPECT_FALSE(HostMatchesSites("", sites));
  EXPECT_FALSE(HostMatchesSites("example.com", {}));
}

TEST(UserFiltersTest, Rules) {
  EXPECT_EQ("@@||bad.test^$document", AllowDocumentRule("bad.test"));
  EXPECT_EQ("||bad.test^$document", BlockDocumentRule("bad.test"));
  EXPECT_EQ("@@||cdn.test/a/b.js?x=1",
            AllowRequestRule(GURL("https://cdn.test/a/b.js?x=1#frag"), ""));
  EXPECT_EQ("@@||cdn.test:8443/a.js$domain=news.test",
            AllowRequestRule(GURL("https://cdn.test:8443/a.js"), "news.test"));
  EXPECT_EQ("||cdn.test/a.js", BlockRequestRule(GURL("https://cdn.test/a.js"), ""));
}

TEST(UserFiltersTest, RulesDontTakeFilterSyntaxFromTheUrl) {
  // `$` would start the options, so `/x$document,important` must not become
  // `@@||cdn.test/x$document,important`.
  EXPECT_EQ(
      "@@||cdn.test/x",
      AllowRequestRule(GURL("https://cdn.test/x$document,important"), ""));
  EXPECT_EQ(
      "@@||cdn.test/x$domain=news.test",
      AllowRequestRule(GURL("https://cdn.test/x$important"), "news.test"));
  EXPECT_EQ("||cdn.test/a",
            BlockRequestRule(GURL("https://cdn.test/a*b.js"), ""));
  EXPECT_EQ("||cdn.test/a.js?x=",
            BlockRequestRule(GURL("https://cdn.test/a.js?x=$csp=x"), ""));
  // Hosts may contain `$`.
  EXPECT_EQ("||ads*important.test/a.js",
            BlockRequestRule(GURL("https://ads$important.test/a.js"), ""));
}

TEST(UserFiltersTest, AddAndRemoveRules) {
  std::string text;
  EXPECT_TRUE(AddRule(text, "||a.test^"));
  EXPECT_EQ("||a.test^\n", text);
  EXPECT_FALSE(AddRule(text, "||a.test^"));
  EXPECT_TRUE(AddRule(text, "||b.test^"));
  EXPECT_EQ("||a.test^\n||b.test^\n", text);

  text = "! my notes\n||a.test^\n##.x\n";
  EXPECT_TRUE(RemoveRule(text, "||a.test^"));
  EXPECT_EQ("! my notes\n##.x\n", text);
  EXPECT_FALSE(RemoveRule(text, "||a.test^"));

  // No trailing newline stays that way.
  text = "||a.test^\n||b.test^";
  EXPECT_TRUE(RemoveRule(text, "||a.test^"));
  EXPECT_EQ("||b.test^", text);
}

TEST(UserFiltersTest, CosmeticExceptions) {
  const std::string text =
      "! comment\n"
      "##.generic\n"
      "news.test#@#.promo\n"
      "other.test#@#.not-for-news\n"
      "a.test,b.test#@#.multi\n"
      "test,~news.test#@#.except-news\n"
      "#@#.everywhere\n";
  EXPECT_THAT(UserCosmeticExceptions(text, "news.test"),
              ElementsAre(".promo", ".everywhere"));
  EXPECT_THAT(UserCosmeticExceptions(text, "www.news.test"),
              ElementsAre(".promo", ".everywhere"));
  EXPECT_THAT(UserCosmeticExceptions(text, "b.test"),
              ElementsAre(".multi", ".except-news", ".everywhere"));
  EXPECT_THAT(UserCosmeticExceptions("", "news.test"), IsEmpty());
}

TEST(UserFiltersTest, CosmeticExceptionsForEntities) {
  EXPECT_THAT(UserCosmeticExceptions("google.*#@#.ad\n", "google.com"),
              ElementsAre(".ad"));
  EXPECT_THAT(UserCosmeticExceptions("google.*#@#.ad\n", "www.google.co.uk"),
              ElementsAre(".ad"));
  EXPECT_THAT(UserCosmeticExceptions("google.*#@#.ad\n", "notgoogle.com"),
              IsEmpty());
}

TEST(UserFiltersTest, ParsedCosmeticExceptions) {
  const std::vector<CosmeticExceptionRule> rules = ParseCosmeticExceptions(
      "news.test#@#.promo\n"
      "##.not-an-exception\n"
      "#@#.everywhere\n");
  ASSERT_EQ(2u, rules.size());
  EXPECT_EQ("news.test", rules[0].domains);
  EXPECT_EQ(".promo", rules[0].selector);
  EXPECT_EQ("", rules[1].domains);
  EXPECT_THAT(UserCosmeticExceptions(rules, "www.news.test"),
              ElementsAre(".promo", ".everywhere"));
  EXPECT_THAT(UserCosmeticExceptions(rules, "other.test"),
              ElementsAre(".everywhere"));
}

TEST(UserFiltersTest, RemoveExceptedSelectors) {
  std::vector<std::string> selectors = {".a", ".b", ".c"};
  RemoveExceptedSelectors(selectors, {".b", ".zzz"});
  EXPECT_THAT(selectors, ElementsAre(".a", ".c"));
}

TEST(UserFiltersTest, UserListExceptions) {
  EXPECT_EQ(
      "@@||a.test^$generichide\n"
      "@@||cdn.test/a.js$redirect-rule=noopjs\n"
      "news.test#@#.promo\n"
      "x.test#@#+js(set-flag, 1)\n"
      "x.test#@#div:has-text(Ad)\n"
      "||ads.test^$badfilter\n",
      UserListExceptions("! comment\n"
                         "||ads.test^$badfilter\n"
                         "||mine.test^\n"
                         "@@||ok.test^\n"
                         "@@||ok.test^$document\n"
                         "news.test##.ad\n"
                         "news.test#@#.promo\n"
                         "  x.test#@#+js(set-flag, 1)  \r\n"
                         "@@||a.test^$generichide\n"
                         "@@||cdn.test/a.js$redirect-rule=noopjs\n"
                         "x.test#@#div:has-text(Ad)\n"
                         "#@#.for-every-site\n"
                         "||a.test^$generichide\n"
                         "||ads.test^$badfilter\n"));
  EXPECT_EQ("", UserListExceptions("||a.test^\n##.ad\n@@||b.test^\n"));
  EXPECT_EQ("", UserListExceptions(""));
}

}  // namespace reba_adblock
