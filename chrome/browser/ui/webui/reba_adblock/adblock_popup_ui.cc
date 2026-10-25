// Copyright 2026 The REBA Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/webui/reba_adblock/adblock_popup_ui.h"

#include <memory>

#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/webui/reba_adblock/adblock_popup_handler.h"
#include "chrome/grit/browser_resources.h"
#include "content/public/browser/web_ui.h"
#include "content/public/browser/web_ui_data_source.h"

namespace reba_adblock {

AdblockPopupUI::AdblockPopupUI(content::WebUI* web_ui)
    : content::WebUIController(web_ui) {
  content::WebUIDataSource* source = content::WebUIDataSource::CreateAndAdd(
      Profile::FromWebUI(web_ui), chrome::kChromeUIRebaAdblockPopupHost);
  source->AddResourcePath("popup.css", IDR_REBA_ADBLOCK_POPUP_CSS);
  source->AddResourcePath("popup.js", IDR_REBA_ADBLOCK_POPUP_JS);
  source->SetDefaultResource(IDR_REBA_ADBLOCK_POPUP_HTML);
  web_ui->AddMessageHandler(std::make_unique<AdblockPopupHandler>());
}

AdblockPopupUI::~AdblockPopupUI() = default;

}  // namespace reba_adblock
