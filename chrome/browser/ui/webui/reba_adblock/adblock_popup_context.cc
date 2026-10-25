// Copyright 2026 The REBA Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/webui/reba_adblock/adblock_popup_context.h"

#include <utility>

namespace reba_adblock {

WEB_CONTENTS_USER_DATA_KEY_IMPL(AdblockPopupContext);

AdblockPopupContext::AdblockPopupContext(content::WebContents* popup,
                                         content::WebContents* source)
    : content::WebContentsUserData<AdblockPopupContext>(*popup),
      source_(source ? source->GetWeakPtr() : nullptr) {}

AdblockPopupContext::~AdblockPopupContext() = default;

void AdblockPopupContext::Close() {
  if (close_) {
    std::move(close_).Run();
  }
}

}  // namespace reba_adblock
