// Copyright 2026 The REBA Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_UI_WEBUI_REBA_ADBLOCK_ADBLOCK_UI_UTILS_H_
#define CHROME_BROWSER_UI_WEBUI_REBA_ADBLOCK_ADBLOCK_UI_UTILS_H_

#include <vector>

#include "base/values.h"
#include "chrome/browser/reba_adblock/reba_adblock_service.h"
#include "components/reba_adblock/core/blocked_request_log.h"
#include "components/reba_adblock/core/security_log.h"

namespace reba_adblock {

// Conversions to what the ad blocker's pages receive.

base::DictValue ListViewToDict(const ListView& view);
base::ListValue BlockedRequestsToList(
    const std::vector<BlockedRequest>& requests);

// Everything the settings page shows, in one dictionary.
// The protection log, for the settings page.
base::ListValue SecurityEventsToList(const std::vector<SecurityEvent>& events);

base::DictValue BuildSettingsState(RebaAdblockService& service);

}  // namespace reba_adblock

#endif  // CHROME_BROWSER_UI_WEBUI_REBA_ADBLOCK_ADBLOCK_UI_UTILS_H_
