// Copyright 2026 The REBA Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/reba_adblock/reba_adblock_popup.h"

#include "chrome/browser/reba_adblock/reba_adblock_service.h"
#include "chrome/browser/reba_adblock/reba_adblock_tab_helper.h"
#include "content/public/browser/browser_context.h"
#include "content/public/browser/render_frame_host.h"
#include "content/public/browser/web_contents.h"
#include "url/gurl.h"

namespace reba_adblock {

bool ShouldBlockPopup(content::RenderFrameHost* opener,
                      const GURL& target_url) {
  RebaAdblockService* service = RebaAdblockService::Get();
  // about:blank and the like are judged when they navigate somewhere.
  if (!opener || !service || !service->IsEnabled() ||
      !target_url.SchemeIsHTTPOrHTTPS()) {
    return false;
  }
  const GURL page_url = opener->GetOutermostMainFrame()->GetLastCommittedURL();
  if (!page_url.SchemeIsHTTPOrHTTPS()) {
    return false;
  }
  const Decision decision =
      service->CheckRequest(target_url, page_url, RequestType::kPopup, "GET");
  if (!decision.block) {
    return false;
  }

  content::WebContents* web_contents =
      content::WebContents::FromRenderFrameHost(opener);
  const bool incognito =
      web_contents && web_contents->GetBrowserContext()->IsOffTheRecord();
  service->RecordBlocked(target_url, RequestType::kPopup, page_url, decision,
                         incognito);
  if (web_contents) {
    if (RebaAdblockTabHelper* helper =
            RebaAdblockTabHelper::FromWebContents(web_contents)) {
      helper->OnRequestBlocked(target_url, RequestType::kPopup, decision);
    }
  }
  return true;
}

}  // namespace reba_adblock
