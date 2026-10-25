// Copyright 2026 The REBA Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "components/reba_adblock/engine/adblock_engine.h"

#include <cstdlib>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "base/base64.h"
#include "base/files/file_path.h"
#include "base/files/file_util.h"
#include "testing/gmock/include/gmock/gmock.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "url/gurl.h"

using ::testing::ElementsAre;
using ::testing::HasSubstr;
using ::testing::IsEmpty;
using ::testing::UnorderedElementsAre;

namespace reba_adblock {

namespace {

std::unique_ptr<AdblockEngine> BuildFromList(
    const std::string& text,
    const std::string& resources_json = std::string()) {
  std::vector<FilterListInput> lists;
  lists.push_back({.text = text});
  return AdblockEngine::Build(std::move(lists), resources_json);
}

NetworkResult Check(const AdblockEngine& engine,
                    const char* url,
                    const char* source,
                    RequestType type,
                    std::string_view method = "GET") {
  return engine.CheckNetwork(GURL(url), GURL(source), type, method);
}

}  // namespace

TEST(AdblockEngineTest, BlocksMatchingRequests) {
  auto engine = BuildFromList("||ads.example.com^\n/banner/*$image\n");
  ASSERT_TRUE(engine);

  EXPECT_TRUE(Check(*engine, "https://ads.example.com/x.js",
                    "https://news.test/", RequestType::kScript)
                  .blocked);
  EXPECT_TRUE(Check(*engine, "https://cdn.test/banner/1.png",
                    "https://news.test/", RequestType::kImage)
                  .blocked);
  // `$image` only applies to images.
  EXPECT_FALSE(Check(*engine, "https://cdn.test/banner/1.js",
                     "https://news.test/", RequestType::kScript)
                   .blocked);
  EXPECT_FALSE(Check(*engine, "https://good.example.com/x.js",
                     "https://news.test/", RequestType::kScript)
                   .blocked);
}

TEST(AdblockEngineTest, ExceptionsOverrideBlocks) {
  auto engine = BuildFromList("||ads.example.com^\n@@||ads.example.com/ok.js\n");
  ASSERT_TRUE(engine);

  NetworkResult result = Check(*engine, "https://ads.example.com/ok.js",
                               "https://news.test/", RequestType::kScript);
  EXPECT_FALSE(result.blocked);
  EXPECT_TRUE(result.has_exception);
  EXPECT_EQ("@@||ads.example.com/ok.js", result.exception_filter);

  EXPECT_TRUE(Check(*engine, "https://ads.example.com/bad.js",
                    "https://news.test/", RequestType::kScript)
                  .blocked);
}

TEST(AdblockEngineTest, ImportantBeatsExceptions) {
  auto engine = BuildFromList("||ads.example.com^$important\n@@||ads.example.com^\n");
  ASSERT_TRUE(engine);
  NetworkResult result = Check(*engine, "https://ads.example.com/x.js",
                               "https://news.test/", RequestType::kScript);
  EXPECT_TRUE(result.blocked);
  EXPECT_TRUE(result.important);
}

TEST(AdblockEngineTest, ThirdPartyOption) {
  auto engine = BuildFromList("||tracker.test^$third-party\n");
  ASSERT_TRUE(engine);

  EXPECT_TRUE(Check(*engine, "https://tracker.test/p.gif", "https://news.test/",
                    RequestType::kImage)
                  .blocked);
  // The same site loading its own resources isn't third-party.
  EXPECT_FALSE(Check(*engine, "https://tracker.test/p.gif",
                     "https://www.tracker.test/", RequestType::kImage)
                   .blocked);
}

TEST(AdblockEngineTest, UsesThePublicSuffixList) {
  // `github.io` is a private registry, so two project pages are different
  // sites; `co.uk` is a public suffix, so `a.co.uk` and `b.co.uk` differ too.
  auto engine = BuildFromList("||cdn.test^$third-party\n");
  ASSERT_TRUE(engine);
  EXPECT_TRUE(Check(*engine, "https://cdn.test/a.js", "https://a.co.uk/",
                    RequestType::kScript)
                  .blocked);

  engine = BuildFromList("||a.github.io^$third-party\n");
  ASSERT_TRUE(engine);
  EXPECT_TRUE(Check(*engine, "https://a.github.io/x.js", "https://b.github.io/",
                    RequestType::kScript)
                  .blocked);
  EXPECT_FALSE(Check(*engine, "https://a.github.io/x.js",
                     "https://www.a.github.io/", RequestType::kScript)
                   .blocked);
}

TEST(AdblockEngineTest, DomainOption) {
  auto engine = BuildFromList("/promo.js$domain=news.test|~live.news.test\n");
  ASSERT_TRUE(engine);
  EXPECT_TRUE(Check(*engine, "https://cdn.test/promo.js", "https://news.test/",
                    RequestType::kScript)
                  .blocked);
  EXPECT_FALSE(Check(*engine, "https://cdn.test/promo.js",
                     "https://live.news.test/", RequestType::kScript)
                   .blocked);
  EXPECT_FALSE(Check(*engine, "https://cdn.test/promo.js", "https://other.test/",
                     RequestType::kScript)
                   .blocked);
}

TEST(AdblockEngineTest, ReportsTheListAndRuleThatBlocked) {
  std::vector<FilterListInput> lists;
  lists.push_back({.text = "||first.test^\n"});
  lists.push_back({.text = "! Title: Second\n||second.test^\n"});
  lists.push_back({.text = "0.0.0.0 third.test\n", .is_hosts = true});
  auto engine = AdblockEngine::Build(std::move(lists), std::string());
  ASSERT_TRUE(engine);

  NetworkResult first = Check(*engine, "https://first.test/a.js",
                              "https://x.test/", RequestType::kScript);
  EXPECT_TRUE(first.blocked);
  EXPECT_EQ(0, first.filter_list);
  EXPECT_EQ("||first.test^", first.filter);

  NetworkResult second = Check(*engine, "https://second.test/a.js",
                               "https://x.test/", RequestType::kScript);
  EXPECT_TRUE(second.blocked);
  EXPECT_EQ(1, second.filter_list);
  EXPECT_EQ("||second.test^", second.filter);

  NetworkResult third = Check(*engine, "https://third.test/a.js",
                              "https://x.test/", RequestType::kScript);
  EXPECT_TRUE(third.blocked);
  EXPECT_EQ(2, third.filter_list);
}

TEST(AdblockEngineTest, ExceptionInAnotherListWins) {
  std::vector<FilterListInput> lists;
  lists.push_back({.text = "||ads.test^\n"});
  lists.push_back({.text = "@@||ads.test/fine.js\n"});
  auto engine = AdblockEngine::Build(std::move(lists), std::string());
  ASSERT_TRUE(engine);
  NetworkResult result = Check(*engine, "https://ads.test/fine.js",
                               "https://x.test/", RequestType::kScript);
  EXPECT_FALSE(result.blocked);
  EXPECT_EQ(1, result.exception_list);
}

TEST(AdblockEngineTest, StackedEngineCanExceptWhatTheOtherBlocks) {
  auto lists = BuildFromList("||ads.test^\n");
  auto user = BuildFromList("@@||ads.test^$domain=mine.test\n");
  ASSERT_TRUE(lists);
  ASSERT_TRUE(user);

  const GURL url("https://ads.test/a.js");
  const GURL on_mine("https://mine.test/");
  const GURL elsewhere("https://other.test/");

  EXPECT_TRUE(
      lists->CheckNetwork(url, on_mine, RequestType::kScript, "GET").blocked);

  // Asking the user engine for exceptions only, given that the first engine
  // blocked the request.
  NetworkResult on_mine_result = user->CheckNetwork(
      url, on_mine, RequestType::kScript, "GET", /*previously_matched=*/true,
      /*force_exceptions=*/true);
  EXPECT_TRUE(on_mine_result.has_exception);
  EXPECT_FALSE(on_mine_result.blocked);

  NetworkResult elsewhere_result = user->CheckNetwork(
      url, elsewhere, RequestType::kScript, "GET", /*previously_matched=*/true,
      /*force_exceptions=*/true);
  EXPECT_FALSE(elsewhere_result.has_exception);
}

TEST(AdblockEngineTest, Redirects) {
  const std::string gif = "GIF89a";
  const std::string resources = R"([{"name":"1x1.gif","aliases":["1x1-transparent.gif"],"kind":{"mime":"image/gif"},"content":")" +
                                base::Base64Encode(gif) + R"("}])";
  auto engine = BuildFromList("||ads.test/pixel.gif$redirect=1x1.gif\n", resources);
  ASSERT_TRUE(engine);
  NetworkResult result = Check(*engine, "https://ads.test/pixel.gif",
                               "https://x.test/", RequestType::kImage);
  EXPECT_TRUE(result.blocked);
  EXPECT_EQ("data:image/gif;base64," + base::Base64Encode(gif),
            result.redirect);
}

TEST(AdblockEngineTest, PopupFilters) {
  auto engine = BuildFromList(
      "||poponly.test^$popup\n"
      "||everything.test^\n"
      "/click/*$popup,domain=stream.test\n"
      "||allowed.test^$popup\n"
      "@@||allowed.test^$popup\n"
      "||script-only.test^$script\n");
  ASSERT_TRUE(engine);
  const char kPage[] = "https://stream.test/watch";

  // `$popup` applies to new windows only, not to loads of the page itself.
  EXPECT_TRUE(Check(*engine, "https://poponly.test/a", kPage,
                    RequestType::kPopup)
                  .blocked);
  EXPECT_FALSE(Check(*engine, "https://poponly.test/a", kPage,
                     RequestType::kScript)
                   .blocked);
  EXPECT_FALSE(Check(*engine, "https://poponly.test/a", kPage,
                     RequestType::kImage)
                   .blocked);

  // A rule for a whole host covers pop-ups of it too.
  EXPECT_TRUE(Check(*engine, "https://everything.test/", kPage,
                    RequestType::kPopup)
                  .blocked);

  // `domain=` restricts by the page that opens the window.
  EXPECT_TRUE(Check(*engine, "https://x.test/click/7", kPage,
                    RequestType::kPopup)
                  .blocked);
  EXPECT_FALSE(Check(*engine, "https://x.test/click/7", "https://other.test/",
                     RequestType::kPopup)
                   .blocked);

  // Exceptions work, and a rule for another type doesn't block windows.
  EXPECT_FALSE(Check(*engine, "https://allowed.test/", kPage,
                     RequestType::kPopup)
                   .blocked);
  EXPECT_FALSE(Check(*engine, "https://script-only.test/", kPage,
                     RequestType::kPopup)
                   .blocked);
}

TEST(AdblockEngineTest, RemoveParam) {
  auto engine = BuildFromList("$removeparam=utm_source\n");
  ASSERT_TRUE(engine);
  NetworkResult result =
      Check(*engine, "https://shop.test/p?id=3&utm_source=mail&x=1",
            "https://x.test/", RequestType::kMainFrame);
  EXPECT_FALSE(result.blocked);
  EXPECT_EQ("https://shop.test/p?id=3&x=1", result.rewritten_url);
}

TEST(AdblockEngineTest, CspDirectives) {
  auto engine = BuildFromList("||site.test^$csp=script-src 'none'\n");
  ASSERT_TRUE(engine);
  EXPECT_EQ("script-src 'none'",
            engine->CspDirectives(GURL("https://site.test/"),
                                  GURL("https://site.test/"),
                                  RequestType::kMainFrame));
  EXPECT_EQ("", engine->CspDirectives(GURL("https://other.test/"),
                                      GURL("https://other.test/"),
                                      RequestType::kMainFrame));
}

TEST(AdblockEngineTest, CosmeticFilters) {
  auto engine = BuildFromList(
      "##.ad-banner\n"
      "news.test##.sidebar-promo\n"
      "news.test#@#.ad-banner\n"
      "other.test##div:has-text(Sponsored)\n"
      "example.org##.x:style(opacity: 0.1 !important)\n");
  ASSERT_TRUE(engine);

  // `##.ad-banner` is generic, so it is only found by class/id lookup.
  CosmeticResources news = engine->UrlCosmeticResources(GURL("https://news.test/a"));
  EXPECT_THAT(news.hide_selectors, UnorderedElementsAre(".sidebar-promo"));
  EXPECT_THAT(news.exceptions, UnorderedElementsAre(".ad-banner"));
  EXPECT_THAT(engine->HiddenClassIdSelectors({"ad-banner", "content"}, {},
                                             news.exceptions),
              IsEmpty());
  EXPECT_THAT(engine->HiddenClassIdSelectors({"ad-banner", "content"}, {}, {}),
              ElementsAre(".ad-banner"));

  CosmeticResources other = engine->UrlCosmeticResources(GURL("https://other.test/"));
  ASSERT_EQ(1u, other.procedural_actions.size());
  EXPECT_THAT(other.procedural_actions[0], HasSubstr("has-text"));

  CosmeticResources org = engine->UrlCosmeticResources(GURL("https://example.org/"));
  ASSERT_EQ(1u, org.procedural_actions.size());
  EXPECT_THAT(org.procedural_actions[0], HasSubstr("style"));
}

TEST(AdblockEngineTest, Scriptlets) {
  const std::string scriptlet = "window.__reba = {{1}};";
  const std::string resources =
      R"([{"name":"set-flag.js","aliases":["sf.js"],"kind":"template","content":")" +
      base::Base64Encode(scriptlet) + R"("}])";
  auto engine = BuildFromList("news.test##+js(set-flag, 42)\n", resources);
  ASSERT_TRUE(engine);

  CosmeticResources news = engine->UrlCosmeticResources(GURL("https://news.test/"));
  EXPECT_THAT(news.injected_script, HasSubstr("window.__reba = 42;"));
  EXPECT_TRUE(
      engine->UrlCosmeticResources(GURL("https://other.test/")).injected_script.empty());
}

TEST(AdblockEngineTest, TrustedScriptletsNeedATrustedList) {
  const std::string resources =
      R"([{"name":"trusted-thing.js","aliases":[],"kind":"template","permission":1,"content":")" +
      base::Base64Encode("window.__trusted = true;") + R"("}])";

  std::vector<FilterListInput> untrusted;
  untrusted.push_back({.text = "news.test##+js(trusted-thing)\n"});
  auto untrusted_engine = AdblockEngine::Build(std::move(untrusted), resources);
  ASSERT_TRUE(untrusted_engine);
  EXPECT_TRUE(untrusted_engine->UrlCosmeticResources(GURL("https://news.test/"))
                  .injected_script.empty());

  std::vector<FilterListInput> trusted;
  trusted.push_back({.text = "news.test##+js(trusted-thing)\n", .trusted = true});
  auto trusted_engine = AdblockEngine::Build(std::move(trusted), resources);
  ASSERT_TRUE(trusted_engine);
  EXPECT_THAT(trusted_engine->UrlCosmeticResources(GURL("https://news.test/"))
                  .injected_script,
              HasSubstr("window.__trusted = true;"));
}

// With resources.json made by tools/build_resources.mjs (uBlock Origin's own
// scriptlets), set REBA_ADBLOCK_TEST_RESOURCES to its path to run this.
TEST(AdblockEngineTest, UBlockOriginScriptlets) {
  const char* path = std::getenv("REBA_ADBLOCK_TEST_RESOURCES");
  if (!path) {
    GTEST_SKIP() << "REBA_ADBLOCK_TEST_RESOURCES is not set";
  }
  std::string resources;
  ASSERT_TRUE(base::ReadFileToString(
      base::FilePath::FromUTF8Unsafe(path), &resources));

  std::vector<FilterListInput> lists;
  lists.push_back({.text =
                       "news.test##+js(set-constant, adsEnabled, false)\n"
                       "news.test##+js(json-prune, ad.slots, ad.cfg)\n"
                       "news.test##+js(aopr, trackerInit)\n"
                       "news.test##+js(trusted-set-constant, flag, true)\n"
                       "||ads.test/pixel.gif$image,redirect=1x1.gif\n"
                       "||ads.test/script.js$script,redirect=noop.js\n",
                   .trusted = true});
  auto engine = AdblockEngine::Build(std::move(lists), resources);
  ASSERT_TRUE(engine);

  CosmeticResources news =
      engine->UrlCosmeticResources(GURL("https://news.test/"));
  EXPECT_THAT(news.injected_script, HasSubstr("function setConstant"));
  EXPECT_THAT(news.injected_script, HasSubstr("function jsonPrune"));
  EXPECT_THAT(news.injected_script, HasSubstr("\"adsEnabled\", \"false\""));
  EXPECT_THAT(news.injected_script, HasSubstr("$SCRIPTLET_GLOBALS$"));
  EXPECT_THAT(news.injected_script, HasSubstr("trustedSetConstant"));

  NetworkResult gif = Check(*engine, "https://ads.test/pixel.gif",
                            "https://news.test/", RequestType::kImage);
  EXPECT_TRUE(gif.blocked);
  EXPECT_THAT(gif.redirect, HasSubstr("data:image/gif;base64,"));
  NetworkResult js = Check(*engine, "https://ads.test/script.js",
                           "https://news.test/", RequestType::kScript);
  EXPECT_THAT(js.redirect, HasSubstr("data:application/javascript"));
}

TEST(AdblockEngineTest, SerializationRoundTrip) {
  auto engine = BuildFromList("||ads.test^\nnews.test##.promo\n");
  ASSERT_TRUE(engine);
  std::vector<uint8_t> data = engine->Serialize();
  ASSERT_FALSE(data.empty());

  auto loaded = AdblockEngine::Deserialize(data, std::string());
  ASSERT_TRUE(loaded);
  EXPECT_TRUE(Check(*loaded, "https://ads.test/a.js", "https://x.test/",
                    RequestType::kScript)
                  .blocked);
  EXPECT_THAT(loaded->UrlCosmeticResources(GURL("https://news.test/")).hide_selectors,
              ElementsAre(".promo"));

  EXPECT_FALSE(AdblockEngine::Deserialize(std::vector<uint8_t>{1, 2, 3}, std::string()));
}

TEST(AdblockEngineTest, IgnoresUnsupportedSchemesAndGarbage) {
  auto engine = BuildFromList("||ads.test^\n");
  ASSERT_TRUE(engine);
  EXPECT_FALSE(Check(*engine, "data:text/plain,hello", "https://x.test/",
                     RequestType::kScript)
                   .blocked);
  EXPECT_FALSE(Check(*engine, "chrome://settings", "https://x.test/",
                     RequestType::kMainFrame)
                   .blocked);
  EXPECT_FALSE(
      engine->CheckNetwork(GURL(), GURL(), RequestType::kOther, "GET").blocked);
}

TEST(AdblockEngineTest, MethodOption) {
  auto engine = BuildFromList("||x.test/api$xhr,method=post\n");
  ASSERT_TRUE(engine);
  EXPECT_TRUE(Check(*engine, "https://x.test/api", "https://news.test/",
                    RequestType::kXhr, "POST")
                  .blocked);
  EXPECT_FALSE(Check(*engine, "https://x.test/api", "https://news.test/",
                     RequestType::kXhr, "GET")
                   .blocked);
  // Without a method, `$method` filters don't apply, the others still do.
  EXPECT_FALSE(Check(*engine, "https://x.test/api", "https://news.test/",
                     RequestType::kXhr, "")
                   .blocked);
  auto plain = BuildFromList("||x.test/api$xhr\n");
  ASSERT_TRUE(plain);
  EXPECT_TRUE(Check(*plain, "https://x.test/api", "https://news.test/",
                    RequestType::kXhr, "")
                  .blocked);
  EXPECT_TRUE(Check(*plain, "https://x.test/api", "https://news.test/",
                    RequestType::kXhr, std::string_view("\xff\xfe", 2))
                  .blocked);
}

TEST(AdblockEngineTest, ResourcesThatAreNotUtf8AreIgnored) {
  const std::string bad_resources("[\xff\xfe]", 4);
  auto engine =
      BuildFromList("||ads.test^\nnews.test##.promo\n", bad_resources);
  ASSERT_TRUE(engine);
  EXPECT_TRUE(Check(*engine, "https://ads.test/a.js", "https://x.test/",
                    RequestType::kScript)
                  .blocked);
  EXPECT_THAT(
      engine->UrlCosmeticResources(GURL("https://news.test/")).hide_selectors,
      ElementsAre(".promo"));

  auto loaded = AdblockEngine::Deserialize(engine->Serialize(), bad_resources);
  ASSERT_TRUE(loaded);
  EXPECT_TRUE(Check(*loaded, "https://ads.test/a.js", "https://x.test/",
                    RequestType::kScript)
                  .blocked);
}

TEST(AdblockEngineTest, ParsesALargeListQuickly) {
  std::string text;
  for (int i = 0; i < 50000; ++i) {
    text += "||ad" + std::to_string(i) + ".example.test^$third-party\n";
    text += "/ads/" + std::to_string(i) + "/banner.gif$image\n";
    text += "site" + std::to_string(i) + ".test##.ad-" + std::to_string(i) + "\n";
  }
  auto engine = BuildFromList(text);
  ASSERT_TRUE(engine);
  EXPECT_TRUE(Check(*engine, "https://ad49999.example.test/x", "https://news.test/",
                    RequestType::kScript)
                  .blocked);
  EXPECT_TRUE(Check(*engine, "https://cdn.test/ads/123/banner.gif",
                    "https://news.test/", RequestType::kImage)
                  .blocked);
  EXPECT_FALSE(Check(*engine, "https://cdn.test/ads/123/banner.js",
                     "https://news.test/", RequestType::kScript)
                   .blocked);
}

}  // namespace reba_adblock
