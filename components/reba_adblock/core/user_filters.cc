// Copyright 2026 The REBA Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "components/reba_adblock/core/user_filters.h"

#include <algorithm>
#include <set>
#include <string_view>

#include "base/strings/strcat.h"
#include "base/strings/string_split.h"
#include "base/strings/string_util.h"
#include "url/gurl.h"

namespace reba_adblock {

namespace {

bool IsValidHostChar(char c) {
  return base::IsAsciiAlphaNumeric(c) || c == '-' || c == '.' || c == '_';
}

// Characters with a meaning in the pattern of a network filter: `$` starts the
// options, `*` and `^` are wildcards and `|` is an anchor. Filters have no way
// to escape them.
constexpr std::string_view kPatternSyntaxChars = "$*^|";

// `||host/path?query` for a URL, without the scheme, fragment and any
// credentials. A page can request URLs with the characters above in them
// (`/x$document,important`), and copying those would let the URL add options
// or wildcards to the user's filter. Hosts may contain `$`, which becomes the
// wildcard `*`; the path is cut before the first of them, which leaves a
// prefix match on the host.
std::string UrlToPattern(const GURL& url) {
  std::string pattern(url.host());
  std::ranges::replace_if(
      pattern,
      [](char c) {
        return kPatternSyntaxChars.find(c) != std::string_view::npos;
      },
      '*');
  if (url.has_port()) {
    base::StrAppend(&pattern, {":", url.port()});
  }
  std::string rest(url.path());
  if (url.has_query()) {
    base::StrAppend(&rest, {"?", url.query()});
  }
  const size_t cut = rest.find_first_of(kPatternSyntaxChars);
  base::StrAppend(&pattern, {std::string_view(rest).substr(0, cut)});
  return pattern;
}

// Where the first `#` of a cosmetic filter is, or npos for any other line.
// This is how adblock-rust tells them apart: a `#` with another one within the
// next four characters (`##`, `#@#`, `#?#`, `#@?#`, ...), unless the line
// starts like a network filter.
size_t CosmeticMarker(std::string_view line) {
  if (line.starts_with('|') || line.starts_with("@@|")) {
    return std::string_view::npos;
  }
  const size_t sharp = line.find('#');
  if (sharp == std::string_view::npos ||
      line.substr(sharp + 1, 4).find('#') == std::string_view::npos) {
    return std::string_view::npos;
  }
  return sharp;
}

// Whether a line of the user's filters changes what the lists' own filters
// do. See UserListExceptions().
bool AffectsListFilters(std::string_view line) {
  if (line.starts_with('!') || line.starts_with("[Adblock")) {
    return false;
  }
  if (const size_t sharp = CosmeticMarker(line);
      sharp != std::string_view::npos) {
    // Only exceptions, and only those for given sites: adblock-rust rejects
    // `#@#` without a site in front.
    return sharp > 0 && line.substr(sharp + 1).starts_with('@');
  }
  const size_t dollar = line.rfind('$');
  if (dollar == std::string_view::npos) {
    return false;
  }
  const bool exception = line.starts_with("@@");
  for (std::string_view option :
       base::SplitStringPiece(line.substr(dollar + 1), ",",
                              base::TRIM_WHITESPACE, base::SPLIT_WANT_ALL)) {
    while (option.starts_with('~')) {
      option.remove_prefix(1);
    }
    option = option.substr(0, option.find('='));
    if (option == "badfilter") {
      return true;
    }
    if (exception && (option == "generichide" || option == "ghide" ||
                      option == "csp" || option == "redirect" ||
                      option == "redirect-rule" || option == "rewrite")) {
      return true;
    }
  }
  return false;
}

// Whether a domain list of a cosmetic rule (`a.com,~b.a.com,google.*`) applies
// to `hostname`.
bool CosmeticDomainsApply(std::string_view domains, std::string_view hostname) {
  bool any_positive = false;
  bool positive_match = false;
  for (std::string_view entry : base::SplitStringPiece(
           domains, ",", base::TRIM_WHITESPACE, base::SPLIT_WANT_NONEMPTY)) {
    const bool negated = entry.starts_with('~');
    if (negated) {
      entry.remove_prefix(1);
    }
    bool matches;
    if (entry.ends_with(".*")) {
      // Entity: `google.*` is google.<any public suffix>.
      const std::string_view stem = entry.substr(0, entry.size() - 1);
      matches = hostname.starts_with(stem) ||
                hostname.find(base::StrCat({".", stem})) !=
                    std::string_view::npos;
    } else {
      matches = hostname == entry ||
                (hostname.size() > entry.size() && hostname.ends_with(entry) &&
                 hostname[hostname.size() - entry.size() - 1] == '.');
    }
    if (negated) {
      if (matches) {
        return false;
      }
    } else {
      any_positive = true;
      positive_match |= matches;
    }
  }
  return !any_positive || positive_match;
}

}  // namespace

std::string NormalizeSiteEntry(std::string_view input) {
  std::string_view s = base::TrimWhitespaceASCII(input, base::TRIM_ALL);
  if (const size_t scheme = s.find("://"); scheme != std::string_view::npos) {
    s.remove_prefix(scheme + 3);
  }
  if (s.starts_with("*.")) {
    s.remove_prefix(2);
  }
  s = s.substr(0, s.find_first_of("/?#"));
  if (const size_t at = s.rfind('@'); at != std::string_view::npos) {
    s.remove_prefix(at + 1);
  }
  // Drop a port.
  if (const size_t colon = s.find(':'); colon != std::string_view::npos) {
    s = s.substr(0, colon);
  }
  while (s.ends_with('.')) {
    s.remove_suffix(1);
  }
  if (s.empty() || s.size() > 253 || s.starts_with('.') ||
      !std::ranges::all_of(s, IsValidHostChar)) {
    return std::string();
  }
  return base::ToLowerASCII(s);
}

bool HostMatchesSites(std::string_view host,
                      const std::set<std::string, std::less<>>& sites) {
  if (host.empty() || sites.empty()) {
    return false;
  }
  while (true) {
    if (sites.contains(host)) {
      return true;
    }
    const size_t dot = host.find('.');
    if (dot == std::string_view::npos) {
      return false;
    }
    host.remove_prefix(dot + 1);
  }
}

std::string AllowDocumentRule(std::string_view host) {
  return base::StrCat({"@@||", host, "^$document"});
}

std::string BlockDocumentRule(std::string_view host) {
  return base::StrCat({"||", host, "^$document"});
}

std::string AllowRequestRule(const GURL& url, std::string_view page_host) {
  std::string rule = base::StrCat({"@@||", UrlToPattern(url)});
  if (!page_host.empty()) {
    base::StrAppend(&rule, {"$domain=", page_host});
  }
  return rule;
}

std::string BlockRequestRule(const GURL& url, std::string_view page_host) {
  std::string rule = base::StrCat({"||", UrlToPattern(url)});
  if (!page_host.empty()) {
    base::StrAppend(&rule, {"$domain=", page_host});
  }
  return rule;
}

bool AddRule(std::string& text, std::string_view rule) {
  for (std::string_view line : base::SplitStringPiece(
           text, "\n", base::TRIM_WHITESPACE, base::SPLIT_WANT_NONEMPTY)) {
    if (line == rule) {
      return false;
    }
  }
  if (!text.empty() && !text.ends_with('\n')) {
    text += '\n';
  }
  base::StrAppend(&text, {rule, "\n"});
  return true;
}

bool RemoveRule(std::string& text, std::string_view rule) {
  std::string out;
  bool changed = false;
  for (std::string_view line : base::SplitStringPieceUsingSubstr(
           text, "\n", base::KEEP_WHITESPACE, base::SPLIT_WANT_ALL)) {
    if (base::TrimWhitespaceASCII(line, base::TRIM_ALL) == rule) {
      changed = true;
      continue;
    }
    base::StrAppend(&out, {line, "\n"});
  }
  if (changed) {
    // The split yields a last empty piece after a trailing newline, which
    // became one more newline; strip it.
    if (!text.ends_with('\n') && out.ends_with('\n')) {
      out.pop_back();
    }
    while (out.ends_with("\n\n")) {
      out.pop_back();
    }
    text = std::move(out);
  }
  return changed;
}

std::vector<CosmeticExceptionRule> ParseCosmeticExceptions(
    std::string_view user_text) {
  std::vector<CosmeticExceptionRule> rules;
  for (std::string_view line : base::SplitStringPiece(
           user_text, "\n", base::TRIM_WHITESPACE, base::SPLIT_WANT_NONEMPTY)) {
    const size_t marker = line.find("#@#");
    if (marker == std::string_view::npos || line.starts_with('!')) {
      continue;
    }
    const std::string_view selector = line.substr(marker + 3);
    if (selector.empty()) {
      continue;
    }
    rules.push_back({.domains = std::string(line.substr(0, marker)),
                     .selector = std::string(selector)});
  }
  return rules;
}

std::vector<std::string> UserCosmeticExceptions(
    const std::vector<CosmeticExceptionRule>& rules,
    std::string_view hostname) {
  std::vector<std::string> exceptions;
  for (const CosmeticExceptionRule& rule : rules) {
    if (CosmeticDomainsApply(rule.domains, hostname)) {
      exceptions.push_back(rule.selector);
    }
  }
  return exceptions;
}

std::vector<std::string> UserCosmeticExceptions(std::string_view user_text,
                                                std::string_view hostname) {
  return UserCosmeticExceptions(ParseCosmeticExceptions(user_text), hostname);
}

void RemoveExceptedSelectors(std::vector<std::string>& selectors,
                             const std::vector<std::string>& exceptions) {
  if (exceptions.empty()) {
    return;
  }
  std::erase_if(selectors, [&](const std::string& selector) {
    return std::ranges::contains(exceptions, selector);
  });
}

std::string UserListExceptions(std::string_view user_filters) {
  std::set<std::string_view> lines;
  for (std::string_view line :
       base::SplitStringPiece(user_filters, "\n", base::TRIM_WHITESPACE,
                              base::SPLIT_WANT_NONEMPTY)) {
    if (AffectsListFilters(line)) {
      lines.insert(line);
    }
  }
  std::string text;
  for (std::string_view line : lines) {
    base::StrAppend(&text, {line, "\n"});
  }
  return text;
}

}  // namespace reba_adblock
