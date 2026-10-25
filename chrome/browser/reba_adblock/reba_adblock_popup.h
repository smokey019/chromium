// Copyright 2026 The REBA Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_REBA_ADBLOCK_REBA_ADBLOCK_POPUP_H_
#define CHROME_BROWSER_REBA_ADBLOCK_REBA_ADBLOCK_POPUP_H_

class GURL;

namespace content {
class RenderFrameHost;
}

namespace reba_adblock {

// Whether the ad blocker refuses the new window that `opener` wants to open
// at `target_url` (window.open(), a link with target=_blank, ...). The filter
// lists decide with their `$popup` rules. A refused window is recorded in the
// log and in the opener's blocked count; the caller only has to not create it,
// so no empty tab flashes up.
bool ShouldBlockPopup(content::RenderFrameHost* opener, const GURL& target_url);

}  // namespace reba_adblock

#endif  // CHROME_BROWSER_REBA_ADBLOCK_REBA_ADBLOCK_POPUP_H_
