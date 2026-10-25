// Copyright 2026 The REBA Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef COMPONENTS_REBA_ADBLOCK_ENGINE_DOMAIN_RESOLVER_H_
#define COMPONENTS_REBA_ADBLOCK_ENGINE_DOMAIN_RESOLVER_H_

#include <stddef.h>

#include "third_party/rust/cxx/v1/cxx.h"

namespace reba_adblock::rust {

// Called by the Rust adblock engine to find the registrable domain (eTLD+1) of
// a hostname with Chromium's public suffix list. Returns the offset in `host`
// where the domain starts, or 0 if the host has none (IP addresses, bare
// labels).
size_t HostDomainStart(::rust::Str host);

}  // namespace reba_adblock::rust

#endif  // COMPONENTS_REBA_ADBLOCK_ENGINE_DOMAIN_RESOLVER_H_
