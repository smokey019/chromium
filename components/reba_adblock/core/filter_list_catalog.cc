// Copyright 2026 The REBA Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "components/reba_adblock/core/filter_list_catalog.h"

#include <algorithm>
#include <iterator>

#include "base/containers/span.h"
#include "base/hash/hash.h"
#include "base/no_destructor.h"
#include "base/strings/strcat.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/string_split.h"
#include "base/strings/string_util.h"
#include "url/gurl.h"

namespace reba_adblock {

namespace {

struct BuiltinEntry {
  const char* id;
  const char* title;
  const char* group;
  const char* group2;
  const char* parent;
  const char* urls[6];
  const char* support_url;
  const char* lang;
  bool default_enabled;
};

// Generated; see the header of the file.
const BuiltinEntry kBuiltinEntries[] = {
#include "components/reba_adblock/core/filter_list_catalog_data.inc"
};

}  // namespace

FilterList::FilterList() = default;
FilterList::FilterList(const FilterList&) = default;
FilterList::FilterList(FilterList&&) = default;
FilterList& FilterList::operator=(const FilterList&) = default;
FilterList& FilterList::operator=(FilterList&&) = default;
FilterList::~FilterList() = default;

FilterListMetadata::FilterListMetadata() = default;
FilterListMetadata::FilterListMetadata(const FilterListMetadata&) = default;
FilterListMetadata& FilterListMetadata::operator=(const FilterListMetadata&) =
    default;
FilterListMetadata::~FilterListMetadata() = default;

const std::vector<FilterList>& BuiltinFilterLists() {
  static const base::NoDestructor<std::vector<FilterList>> lists([] {
    std::vector<FilterList> lists;
    for (const BuiltinEntry& entry : kBuiltinEntries) {
      FilterList list;
      list.id = entry.id;
      list.title = entry.title;
      list.group = entry.group;
      list.group2 = entry.group2;
      list.parent = entry.parent;
      for (const char* url : entry.urls) {
        if (url) {
          list.urls.push_back(url);
        }
      }
      list.support_url = entry.support_url;
      list.lang = entry.lang;
      list.default_enabled = entry.default_enabled;
      list.trusted = list.id.starts_with("ublock-");
      lists.push_back(std::move(list));
    }
    return lists;
  }());
  return *lists;
}

const FilterList* FindBuiltinFilterList(std::string_view id) {
  const std::vector<FilterList>& lists = BuiltinFilterLists();
  auto it = std::ranges::find(lists, id, &FilterList::id);
  return it == lists.end() ? nullptr : &*it;
}

std::vector<std::string> DefaultEnabledListIds() {
  std::vector<std::string> ids;
  for (const FilterList& list : BuiltinFilterLists()) {
    if (list.default_enabled) {
      ids.push_back(list.id);
    }
  }
  return ids;
}

FilterList MakeCustomFilterList(const GURL& url, std::string_view title) {
  FilterList list;
  list.id = base::StrCat(
      {"custom-", base::NumberToString(base::PersistentHash(url.spec()))});
  list.title = title.empty() ? url.spec() : std::string(title);
  list.group = "custom";
  list.urls = {url.spec()};
  list.is_custom = true;
  return list;
}

bool LooksLikeHostsFile(std::string_view text) {
  int hosts_lines = 0;
  int other_lines = 0;
  int examined = 0;
  for (std::string_view line :
       base::SplitStringPiece(text, "\n", base::TRIM_WHITESPACE,
                              base::SPLIT_WANT_NONEMPTY)) {
    if (line.starts_with('#') || line.starts_with('!') ||
        line.starts_with('[')) {
      continue;
    }
    if (line.starts_with("0.0.0.0 ") || line.starts_with("127.0.0.1 ") ||
        line.starts_with("0.0.0.0\t") || line.starts_with("127.0.0.1\t") ||
        line.starts_with("::1 ") || line.starts_with("::1\t")) {
      ++hosts_lines;
    } else {
      ++other_lines;
    }
    if (++examined >= 100) {
      break;
    }
  }
  return hosts_lines > 0 && hosts_lines >= 4 * other_lines;
}

FilterListMetadata ParseFilterListMetadata(std::string_view text) {
  FilterListMetadata metadata;
  // Like uBlock Origin, only look at the top of the list.
  text = text.substr(0, 2048);
  for (std::string_view line : base::SplitStringPiece(
           text, "\n", base::TRIM_WHITESPACE, base::SPLIT_WANT_NONEMPTY)) {
    if (!line.starts_with('!') && !line.starts_with('#')) {
      if (line.starts_with('[')) {
        continue;  // "[Adblock Plus 2.0]"
      }
      break;
    }
    line.remove_prefix(1);
    const size_t colon = line.find(':');
    if (colon == std::string_view::npos) {
      continue;
    }
    const std::string_view key =
        base::TrimWhitespaceASCII(line.substr(0, colon), base::TRIM_ALL);
    const std::string_view value =
        base::TrimWhitespaceASCII(line.substr(colon + 1), base::TRIM_ALL);
    if (base::EqualsCaseInsensitiveASCII(key, "title") &&
        metadata.title.empty()) {
      metadata.title = std::string(value);
    } else if (base::EqualsCaseInsensitiveASCII(key, "homepage") &&
               metadata.homepage.empty()) {
      metadata.homepage = std::string(value);
    } else if (base::EqualsCaseInsensitiveASCII(key, "version") &&
               metadata.version.empty()) {
      metadata.version = std::string(value);
    } else if (base::EqualsCaseInsensitiveASCII(key, "redirect") &&
               metadata.redirect.empty()) {
      metadata.redirect = std::string(value);
    } else if (base::EqualsCaseInsensitiveASCII(key, "expires") &&
               !metadata.expires) {
      // "4 days", "12 hours", sometimes with a trailing comment "(update
      // frequency)".
      std::vector<std::string_view> words = base::SplitStringPiece(
          value, " ", base::TRIM_WHITESPACE, base::SPLIT_WANT_NONEMPTY);
      int amount = 0;
      if (words.size() >= 1 && base::StringToInt(words[0], &amount) &&
          amount > 0) {
        const std::string_view unit =
            words.size() >= 2 ? words[1] : std::string_view("days");
        if (unit.starts_with("hour")) {
          metadata.expires = base::Hours(amount);
        } else if (unit.starts_with("day") || words.size() == 1) {
          metadata.expires = base::Days(amount);
        }
      }
    }
  }
  return metadata;
}

base::TimeDelta UpdateIntervalFor(const FilterListMetadata& metadata) {
  constexpr base::TimeDelta kDefault = base::Days(4);
  constexpr base::TimeDelta kMin = base::Hours(6);
  constexpr base::TimeDelta kMax = base::Days(7);
  return std::clamp(metadata.expires.value_or(kDefault), kMin, kMax);
}

}  // namespace reba_adblock
