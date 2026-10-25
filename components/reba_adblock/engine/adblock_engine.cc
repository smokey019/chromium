// Copyright 2026 The REBA Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "components/reba_adblock/engine/adblock_engine.h"

#include <optional>
#include <string_view>
#include <utility>

#include "base/check.h"
#include "base/containers/span.h"
#include "base/memory/ptr_util.h"
#include "base/notreached.h"
#include "base/strings/string_util.h"
#include "components/reba_adblock/engine/engine.rs.h"
#include "url/gurl.h"

namespace reba_adblock {

namespace {

// cxx aborts the process when a rust::Str is given text that isn't UTF-8, so
// anything that comes from outside this file is checked first. Returns
// nullopt for such text.
std::optional<::rust::Str> ToRustStr(std::string_view s) {
  if (!base::IsStringUTF8AllowingNoncharacters(s)) {
    return std::nullopt;
  }
  return ::rust::Str(s.data(), s.size());
}

::rust::Slice<const uint8_t> ToRustSlice(base::span<const uint8_t> data) {
  return ::rust::Slice<const uint8_t>(data.data(), data.size());
}

std::string ToString(const ::rust::String& s) {
  return std::string(s);
}

std::vector<std::string> ToStringVector(const ::rust::Vec<::rust::String>& v) {
  std::vector<std::string> out;
  out.reserve(v.size());
  for (const ::rust::String& s : v) {
    out.push_back(ToString(s));
  }
  return out;
}

::rust::Vec<::rust::String> ToRustVector(const std::vector<std::string>& v) {
  ::rust::Vec<::rust::String> out;
  out.reserve(v.size());
  for (const std::string& s : v) {
    // Selectors and class names come from web pages, so they may not be valid
    // UTF-8, which rust::String can't hold.
    out.push_back(::rust::String::lossy(s.data(), s.size()));
  }
  return out;
}

}  // namespace

const char* RequestTypeName(RequestType type) {
  switch (type) {
    case RequestType::kMainFrame:
      return "main_frame";
    case RequestType::kSubFrame:
      return "sub_frame";
    case RequestType::kScript:
      return "script";
    case RequestType::kStylesheet:
      return "stylesheet";
    case RequestType::kImage:
      return "image";
    case RequestType::kFont:
      return "font";
    case RequestType::kMedia:
      return "media";
    case RequestType::kObject:
      return "object";
    case RequestType::kXhr:
      return "xhr";
    case RequestType::kFetch:
      return "fetch";
    case RequestType::kPing:
      return "ping";
    case RequestType::kWebSocket:
      return "websocket";
    case RequestType::kPopup:
      return "popup";
    case RequestType::kCspReport:
      return "csp_report";
    case RequestType::kOther:
      return "other";
  }
  NOTREACHED();
}

NetworkResult::NetworkResult() = default;
NetworkResult::NetworkResult(const NetworkResult&) = default;
NetworkResult::NetworkResult(NetworkResult&&) = default;
NetworkResult& NetworkResult::operator=(const NetworkResult&) = default;
NetworkResult& NetworkResult::operator=(NetworkResult&&) = default;
NetworkResult::~NetworkResult() = default;

CosmeticResources::CosmeticResources() = default;
CosmeticResources::CosmeticResources(const CosmeticResources&) = default;
CosmeticResources::CosmeticResources(CosmeticResources&&) = default;
CosmeticResources& CosmeticResources::operator=(const CosmeticResources&) =
    default;
CosmeticResources& CosmeticResources::operator=(CosmeticResources&&) = default;
CosmeticResources::~CosmeticResources() = default;

AdblockEngine::AdblockEngine(::rust::Box<rust::Engine> engine)
    : engine_(std::move(engine)) {}

AdblockEngine::~AdblockEngine() = default;

// static
std::unique_ptr<AdblockEngine> AdblockEngine::Build(
    std::vector<FilterListInput> lists,
    const std::string& resources_json) {
  ::rust::Vec<rust::ListInput> rust_lists;
  rust_lists.reserve(lists.size());
  for (const FilterListInput& list : lists) {
    rust::ListInput input;
    input.text = ::rust::String::lossy(list.text.data(), list.text.size());
    input.is_hosts = list.is_hosts;
    input.trusted = list.trusted;
    rust_lists.push_back(std::move(input));
  }
  // The rust copies are made; free the originals before compiling.
  lists.clear();
  lists.shrink_to_fit();
  return base::WrapUnique(new AdblockEngine(rust::build_engine(
      std::move(rust_lists), ToRustSlice(base::as_byte_span(resources_json)))));
}

// static
std::unique_ptr<AdblockEngine> AdblockEngine::Deserialize(
    base::span<const uint8_t> data,
    const std::string& resources_json) {
  ::rust::Box<rust::Engine> engine = rust::deserialize_engine(
      ToRustSlice(data), ToRustSlice(base::as_byte_span(resources_json)));
  if (!engine->is_valid()) {
    return nullptr;
  }
  return base::WrapUnique(new AdblockEngine(std::move(engine)));
}

std::vector<uint8_t> AdblockEngine::Serialize() const {
  ::rust::Vec<uint8_t> data = engine_->serialize();
  return std::vector<uint8_t>(data.begin(), data.end());
}

NetworkResult AdblockEngine::CheckNetwork(const GURL& url,
                                          const GURL& source_url,
                                          RequestType type,
                                          std::string_view method,
                                          bool previously_matched,
                                          bool force_exceptions) const {
  const std::optional<::rust::Str> rust_url = ToRustStr(url.spec());
  const std::optional<::rust::Str> rust_source_url =
      ToRustStr(source_url.spec());
  if (!rust_url || !rust_source_url) {
    return NetworkResult();
  }
  // Without a method only `$method` filters stop matching.
  const ::rust::Str rust_method = ToRustStr(method).value_or(::rust::Str());
  const rust::NetworkResult result = engine_->check_network(
      *rust_url, *rust_source_url, ::rust::Str(RequestTypeName(type)),
      rust_method, previously_matched, force_exceptions);
  NetworkResult out;
  out.blocked = result.blocked;
  out.matched = result.matched;
  out.important = result.important;
  out.has_exception = result.has_exception;
  out.filter = ToString(result.filter);
  out.exception_filter = ToString(result.exception_filter);
  out.filter_list = result.filter_list;
  out.exception_list = result.exception_list;
  out.redirect = ToString(result.redirect);
  out.rewritten_url = ToString(result.rewritten_url);
  return out;
}

std::string AdblockEngine::CspDirectives(const GURL& url,
                                         const GURL& source_url,
                                         RequestType type) const {
  const std::optional<::rust::Str> rust_url = ToRustStr(url.spec());
  const std::optional<::rust::Str> rust_source_url =
      ToRustStr(source_url.spec());
  if (!rust_url || !rust_source_url) {
    return std::string();
  }
  return ToString(engine_->csp_directives(*rust_url, *rust_source_url,
                                          ::rust::Str(RequestTypeName(type))));
}

CosmeticResources AdblockEngine::UrlCosmeticResources(const GURL& url) const {
  const std::optional<::rust::Str> rust_url = ToRustStr(url.spec());
  if (!rust_url) {
    return CosmeticResources();
  }
  const rust::CosmeticResult result =
      engine_->url_cosmetic_resources(*rust_url);
  CosmeticResources out;
  out.hide_selectors = ToStringVector(result.hide_selectors);
  out.procedural_actions = ToStringVector(result.procedural_actions);
  out.exceptions = ToStringVector(result.exceptions);
  out.injected_script = ToString(result.injected_script);
  out.generichide = result.generichide;
  return out;
}

std::vector<std::string> AdblockEngine::HiddenClassIdSelectors(
    const std::vector<std::string>& classes,
    const std::vector<std::string>& ids,
    const std::vector<std::string>& exceptions) const {
  return ToStringVector(engine_->hidden_class_id_selectors(
      ToRustVector(classes), ToRustVector(ids), ToRustVector(exceptions)));
}

}  // namespace reba_adblock
