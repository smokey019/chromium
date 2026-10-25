// Copyright 2026 The REBA Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_UI_WEBUI_REBA_ADBLOCK_ADBLOCK_BLOCKED_PAGE_UI_H_
#define CHROME_BROWSER_UI_WEBUI_REBA_ADBLOCK_ADBLOCK_BLOCKED_PAGE_UI_H_

#include "chrome/common/webui_url_constants.h"
#include "content/public/browser/webui_config.h"
#include "content/public/common/url_constants.h"
#include "content/public/browser/web_ui_controller.h"

namespace reba_adblock {

// chrome://reba-blocked: the page that replaces a website the ad blocker
// blocks, with buttons to open it anyway.
class AdblockBlockedPageUI : public content::WebUIController {
 public:
  explicit AdblockBlockedPageUI(content::WebUI* web_ui);
  AdblockBlockedPageUI(const AdblockBlockedPageUI&) = delete;
  AdblockBlockedPageUI& operator=(const AdblockBlockedPageUI&) = delete;
  ~AdblockBlockedPageUI() override;
};

class AdblockBlockedPageUIConfig
    : public content::DefaultWebUIConfig<AdblockBlockedPageUI> {
 public:
  AdblockBlockedPageUIConfig()
      : DefaultWebUIConfig(content::kChromeUIScheme,
                           chrome::kChromeUIRebaBlockedHost) {}
};

}  // namespace reba_adblock

#endif  // CHROME_BROWSER_UI_WEBUI_REBA_ADBLOCK_ADBLOCK_BLOCKED_PAGE_UI_H_
