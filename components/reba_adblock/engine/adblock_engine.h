// Copyright 2026 The REBA Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef COMPONENTS_REBA_ADBLOCK_ENGINE_ADBLOCK_ENGINE_H_
#define COMPONENTS_REBA_ADBLOCK_ENGINE_ADBLOCK_ENGINE_H_

#include <stdint.h>

#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "base/containers/span.h"
#include "third_party/rust/cxx/v1/cxx.h"

class GURL;

namespace reba_adblock {

namespace rust {
struct Engine;
}  // namespace rust

// What kind of resource a network request is for. Filters can be limited to
// some of these with `$script`, `$image` and so on.
enum class RequestType {
  kMainFrame,
  kSubFrame,
  kScript,
  kStylesheet,
  kImage,
  kFont,
  kMedia,
  kObject,
  kXhr,
  kFetch,
  kPing,
  kWebSocket,
  // A new window or tab that a page opens (`$popup`).
  kPopup,
  kCspReport,
  kOther,
};

// The uBlock Origin name of `type`, as understood by the engine.
const char* RequestTypeName(RequestType type);

// A filter list to compile into an engine.
struct FilterListInput {
  std::string text;
  // `text` is a hosts file (`0.0.0.0 example.com`) rather than ABP syntax.
  bool is_hosts = false;
  // The list may use trusted scriptlets.
  bool trusted = false;
};

struct NetworkResult {
  NetworkResult();
  NetworkResult(const NetworkResult&);
  NetworkResult(NetworkResult&&);
  NetworkResult& operator=(const NetworkResult&);
  NetworkResult& operator=(NetworkResult&&);
  ~NetworkResult();

  // The request should be blocked.
  bool blocked = false;
  // A blocking filter matched, even if an exception then overrode it.
  bool matched = false;
  // A `$important` filter matched; exceptions can't override it.
  bool important = false;
  // An exception filter matched.
  bool has_exception = false;
  // Raw text of the matched blocking filter and of the matched exception.
  std::string filter;
  std::string exception_filter;
  // Index, in the order the lists were given to Build(), of the list that has
  // the filter, or -1 if unknown.
  int filter_list = -1;
  int exception_list = -1;
  // `data:` URL to load instead of the request (`$redirect`).
  std::string redirect;
  // The request URL minus tracking parameters (`$removeparam`), or empty.
  std::string rewritten_url;
};

struct CosmeticResources {
  CosmeticResources();
  CosmeticResources(const CosmeticResources&);
  CosmeticResources(CosmeticResources&&);
  CosmeticResources& operator=(const CosmeticResources&);
  CosmeticResources& operator=(CosmeticResources&&);
  ~CosmeticResources();

  std::vector<std::string> hide_selectors;
  // JSON objects for filters that need the procedural filter interpreter
  // (`:has-text()`, `:upward()`, `:style()`, `:remove()`, ...).
  std::vector<std::string> procedural_actions;
  // Class and id selectors excepted from generic hiding.
  std::vector<std::string> exceptions;
  // JavaScript to run in the page before its own scripts (scriptlets).
  std::string injected_script;
  // Don't ask for generic hide rules on this page.
  bool generichide = false;
};

// An immutable set of compiled filter lists. All const methods are thread
// safe.
class AdblockEngine {
 public:
  // Compiles `lists`. This takes seconds with the full default set, so call it
  // off the UI thread. `resources_json` is the JSON array of redirect
  // resources and scriptlets, or empty. It is ignored if it doesn't parse.
  static std::unique_ptr<AdblockEngine> Build(
      std::vector<FilterListInput> lists,
      const std::string& resources_json);

  // Compiled form of the engine for caching on disk, see Deserialize().
  std::vector<uint8_t> Serialize() const;

  // Loads an engine written by Serialize(). Returns null if `data` is not
  // understood, for example when it was written by another version.
  static std::unique_ptr<AdblockEngine> Deserialize(
      base::span<const uint8_t> data,
      const std::string& resources_json);

  AdblockEngine(const AdblockEngine&) = delete;
  AdblockEngine& operator=(const AdblockEngine&) = delete;
  ~AdblockEngine();

  // Decides what to do with a request for `url` made by a page at
  // `source_url` (the top-level page; it decides what is third-party).
  // `method` is the HTTP method ("GET", "POST", ...) for `$method` filters.
  // `previously_matched` and `force_exceptions` are for layering this engine
  // over another one: pass the other engine's `blocked` and true to find out
  // whether this engine has an exception for a request the other one blocks.
  NetworkResult CheckNetwork(const GURL& url,
                             const GURL& source_url,
                             RequestType type,
                             std::string_view method,
                             bool previously_matched = false,
                             bool force_exceptions = false) const;

  // Extra CSP directives (`$csp`) to add to the response to a document
  // request, or empty.
  std::string CspDirectives(const GURL& url,
                            const GURL& source_url,
                            RequestType type) const;

  CosmeticResources UrlCosmeticResources(const GURL& url) const;

  // Selectors of generic hide rules that match any of the classes and ids.
  std::vector<std::string> HiddenClassIdSelectors(
      const std::vector<std::string>& classes,
      const std::vector<std::string>& ids,
      const std::vector<std::string>& exceptions) const;

 private:
  explicit AdblockEngine(::rust::Box<rust::Engine> engine);

  ::rust::Box<rust::Engine> engine_;
};

}  // namespace reba_adblock

#endif  // COMPONENTS_REBA_ADBLOCK_ENGINE_ADBLOCK_ENGINE_H_
