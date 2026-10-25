// Copyright 2026 The REBA Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_UI_WEBUI_REBA_ADBLOCK_ADBLOCK_POPUP_UI_H_
#define CHROME_BROWSER_UI_WEBUI_REBA_ADBLOCK_ADBLOCK_POPUP_UI_H_

#include "chrome/common/webui_url_constants.h"
#include "content/public/browser/webui_config.h"
#include "content/public/common/url_constants.h"
#include "content/public/browser/web_ui_controller.h"

namespace reba_adblock {

// chrome://reba-adblock-popup: the pop-up of the ad blocker's toolbar button.
class AdblockPopupUI : public content::WebUIController {
 public:
  explicit AdblockPopupUI(content::WebUI* web_ui);
  AdblockPopupUI(const AdblockPopupUI&) = delete;
  AdblockPopupUI& operator=(const AdblockPopupUI&) = delete;
  ~AdblockPopupUI() override;
};

class AdblockPopupUIConfig
    : public content::DefaultWebUIConfig<AdblockPopupUI> {
 public:
  AdblockPopupUIConfig()
      : DefaultWebUIConfig(content::kChromeUIScheme,
                           chrome::kChromeUIRebaAdblockPopupHost) {}
};

}  // namespace reba_adblock

#endif  // CHROME_BROWSER_UI_WEBUI_REBA_ADBLOCK_ADBLOCK_POPUP_UI_H_
