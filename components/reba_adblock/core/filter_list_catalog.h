// Copyright 2026 The REBA Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef COMPONENTS_REBA_ADBLOCK_CORE_FILTER_LIST_CATALOG_H_
#define COMPONENTS_REBA_ADBLOCK_CORE_FILTER_LIST_CATALOG_H_

#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "base/time/time.h"

class GURL;

namespace reba_adblock {

// A filter list the user can turn on, from the built-in catalog (the lists
// uBlock Origin offers) or added by URL.
struct FilterList {
  FilterList();
  FilterList(const FilterList&);
  FilterList(FilterList&&);
  FilterList& operator=(const FilterList&);
  FilterList& operator=(FilterList&&);
  ~FilterList();

  // Stable identifier, used in prefs and file names. "ublock-filters",
  // "easylist", or "custom-<hash of url>" for lists added by the user.
  std::string id;
  std::string title;
  // Section of the filter lists page: "default", "ads", "privacy", "malware",
  // "multipurpose", "annoyances" or "regions". Custom lists use "custom".
  std::string group;
  // Sub-section within "annoyances" (for example "cookies"), or empty.
  std::string group2;
  // Name of the family the list belongs to, shown as a prefix, or empty.
  std::string parent;
  // Where to download from, in order of preference.
  std::vector<std::string> urls;
  std::string support_url;
  // Space separated language codes for regional lists.
  std::string lang;
  bool default_enabled = false;
  // May use trusted scriptlets (lists maintained by uBlock Origin itself).
  bool trusted = false;
  bool is_custom = false;
};

// The lists uBlock Origin ships with. The first call builds the vector; it is
// not destroyed.
const std::vector<FilterList>& BuiltinFilterLists();

// Looks `id` up in the built-in catalog. Null if it isn't there.
const FilterList* FindBuiltinFilterList(std::string_view id);

// Ids of the lists that are on by default.
std::vector<std::string> DefaultEnabledListIds();

// The id and description for a list added by the user from `url`.
FilterList MakeCustomFilterList(const GURL& url, std::string_view title);

// Whether `text` is a hosts file ("0.0.0.0 example.com") and not a list in
// the Adblock Plus syntax.
bool LooksLikeHostsFile(std::string_view text);

// What the header comments of a list say about it.
struct FilterListMetadata {
  FilterListMetadata();
  FilterListMetadata(const FilterListMetadata&);
  FilterListMetadata& operator=(const FilterListMetadata&);
  ~FilterListMetadata();

  std::string title;
  std::string homepage;
  std::string version;
  // How long the list says it can be used before checking for a new one
  // ("! Expires: 4 days").
  std::optional<base::TimeDelta> expires;
  // Where the list says it moved to ("! Redirect: <url>").
  std::string redirect;
};

FilterListMetadata ParseFilterListMetadata(std::string_view text);

// When to check a list for updates, given what it says: the list's own
// suggestion limited to a sensible range.
base::TimeDelta UpdateIntervalFor(const FilterListMetadata& metadata);

}  // namespace reba_adblock

#endif  // COMPONENTS_REBA_ADBLOCK_CORE_FILTER_LIST_CATALOG_H_
