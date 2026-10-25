// Copyright 2026 The REBA Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef COMPONENTS_REBA_ADBLOCK_CORE_ENGINE_SET_H_
#define COMPONENTS_REBA_ADBLOCK_CORE_ENGINE_SET_H_

#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "base/memory/ref_counted.h"
#include "components/reba_adblock/core/user_filters.h"
#include "components/reba_adblock/engine/adblock_engine.h"

class GURL;

namespace reba_adblock {

// Name shown for the user's own filters.
inline constexpr char kUserFiltersListId[] = "user-filters";
inline constexpr char kUserFiltersListTitle[] = "My filters";

// What to do with a network request.
struct Decision {
  Decision();
  Decision(const Decision&);
  Decision(Decision&&);
  Decision& operator=(const Decision&);
  Decision& operator=(Decision&&);
  ~Decision();

  // Fail the request.
  bool block = false;
  // Serve this `data:` URL instead of the real resource.
  std::string redirect_url;
  // Load this URL instead (tracking parameters removed).
  std::string rewritten_url;
  // A filter wanted to block the request but an exception let it through.
  bool excepted = false;
  // The filter (and the list it is in) that blocked the request, or that
  // excepted it when `excepted` is set.
  std::string filter;
  std::string list_id;
  std::string list_title;
};

// What to do to a page that is loading.
struct PageCosmetics {
  PageCosmetics();
  PageCosmetics(const PageCosmetics&);
  PageCosmetics(PageCosmetics&&);
  PageCosmetics& operator=(const PageCosmetics&);
  PageCosmetics& operator=(PageCosmetics&&);
  ~PageCosmetics();

  std::vector<std::string> hide_selectors;
  std::vector<std::string> procedural_actions;
  std::vector<std::string> generic_exceptions;
  std::string injected_script;
  bool generichide = false;
};

// Everything that decides what gets blocked: the compiled filter lists, and a
// small engine for the user's own filters, kept apart so changing them
// doesn't mean compiling the lists again.
//
// Immutable, and safe to use from any thread.
class EngineSet : public base::RefCountedThreadSafe<EngineSet> {
 public:
  struct ListInfo {
    std::string id;
    std::string title;
  };

  // `lists_engine` may be null while the lists are still loading. `lists` has
  // one entry for each list given to the engine, in order. The lines of
  // `user_filters` that act on the lists' filters (UserListExceptions()) are
  // expected to be compiled into `lists_engine` as well, as its last list,
  // with kUserFiltersListId as its id.
  static scoped_refptr<EngineSet> Create(
      std::shared_ptr<const AdblockEngine> lists_engine,
      std::vector<ListInfo> lists,
      std::shared_ptr<const std::string> resources_json,
      const std::string& user_filters);

  // The same lists with different user filters. Compiles only the user
  // filters.
  scoped_refptr<EngineSet> WithUserFilters(const std::string& user_filters) const;

  // Decides about a request for `url` from a page at `page_url` (the top level
  // page). `method` is the HTTP method ("GET", "POST", ...).
  Decision CheckRequest(const GURL& url,
                        const GURL& page_url,
                        RequestType type,
                        std::string_view method) const;

  // `$csp` directives to add to the response of a document request.
  std::string CspDirectives(const GURL& url,
                            const GURL& page_url,
                            RequestType type) const;

  PageCosmetics GetPageCosmetics(const GURL& url) const;

  std::vector<std::string> HiddenClassIdSelectors(
      const GURL& page_url,
      const std::vector<std::string>& classes,
      const std::vector<std::string>& ids,
      const std::vector<std::string>& exceptions) const;

  bool has_lists() const { return !!lists_engine_; }

  // The compiled lists, to share with an EngineSet that has other user filters.
  std::shared_ptr<const AdblockEngine> lists_engine() const {
    return lists_engine_;
  }

 private:
  friend class base::RefCountedThreadSafe<EngineSet>;

  EngineSet(std::shared_ptr<const AdblockEngine> lists_engine,
            std::vector<ListInfo> lists,
            std::shared_ptr<const std::string> resources_json,
            std::unique_ptr<AdblockEngine> user_engine,
            std::vector<CosmeticExceptionRule> user_cosmetic_exceptions);
  ~EngineSet();

  std::shared_ptr<const AdblockEngine> lists_engine_;
  std::vector<ListInfo> lists_;
  std::shared_ptr<const std::string> resources_json_;
  std::unique_ptr<AdblockEngine> user_engine_;
  // The user's `domains#@#selector` rules. adblock-rust doesn't take them
  // without a site, so EngineSet applies them to what the engines return.
  std::vector<CosmeticExceptionRule> user_cosmetic_exceptions_;
};

}  // namespace reba_adblock

#endif  // COMPONENTS_REBA_ADBLOCK_CORE_ENGINE_SET_H_
