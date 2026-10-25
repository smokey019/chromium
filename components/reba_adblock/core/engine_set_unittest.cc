// Copyright 2026 The REBA Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "components/reba_adblock/core/engine_set.h"

#include <cstdlib>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "base/base64.h"
#include "base/files/file_enumerator.h"
#include "base/files/file_util.h"
#include "base/logging.h"
#include "base/strings/strcat.h"
#include "base/timer/elapsed_timer.h"
#include "components/reba_adblock/core/filter_list_catalog.h"
#include "components/reba_adblock/core/user_filters.h"
#include "testing/gmock/include/gmock/gmock.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "url/gurl.h"

using ::testing::ElementsAre;
using ::testing::HasSubstr;
using ::testing::IsEmpty;
using ::testing::Not;
using ::testing::SizeIs;
using ::testing::StartsWith;
using ::testing::UnorderedElementsAre;

namespace reba_adblock {

namespace {

// Builds the set the way BuildEngineSet() does, with the user's filters that
// act on the lists' filters compiled in with the lists.
scoped_refptr<EngineSet> MakeSet(std::vector<std::string> list_texts,
                                 const std::string& user_filters = "",
                                 const std::string& resources = "") {
  std::vector<FilterListInput> inputs;
  std::vector<EngineSet::ListInfo> infos;
  int i = 0;
  for (std::string& text : list_texts) {
    inputs.push_back({.text = std::move(text)});
    infos.push_back({.id = "list-" + std::to_string(i),
                     .title = "List " + std::to_string(i)});
    ++i;
  }
  if (std::string exceptions = UserListExceptions(user_filters);
      !exceptions.empty()) {
    inputs.push_back({.text = std::move(exceptions)});
    infos.push_back({.id = kUserFiltersListId, .title = kUserFiltersListTitle});
  }
  std::shared_ptr<const AdblockEngine> engine =
      AdblockEngine::Build(std::move(inputs), resources);
  return EngineSet::Create(std::move(engine), std::move(infos),
                           std::make_shared<const std::string>(resources),
                           user_filters);
}

Decision Check(const EngineSet& set,
               const char* url,
               RequestType type = RequestType::kScript,
               const char* page = "https://news.test/",
               std::string_view method = "GET") {
  return set.CheckRequest(GURL(url), GURL(page), type, method);
}

// A stand-in script for `$redirect` filters (`noopjs`), and a scriptlet that
// sets `window.__reba` (`set-flag`).
std::string TestResources() {
  return base::StrCat(
      {R"([{"name":"noop.js","aliases":["noopjs"],)",
       R"("kind":{"mime":"application/javascript"},"content":")",
       base::Base64Encode("(function() {})();"),
       R"("},{"name":"set-flag.js","aliases":[],"kind":"template",)",
       R"("content":")", base::Base64Encode("window.__reba = {{1}};"),
       R"("}])"});
}

}  // namespace

TEST(EngineSetTest, AttributesBlocksToTheirList) {
  auto set = MakeSet({"||a.test^\n", "||b.test^\n"});
  Decision a = Check(*set, "https://a.test/x.js");
  EXPECT_TRUE(a.block);
  EXPECT_EQ("list-0", a.list_id);
  EXPECT_EQ("List 0", a.list_title);
  EXPECT_EQ("||a.test^", a.filter);

  Decision b = Check(*set, "https://b.test/x.js");
  EXPECT_TRUE(b.block);
  EXPECT_EQ("List 1", b.list_title);

  EXPECT_FALSE(Check(*set, "https://c.test/x.js").block);
}

TEST(EngineSetTest, UserExceptionOverridesListBlock) {
  auto set = MakeSet({"||ads.test^\n"}, "@@||ads.test/ok.js\n");
  EXPECT_FALSE(Check(*set, "https://ads.test/ok.js").block);
  Decision excepted = Check(*set, "https://ads.test/ok.js");
  EXPECT_TRUE(excepted.excepted);
  EXPECT_EQ(kUserFiltersListId, excepted.list_id);
  EXPECT_TRUE(Check(*set, "https://ads.test/bad.js").block);
}

TEST(EngineSetTest, UserBlockWorksOnItsOwn) {
  auto set = MakeSet({"||ads.test^\n"}, "||mine.test^\n");
  Decision d = Check(*set, "https://mine.test/a.js");
  EXPECT_TRUE(d.block);
  EXPECT_EQ(kUserFiltersListTitle, d.list_title);
}

TEST(EngineSetTest, ListExceptionOverridesUserBlock) {
  auto set = MakeSet({"@@||mine.test/fine.js\n"}, "||mine.test^\n");
  EXPECT_FALSE(Check(*set, "https://mine.test/fine.js").block);
  EXPECT_TRUE(Check(*set, "https://mine.test/other.js").block);
}

TEST(EngineSetTest, ListImportantBeatsListExceptions) {
  auto set = MakeSet({"||ads.test^$important\n", "@@||ads.test^\n"});
  EXPECT_TRUE(Check(*set, "https://ads.test/a.js").block);
}

TEST(EngineSetTest, UserExceptionBeatsListImportant) {
  auto set = MakeSet({"||ads.test^$important\n"}, "@@||ads.test^\n");
  Decision d = Check(*set, "https://ads.test/a.js");
  EXPECT_FALSE(d.block);
  EXPECT_TRUE(d.excepted);
  EXPECT_EQ(kUserFiltersListId, d.list_id);
  EXPECT_EQ("@@||ads.test^", d.filter);
}

TEST(EngineSetTest, UserImportantBeatsExceptions) {
  auto set = MakeSet({"@@||mine.test^\n"},
                     "||mine.test^$important\n@@||mine.test/fine.js\n");
  Decision d = Check(*set, "https://mine.test/fine.js");
  EXPECT_TRUE(d.block);
  EXPECT_EQ(kUserFiltersListId, d.list_id);
}

TEST(EngineSetTest, RedirectRuleAloneDoesNotBlock) {
  auto set = MakeSet({"*$script,redirect-rule=noopjs,domain=x.test\n"}, "",
                     TestResources());
  Decision d = Check(*set, "https://cdn.test/a.js", RequestType::kScript,
                     "https://x.test/");
  EXPECT_FALSE(d.block);
  EXPECT_THAT(d.redirect_url, IsEmpty());
}

TEST(EngineSetTest, RedirectRuleAppliesToBlockedRequests) {
  auto set = MakeSet(
      {"*$script,redirect-rule=noopjs,domain=x.test\n", "||cdn.test^\n"}, "",
      TestResources());
  Decision d = Check(*set, "https://cdn.test/a.js", RequestType::kScript,
                     "https://x.test/");
  EXPECT_TRUE(d.block);
  EXPECT_THAT(d.redirect_url, StartsWith("data:application/javascript"));
  EXPECT_EQ("||cdn.test^", d.filter);

  // Without the redirect rule on that site it's a plain block.
  Decision elsewhere = Check(*set, "https://cdn.test/a.js",
                             RequestType::kScript, "https://other.test/");
  EXPECT_TRUE(elsewhere.block);
  EXPECT_THAT(elsewhere.redirect_url, IsEmpty());

  // An exception for the blocking filter leaves the request alone.
  auto excepted = set->WithUserFilters("@@||cdn.test/a.js\n");
  Decision allowed = Check(*excepted, "https://cdn.test/a.js",
                           RequestType::kScript, "https://x.test/");
  EXPECT_FALSE(allowed.block);
  EXPECT_THAT(allowed.redirect_url, IsEmpty());
}

TEST(EngineSetTest, RedirectBlocksAndRedirects) {
  auto set = MakeSet({"||cdn.test/a.js$script,redirect=noopjs\n"}, "",
                     TestResources());
  Decision d = Check(*set, "https://cdn.test/a.js");
  EXPECT_TRUE(d.block);
  EXPECT_THAT(d.redirect_url, StartsWith("data:application/javascript"));
  EXPECT_FALSE(Check(*set, "https://cdn.test/b.js").block);
}

TEST(EngineSetTest, MethodFilters) {
  auto set = MakeSet({"||x.test/api$xhr,method=post\n"});
  EXPECT_TRUE(Check(*set, "https://x.test/api", RequestType::kXhr,
                    "https://news.test/", "POST")
                  .block);
  EXPECT_FALSE(Check(*set, "https://x.test/api", RequestType::kXhr,
                     "https://news.test/", "GET")
                   .block);
}

TEST(EngineSetTest, UserBadfilterTurnsOffAListFilter) {
  auto set =
      MakeSet({"||ads.test^\n||ads.test/x.js\n"}, "||ads.test^$badfilter\n");
  EXPECT_FALSE(Check(*set, "https://ads.test/a.js").block);
  EXPECT_TRUE(Check(*set, "https://ads.test/x.js").block);
}

TEST(EngineSetTest, UserGenerichideAppliesToTheLists) {
  auto set =
      MakeSet({"##.ad-slot\n##div.sponsored\n"}, "@@||x.test^$generichide\n");
  PageCosmetics x = set->GetPageCosmetics(GURL("https://x.test/"));
  EXPECT_TRUE(x.generichide);
  EXPECT_THAT(x.hide_selectors, IsEmpty());

  PageCosmetics other = set->GetPageCosmetics(GURL("https://other.test/"));
  EXPECT_FALSE(other.generichide);
  EXPECT_THAT(other.hide_selectors, SizeIs(1));
}

TEST(EngineSetTest, EachEnginesScriptletsGetTheirOwnBlock) {
  const std::string resources = TestResources();
  const GURL news("https://news.test/");
  auto set = MakeSet({"news.test##+js(set-flag, 1)\n"},
                     "news.test##+js(set-flag, 2)\n", resources);

  const std::string list_script =
      set->lists_engine()->UrlCosmeticResources(news).injected_script;
  std::vector<FilterListInput> user_list;
  user_list.push_back({.text = "news.test##+js(set-flag, 2)\n"});
  const std::string user_script =
      AdblockEngine::Build(std::move(user_list), resources)
          ->UrlCosmeticResources(news)
          .injected_script;
  ASSERT_THAT(list_script, HasSubstr("window.__reba = 1;"));
  ASSERT_THAT(user_script, HasSubstr("window.__reba = 2;"));

  // Both bring their own copies of what they share, which must not clash.
  const std::string expected =
      base::StrCat({"{\n", list_script, "\n}\n{\n", user_script, "\n}\n"});
  EXPECT_EQ(expected, set->GetPageCosmetics(news).injected_script);
}

TEST(EngineSetTest, UserScriptletExceptionsApplyToTheLists) {
  const std::string resources = TestResources();
  const std::string list =
      "x.test##+js(set-flag, 1)\nother.test##+js(set-flag, 1)\n";
  const GURL x("https://x.test/");
  const GURL other("https://other.test/");

  auto one_off = MakeSet({list}, "x.test#@#+js(set-flag, 1)\n", resources);
  EXPECT_THAT(one_off->GetPageCosmetics(x).injected_script, IsEmpty());
  EXPECT_THAT(one_off->GetPageCosmetics(other).injected_script,
              HasSubstr("window.__reba = 1;"));

  // `#@#+js()` turns off every scriptlet, on one site or on all of them.
  auto none_on_x = MakeSet({list}, "x.test#@#+js()\n", resources);
  EXPECT_THAT(none_on_x->GetPageCosmetics(x).injected_script, IsEmpty());
  EXPECT_THAT(none_on_x->GetPageCosmetics(other).injected_script,
              Not(IsEmpty()));
  auto none = MakeSet({list}, "#@#+js()\n", resources);
  EXPECT_THAT(none->GetPageCosmetics(other).injected_script, IsEmpty());
}

TEST(EngineSetTest, BlocksAndAllowsDocuments) {
  auto set = MakeSet({"||bad.test^$document\n"});
  EXPECT_TRUE(Check(*set, "https://bad.test/", RequestType::kMainFrame,
                    "https://bad.test/")
                  .block);
  // `$document` filters leave scripts alone.
  EXPECT_FALSE(Check(*set, "https://bad.test/a.js").block);

  // The "Allow always" button adds an exception for the document.
  auto allowed = set->WithUserFilters("@@||bad.test^$document\n");
  EXPECT_FALSE(Check(*allowed, "https://bad.test/", RequestType::kMainFrame,
                     "https://bad.test/")
                   .block);
}

TEST(EngineSetTest, ChangingUserFiltersKeepsTheLists) {
  auto set = MakeSet({"||ads.test^\n"});
  EXPECT_TRUE(Check(*set, "https://ads.test/a.js").block);
  EXPECT_FALSE(Check(*set, "https://mine.test/a.js").block);

  auto with_user = set->WithUserFilters("||mine.test^\n");
  EXPECT_TRUE(Check(*with_user, "https://ads.test/a.js").block);
  EXPECT_TRUE(Check(*with_user, "https://mine.test/a.js").block);
  EXPECT_EQ(set->lists_engine(), with_user->lists_engine());

  auto without_user = with_user->WithUserFilters("");
  EXPECT_FALSE(Check(*without_user, "https://mine.test/a.js").block);
}

TEST(EngineSetTest, WorksWithoutLists) {
  auto set = EngineSet::Create(nullptr, {}, nullptr, "||mine.test^\n");
  EXPECT_FALSE(set->has_lists());
  EXPECT_TRUE(Check(*set, "https://mine.test/a.js").block);
  EXPECT_FALSE(Check(*set, "https://other.test/a.js").block);

  auto empty = EngineSet::Create(nullptr, {}, nullptr, "");
  EXPECT_FALSE(Check(*empty, "https://mine.test/a.js").block);
}

TEST(EngineSetTest, MergesCosmeticFilters) {
  auto set = MakeSet({"news.test##.from-list\nnews.test##.also-from-list\n"},
                     "news.test##.from-user\nnews.test#@#.also-from-list\n");
  PageCosmetics page = set->GetPageCosmetics(GURL("https://news.test/a"));
  EXPECT_THAT(page.hide_selectors,
              UnorderedElementsAre(".from-list", ".from-user"));
  EXPECT_THAT(set->GetPageCosmetics(GURL("https://other.test/")).hide_selectors,
              IsEmpty());
}

TEST(EngineSetTest, UserCosmeticExceptionAppliesToGenericHiding) {
  auto set = MakeSet({"##.ad-slot\n"}, "news.test#@#.ad-slot\n");
  PageCosmetics news = set->GetPageCosmetics(GURL("https://news.test/"));
  EXPECT_THAT(set->HiddenClassIdSelectors(GURL("https://news.test/"),
                                          {"ad-slot"}, {}, news.generic_exceptions),
              IsEmpty());
  PageCosmetics other = set->GetPageCosmetics(GURL("https://other.test/"));
  EXPECT_THAT(set->HiddenClassIdSelectors(GURL("https://other.test/"),
                                          {"ad-slot"}, {}, other.generic_exceptions),
              ElementsAre(".ad-slot"));
}

TEST(EngineSetTest, CspDirectivesFromBothEngines) {
  auto set = MakeSet({"||site.test^$csp=script-src 'none'\n"},
                     "||site.test^$csp=object-src 'none'\n");
  std::string csp = set->CspDirectives(GURL("https://site.test/"),
                                       GURL("https://site.test/"),
                                       RequestType::kMainFrame);
  EXPECT_NE(std::string::npos, csp.find("script-src 'none'"));
  EXPECT_NE(std::string::npos, csp.find("object-src 'none'"));
}

// With the default filter lists downloaded to a folder (one .txt per list) and,
// optionally, resources.json, set REBA_ADBLOCK_TEST_LISTS_DIR to the folder
// and REBA_ADBLOCK_TEST_RESOURCES to the file to try the real thing.
TEST(EngineSetTest, RealFilterLists) {
  const char* dir = std::getenv("REBA_ADBLOCK_TEST_LISTS_DIR");
  if (!dir) {
    GTEST_SKIP() << "REBA_ADBLOCK_TEST_LISTS_DIR is not set";
  }
  std::string resources;
  if (const char* path = std::getenv("REBA_ADBLOCK_TEST_RESOURCES")) {
    ASSERT_TRUE(base::ReadFileToString(base::FilePath::FromUTF8Unsafe(path),
                                       &resources));
  }

  std::vector<FilterListInput> inputs;
  std::vector<EngineSet::ListInfo> infos;
  base::FileEnumerator files(base::FilePath::FromUTF8Unsafe(dir), false,
                             base::FileEnumerator::FILES,
                             FILE_PATH_LITERAL("*.txt"));
  for (base::FilePath file = files.Next(); !file.empty(); file = files.Next()) {
    std::string text;
    ASSERT_TRUE(base::ReadFileToString(file, &text));
    FilterListInput input;
    input.is_hosts = LooksLikeHostsFile(text);
    input.trusted = file.BaseName().MaybeAsASCII().starts_with("ublock-");
    input.text = std::move(text);
    inputs.push_back(std::move(input));
    infos.push_back({.id = file.BaseName().RemoveExtension().MaybeAsASCII(),
                     .title = file.BaseName().RemoveExtension().MaybeAsASCII()});
  }
  ASSERT_GE(inputs.size(), 3u);

  base::ElapsedTimer timer;
  std::shared_ptr<const AdblockEngine> engine =
      AdblockEngine::Build(std::move(inputs), resources);
  LOG(INFO) << "built " << infos.size() << " lists in "
            << timer.Elapsed().InMilliseconds() << " ms";
  ASSERT_TRUE(engine);
  LOG(INFO) << "serialized size: " << engine->Serialize().size() << " bytes";

  auto set = EngineSet::Create(
      engine, infos, std::make_shared<const std::string>(resources), "");

  auto blocked = [&](const char* url, RequestType type) {
    return set->CheckRequest(GURL(url), GURL("https://news.test/"), type,
                             "GET");
  };
  Decision ga = blocked("https://www.google-analytics.com/analytics.js",
                        RequestType::kScript);
  EXPECT_TRUE(ga.block) << ga.filter;
  LOG(INFO) << "google-analytics: " << ga.list_title << ": " << ga.filter;
  Decision ads = blocked(
      "https://pagead2.googlesyndication.com/pagead/js/adsbygoogle.js",
      RequestType::kScript);
  EXPECT_TRUE(ads.block);
  LOG(INFO) << "adsbygoogle: " << ads.list_title << ": " << ads.filter;
  Decision dc = blocked("https://securepubads.g.doubleclick.net/tag/js/gpt.js",
                        RequestType::kScript);
  EXPECT_TRUE(dc.block);
  Decision plain = blocked("https://cdn.jsdelivr.net/npm/lodash/lodash.min.js",
                           RequestType::kScript);
  EXPECT_FALSE(plain.block) << plain.filter;
  Decision page = set->CheckRequest(GURL("https://news.test/article"),
                                    GURL("https://news.test/article"),
                                    RequestType::kMainFrame, "GET");
  EXPECT_FALSE(page.block) << page.filter;

  PageCosmetics youtube =
      set->GetPageCosmetics(GURL("https://www.youtube.com/watch?v=abc"));
  LOG(INFO) << "youtube: " << youtube.hide_selectors.size()
            << " hide selectors, " << youtube.procedural_actions.size()
            << " procedural, script " << youtube.injected_script.size()
            << " bytes";
  if (!resources.empty()) {
    EXPECT_FALSE(youtube.injected_script.empty());
  }
  PageCosmetics news = set->GetPageCosmetics(GURL("https://www.cnn.com/"));
  LOG(INFO) << "cnn: " << news.hide_selectors.size() << " hide selectors";

  // The user's own cosmetic filters on top of the lists, as in the browser.
  auto with_user = set->WithUserFilters(
      "localhost##div:has-text(SPONSORED_TEST)\n"
      "localhost##+js(set-constant, __adflag, true)\n"
      "localhost##.mine\n");
  PageCosmetics local = with_user->GetPageCosmetics(GURL("http://localhost:18766/"));
  LOG(INFO) << "localhost: hide " << local.hide_selectors.size()
            << " procedural " << local.procedural_actions.size()
            << " script " << local.injected_script.size();
  for (const std::string& p : local.procedural_actions) {
    LOG(INFO) << "  procedural: " << p;
  }
  for (const std::string& h : local.hide_selectors) {
    LOG(INFO) << "  hide: " << h;
  }
  if (const char* dump = std::getenv("REBA_ADBLOCK_TEST_DUMP")) {
    base::WriteFile(base::FilePath::FromUTF8Unsafe(dump),
                    local.injected_script);
  }
  EXPECT_EQ(1u, local.procedural_actions.size());
  EXPECT_FALSE(local.injected_script.empty());

  // Generic hiding by class and id, as the page script asks for it.
  std::vector<std::string> generic = with_user->HiddenClassIdSelectors(
      GURL("http://localhost:18766/"), {"ad-banner-300", "adsbox", "nothing"},
      {"adbanner"}, {});
  for (const std::string& s : generic) {
    LOG(INFO) << "  generic: " << s;
  }
  EXPECT_FALSE(generic.empty());

  // Matching has to be fast.
  base::ElapsedTimer match_timer;
  for (int i = 0; i < 2000; ++i) {
    (void)set->CheckRequest(
        GURL("https://static.example-" + std::to_string(i) + ".com/a/b/c.js"),
        GURL("https://news.test/"), RequestType::kScript, "GET");
  }
  LOG(INFO) << "2000 checks in " << match_timer.Elapsed().InMilliseconds()
            << " ms";
}

}  // namespace reba_adblock
