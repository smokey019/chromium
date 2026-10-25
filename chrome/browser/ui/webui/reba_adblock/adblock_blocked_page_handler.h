// Copyright 2026 The REBA Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_UI_WEBUI_REBA_ADBLOCK_ADBLOCK_BLOCKED_PAGE_HANDLER_H_
#define CHROME_BROWSER_UI_WEBUI_REBA_ADBLOCK_ADBLOCK_BLOCKED_PAGE_HANDLER_H_

#include <string>
#include <vector>

#include "base/scoped_observation.h"
#include "base/timer/timer.h"
#include "base/values.h"
#include "chrome/browser/reba_adblock/reba_adblock_service.h"
#include "content/public/browser/web_ui_message_handler.h"
#include "url/gurl.h"

namespace reba_adblock {

// The buttons of the page that says a website is blocked.
class AdblockBlockedPageHandler : public content::WebUIMessageHandler,
                                  public RebaAdblockServiceObserver {
 public:
  AdblockBlockedPageHandler();
  AdblockBlockedPageHandler(const AdblockBlockedPageHandler&) = delete;
  AdblockBlockedPageHandler& operator=(const AdblockBlockedPageHandler&) =
      delete;
  ~AdblockBlockedPageHandler() override;

  // content::WebUIMessageHandler:
  void RegisterMessages() override;
  void OnJavascriptDisallowed() override;

  // RebaAdblockServiceObserver:
  void OnEnginesChanged() override;
  void OnAdblockServiceShutdown() override;

 private:
  struct Pending {
    base::Value callback_id;
    GURL url;
    bool wait_until_allowed;
  };

  void HandleAction(const base::ListValue& args);
  // Answers the requests whose change is in effect.
  void ResolvePending(bool everything);

  std::vector<Pending> pending_;
  base::OneShotTimer timeout_;
  base::ScopedObservation<RebaAdblockService, RebaAdblockServiceObserver>
      observation_{this};
};

}  // namespace reba_adblock

#endif  // CHROME_BROWSER_UI_WEBUI_REBA_ADBLOCK_ADBLOCK_BLOCKED_PAGE_HANDLER_H_
