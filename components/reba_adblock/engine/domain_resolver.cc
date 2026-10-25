// Copyright 2026 The REBA Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "components/reba_adblock/engine/domain_resolver.h"

#include <string>
#include <string_view>

#include "net/base/registry_controlled_domains/registry_controlled_domain.h"

namespace reba_adblock::rust {

size_t HostDomainStart(::rust::Str host) {
  const std::string_view host_view(host.data(), host.size());
  // Private registries (blogspot.com, github.io, ...) count as suffixes, like
  // in uBlock Origin, so that `a.github.io` and `b.github.io` are third
  // parties of each other.
  const std::string domain =
      net::registry_controlled_domains::GetDomainAndRegistry(
          host_view,
          net::registry_controlled_domains::INCLUDE_PRIVATE_REGISTRIES);
  if (domain.empty() || domain.size() >= host_view.size() ||
      !host_view.ends_with(domain)) {
    return 0;
  }
  return host_view.size() - domain.size();
}

}  // namespace reba_adblock::rust
