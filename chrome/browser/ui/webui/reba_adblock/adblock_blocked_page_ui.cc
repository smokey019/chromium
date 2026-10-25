// Copyright 2026 The REBA Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/webui/reba_adblock/adblock_blocked_page_ui.h"

#include <memory>

#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/webui/reba_adblock/adblock_blocked_page_handler.h"
#include "chrome/grit/browser_resources.h"
#include "content/public/browser/web_ui.h"
#include "content/public/browser/web_ui_data_source.h"

namespace reba_adblock {

AdblockBlockedPageUI::AdblockBlockedPageUI(content::WebUI* web_ui)
    : content::WebUIController(web_ui) {
  Profile* profile = Profile::FromWebUI(web_ui);
  content::WebUIDataSource* source = content::WebUIDataSource::CreateAndAdd(
      profile, chrome::kChromeUIRebaBlockedHost);
  source->AddResourcePath("blocked.css", IDR_REBA_ADBLOCK_BLOCKED_CSS);
  source->AddResourcePath("blocked.js", IDR_REBA_ADBLOCK_BLOCKED_JS);
  source->SetDefaultResource(IDR_REBA_ADBLOCK_BLOCKED_HTML);
  // Incognito and Guest windows don't save choices (see
  // AdblockBlockedPageHandler), so the page offers only ones for the session.
  source->AddBoolean("offTheRecord", profile->IsOffTheRecord());
  source->UseStringsJs();
  web_ui->AddMessageHandler(std::make_unique<AdblockBlockedPageHandler>());
}

AdblockBlockedPageUI::~AdblockBlockedPageUI() = default;

}  // namespace reba_adblock
