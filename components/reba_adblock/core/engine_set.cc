// Copyright 2026 The REBA Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "components/reba_adblock/core/engine_set.h"

#include <algorithm>
#include <string_view>
#include <utility>

#include "base/strings/strcat.h"
#include "base/strings/string_util.h"
#include "components/reba_adblock/core/user_filters.h"
#include "third_party/abseil-cpp/absl/container/flat_hash_set.h"
#include "url/gurl.h"

namespace reba_adblock {

Decision::Decision() = default;
Decision::Decision(const Decision&) = default;
Decision::Decision(Decision&&) = default;
Decision& Decision::operator=(const Decision&) = default;
Decision& Decision::operator=(Decision&&) = default;
Decision::~Decision() = default;

PageCosmetics::PageCosmetics() = default;
PageCosmetics::PageCosmetics(const PageCosmetics&) = default;
PageCosmetics::PageCosmetics(PageCosmetics&&) = default;
PageCosmetics& PageCosmetics::operator=(const PageCosmetics&) = default;
PageCosmetics& PageCosmetics::operator=(PageCosmetics&&) = default;
PageCosmetics::~PageCosmetics() = default;

namespace {

// Moves the strings of `from` that aren't in `into` yet to its end. Generic
// selectors come by the thousand, so this uses a hash set, and nothing at all
// when `into` is empty (an engine's own answers have no repeats that matter;
// a selector given twice does no harm).
void AppendUnique(std::vector<std::string>& into,
                  std::vector<std::string> from) {
  if (from.empty()) {
    return;
  }
  if (into.empty()) {
    into = std::move(from);
    return;
  }
  // `into` doesn't reallocate after this, so views of its strings stay valid.
  into.reserve(into.size() + from.size());
  absl::flat_hash_set<std::string_view> seen(into.begin(), into.end());
  for (std::string& s : from) {
    if (!seen.contains(std::string_view(s))) {
      into.push_back(std::move(s));
      seen.insert(std::string_view(into.back()));
    }
  }
}

// `#@#+js()`: no scriptlets at all on the page.
constexpr std::string_view kAllScriptletsException = "+js()";

std::unique_ptr<AdblockEngine> BuildUserEngine(
    const std::string& user_filters,
    const std::string& resources_json) {
  if (base::TrimWhitespaceASCII(user_filters, base::TRIM_ALL).empty()) {
    return nullptr;
  }
  std::vector<FilterListInput> lists;
  lists.push_back({.text = user_filters});
  return AdblockEngine::Build(std::move(lists), resources_json);
}

}  // namespace

// static
scoped_refptr<EngineSet> EngineSet::Create(
    std::shared_ptr<const AdblockEngine> lists_engine,
    std::vector<ListInfo> lists,
    std::shared_ptr<const std::string> resources_json,
    const std::string& user_filters) {
  if (!resources_json) {
    resources_json = std::make_shared<const std::string>();
  }
  std::unique_ptr<AdblockEngine> user_engine =
      BuildUserEngine(user_filters, *resources_json);
  return base::WrapRefCounted(new EngineSet(
      std::move(lists_engine), std::move(lists), std::move(resources_json),
      std::move(user_engine), ParseCosmeticExceptions(user_filters)));
}

EngineSet::EngineSet(
    std::shared_ptr<const AdblockEngine> lists_engine,
    std::vector<ListInfo> lists,
    std::shared_ptr<const std::string> resources_json,
    std::unique_ptr<AdblockEngine> user_engine,
    std::vector<CosmeticExceptionRule> user_cosmetic_exceptions)
    : lists_engine_(std::move(lists_engine)),
      lists_(std::move(lists)),
      resources_json_(std::move(resources_json)),
      user_engine_(std::move(user_engine)),
      user_cosmetic_exceptions_(std::move(user_cosmetic_exceptions)) {}

EngineSet::~EngineSet() = default;

scoped_refptr<EngineSet> EngineSet::WithUserFilters(
    const std::string& user_filters) const {
  return Create(lists_engine_, lists_, resources_json_, user_filters);
}

Decision EngineSet::CheckRequest(const GURL& url,
                                 const GURL& page_url,
                                 RequestType type,
                                 std::string_view method) const {
  Decision decision;
  if (!lists_engine_ && !user_engine_) {
    return decision;
  }

  // Exceptions are looked for even when the engine itself has no blocking
  // filter, because the other engine may have one.
  NetworkResult lists, user;
  if (lists_engine_) {
    lists = lists_engine_->CheckNetwork(url, page_url, type, method,
                                        /*previously_matched=*/false,
                                        /*force_exceptions=*/true);
  }
  if (user_engine_) {
    user = user_engine_->CheckNetwork(url, page_url, type, method,
                                      /*previously_matched=*/false,
                                      /*force_exceptions=*/true);
  }

  // `$important` beats exceptions, as it does within one engine, with one
  // exception: what the user allowed in their own filters stays allowed even
  // if a list has an `$important` filter for it. The user's own `$important`
  // filters still beat everything.
  const bool important =
      user.important || (lists.important && !user.has_exception);
  const bool matched = lists.matched || user.matched;
  const bool excepted = lists.has_exception || user.has_exception;

  // Which engine to credit. The user's filters win when both have something
  // to say.
  const bool credit_user =
      user.matched || (user.has_exception && !lists.matched);
  const NetworkResult& credited = credit_user ? user : lists;

  auto fill_origin = [&](const NetworkResult& r, bool from_user,
                         bool use_exception) {
    decision.filter = use_exception ? r.exception_filter : r.filter;
    const int list_index = use_exception ? r.exception_list : r.filter_list;
    if (from_user) {
      decision.list_id = kUserFiltersListId;
      decision.list_title = kUserFiltersListTitle;
    } else if (list_index >= 0 &&
               static_cast<size_t>(list_index) < lists_.size()) {
      decision.list_id = lists_[list_index].id;
      decision.list_title = lists_[list_index].title;
    }
  };

  if (important) {
    const bool user_important = user.important;
    decision.block = true;
    fill_origin(user_important ? user : lists, user_important,
                /*use_exception=*/false);
  } else if (matched && !excepted) {
    decision.block = true;
    fill_origin(credited, credit_user, /*use_exception=*/false);
  } else if (matched && excepted) {
    decision.excepted = true;
    const bool user_exception = user.has_exception;
    fill_origin(user_exception ? user : lists, user_exception,
                /*use_exception=*/true);
  }

  if (decision.block) {
    // A blocked request may get a stand-in resource instead. A `$redirect`
    // filter blocks as well, so it gets here on its own; a `$redirect-rule`
    // one only names the stand-in, and adblock-rust returns it whether or not
    // anything blocked the request, so it must never block by itself.
    decision.redirect_url =
        !user.redirect.empty() ? user.redirect : lists.redirect;
  } else {
    decision.rewritten_url =
        !user.rewritten_url.empty() ? user.rewritten_url : lists.rewritten_url;
  }
  return decision;
}

std::string EngineSet::CspDirectives(const GURL& url,
                                     const GURL& page_url,
                                     RequestType type) const {
  std::string directives;
  for (const AdblockEngine* engine :
       {static_cast<const AdblockEngine*>(lists_engine_.get()),
        static_cast<const AdblockEngine*>(user_engine_.get())}) {
    if (!engine) {
      continue;
    }
    std::string part = engine->CspDirectives(url, page_url, type);
    if (part.empty()) {
      continue;
    }
    if (!directives.empty()) {
      directives += ", ";
    }
    directives += part;
  }
  return directives;
}

PageCosmetics EngineSet::GetPageCosmetics(const GURL& url) const {
  PageCosmetics out;
  std::vector<std::string> user_exceptions =
      UserCosmeticExceptions(user_cosmetic_exceptions_, url.host());
  // adblock-rust handles `site#@#+js()` itself, but not `#@#+js()` for every
  // site.
  const bool no_scriptlets =
      std::ranges::any_of(user_exceptions, [](const std::string& exception) {
        return exception == kAllScriptletsException;
      });
  for (const AdblockEngine* engine :
       {static_cast<const AdblockEngine*>(lists_engine_.get()),
        static_cast<const AdblockEngine*>(user_engine_.get())}) {
    if (!engine) {
      continue;
    }
    CosmeticResources resources = engine->UrlCosmeticResources(url);
    if (engine == lists_engine_.get()) {
      RemoveExceptedSelectors(resources.hide_selectors, user_exceptions);
    }
    AppendUnique(out.hide_selectors, std::move(resources.hide_selectors));
    AppendUnique(out.procedural_actions,
                 std::move(resources.procedural_actions));
    AppendUnique(out.generic_exceptions, std::move(resources.exceptions));
    if (!no_scriptlets && !resources.injected_script.empty()) {
      // Each engine's scriptlets come with their own copy of the helpers they
      // use (`const scriptletGlobals`, functions). The page runs all of it in
      // one strict mode function, where a name declared twice is a syntax
      // error that stops every scriptlet, so each engine gets its own block.
      base::StrAppend(&out.injected_script,
                      {"{\n", resources.injected_script, "\n}\n"});
    }
    out.generichide |= resources.generichide;
  }
  // The user's own exceptions also apply to generic hiding.
  AppendUnique(out.generic_exceptions, std::move(user_exceptions));
  return out;
}

std::vector<std::string> EngineSet::HiddenClassIdSelectors(
    const GURL& page_url,
    const std::vector<std::string>& classes,
    const std::vector<std::string>& ids,
    const std::vector<std::string>& exceptions) const {
  std::vector<std::string> selectors;
  for (const AdblockEngine* engine :
       {static_cast<const AdblockEngine*>(lists_engine_.get()),
        static_cast<const AdblockEngine*>(user_engine_.get())}) {
    if (engine) {
      AppendUnique(selectors,
                   engine->HiddenClassIdSelectors(classes, ids, exceptions));
    }
  }
  if (!user_cosmetic_exceptions_.empty()) {
    RemoveExceptedSelectors(
        selectors,
        UserCosmeticExceptions(user_cosmetic_exceptions_, page_url.host()));
  }
  return selectors;
}

}  // namespace reba_adblock
