// Copyright 2025 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef COMPONENTS_HISTORY_CORE_BROWSER_TOP_SITES_CONSTANTS_H_
#define COMPONENTS_HISTORY_CORE_BROWSER_TOP_SITES_CONSTANTS_H_

#include <stddef.h>

#include "build/build_config.h"

namespace history {
// How many top sites to store in the cache. Desktop keeps more so the New Tab
// Page can show up to 30 most-visited shortcuts (see
// `ntp_tiles::kMaxNumMostVisited`).
#if BUILDFLAG(IS_ANDROID) || BUILDFLAG(IS_IOS)
static constexpr size_t kTopSitesNumber = 10;
#else
static constexpr size_t kTopSitesNumber = 30;
#endif
}  // namespace history

#endif  // COMPONENTS_HISTORY_CORE_BROWSER_TOP_SITES_CONSTANTS_H_
