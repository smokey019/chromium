// Copyright 2026 The REBA Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef COMPONENTS_REBA_ADBLOCK_CORE_USER_FILTERS_H_
#define COMPONENTS_REBA_ADBLOCK_CORE_USER_FILTERS_H_

#include <set>
#include <string>
#include <string_view>
#include <vector>

class GURL;

namespace reba_adblock {

// Helpers around the user's own filters ("My filters") and the list of sites
// they trust.

// Turns what a person typed or pasted ("example.com", "https://example.com/a",
// "*.example.com", "  Example.COM ") into a lower case host name, or an empty
// string if it isn't one.
std::string NormalizeSiteEntry(std::string_view input);

// Whether `host` is in `sites`, or is a subdomain of a host in it.
bool HostMatchesSites(std::string_view host,
                      const std::set<std::string, std::less<>>& sites);

// Filters that let a document through / keep blocking it, for the blocked page
// buttons. They block or allow the page with this host name and its
// subdomains.
std::string AllowDocumentRule(std::string_view host);
std::string BlockDocumentRule(std::string_view host);

// A filter that lets `url` through. With a non empty `page_host`, only on
// pages of that site.
std::string AllowRequestRule(const GURL& url, std::string_view page_host);

// A filter that blocks `url`, optionally only on pages of `page_host`.
std::string BlockRequestRule(const GURL& url, std::string_view page_host);

// Adds `rule` as a new line of `text` unless it is already there. Returns
// whether the text changed.
bool AddRule(std::string& text, std::string_view rule);

// Removes every line of `text` that is exactly `rule`. Returns whether the text
// changed.
bool RemoveRule(std::string& text, std::string_view rule);

// One of the user's `domains#@#selector` rules. `domains` is empty for a rule
// that applies everywhere.
struct CosmeticExceptionRule {
  std::string domains;
  std::string selector;
};

// The `domains#@#selector` rules in the user's filters, parsed once so the
// text doesn't have to be read again for every page.
std::vector<CosmeticExceptionRule> ParseCosmeticExceptions(
    std::string_view user_text);

// Selectors that the user's `host#@#selector` rules take out of the hide rules
// of the lists, on the page of `hostname`.
std::vector<std::string> UserCosmeticExceptions(
    const std::vector<CosmeticExceptionRule>& rules,
    std::string_view hostname);
std::vector<std::string> UserCosmeticExceptions(std::string_view user_text,
                                                std::string_view hostname);

// Applies the exceptions, removing the selectors from `selectors`.
void RemoveExceptedSelectors(std::vector<std::string>& selectors,
                             const std::vector<std::string>& exceptions);

// The lines of the user's filters that have to be compiled together with the
// filter lists to act on the lists' own filters: `$badfilter`, `@@` rules
// with `$generichide`, `$redirect`, `$redirect-rule` or `$csp`, and cosmetic
// exceptions for given sites (`site#@#+js(...)`, `site#@#div:has-text(...)`).
// Trimmed, sorted and without duplicates, one per line, so a change to the
// user's filters that leaves this the same doesn't need the lists compiled
// again. Empty if there are none.
std::string UserListExceptions(std::string_view user_filters);

}  // namespace reba_adblock

#endif  // COMPONENTS_REBA_ADBLOCK_CORE_USER_FILTERS_H_
