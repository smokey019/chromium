// Copyright 2016 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "components/ntp_tiles/constants.h"

#include "build/build_config.h"

namespace ntp_tiles {

#if BUILDFLAG(IS_ANDROID) || BUILDFLAG(IS_IOS)
const size_t kMaxNumCustomLinks = 8;
#else
// Upper bound of the user-adjustable shortcut count on the desktop New Tab
// Page (the `ntp.shortcuts_max_count` pref, 1-30). The page shows as many as
// that pref allows.
const size_t kMaxNumCustomLinks = 30;
#endif

// If custom links are enabled, an additional tile may be returned making up to
// kMaxNumCustomLinks custom links including the "Add shortcut" button.
#if BUILDFLAG(IS_ANDROID) || BUILDFLAG(IS_IOS)
const size_t kMaxNumMostVisited = 8;
#else
const size_t kMaxNumMostVisited = 30;
#endif

}  // namespace ntp_tiles
