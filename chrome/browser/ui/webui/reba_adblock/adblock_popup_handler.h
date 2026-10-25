// Copyright 2026 The REBA Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_UI_WEBUI_REBA_ADBLOCK_ADBLOCK_POPUP_HANDLER_H_
#define CHROME_BROWSER_UI_WEBUI_REBA_ADBLOCK_ADBLOCK_POPUP_HANDLER_H_

#include "base/scoped_observation.h"
#include "base/timer/timer.h"
#include "base/values.h"
#include "chrome/browser/reba_adblock/reba_adblock_service.h"
#include "chrome/browser/reba_adblock/reba_adblock_tab_helper.h"
#include "content/public/browser/web_ui_message_handler.h"

namespace content {
class WebContents;
}

namespace reba_adblock {

// The messages of the toolbar pop-up: what the ad blocker did on the tab the
// pop-up belongs to, and controls for it.
class AdblockPopupHandler : public content::WebUIMessageHandler,
                            public RebaAdblockServiceObserver,
                            public RebaAdblockTabHelper::Observer {
 public:
  AdblockPopupHandler();
  AdblockPopupHandler(const AdblockPopupHandler&) = delete;
  AdblockPopupHandler& operator=(const AdblockPopupHandler&) = delete;
  ~AdblockPopupHandler() override;

  // content::WebUIMessageHandler:
  void RegisterMessages() override;
  void OnJavascriptAllowed() override;
  void OnJavascriptDisallowed() override;

  // RebaAdblockServiceObserver:
  void OnSettingsChanged() override;
  void OnBlockedRequestsLogged() override;
  void OnAdblockServiceShutdown() override;

  // RebaAdblockTabHelper::Observer:
  void OnBlockedCountChanged(RebaAdblockTabHelper* helper) override;
  void OnTabHelperDestroyed(RebaAdblockTabHelper* helper) override;

 private:
  // The tab the pop-up is for; null if it went away.
  content::WebContents* SourceContents();
  base::DictValue BuildState();
  void NotifySoon();
  void Notify();

  void HandleGetState(const base::ListValue& args);
  void HandleSetSiteTrusted(const base::ListValue& args);
  void HandleSetEnabled(const base::ListValue& args);
  void HandleReload(const base::ListValue& args);
  void HandlePick(const base::ListValue& args);
  void HandleOpenSettings(const base::ListValue& args);

  base::OneShotTimer notify_timer_;
  base::ScopedObservation<RebaAdblockService, RebaAdblockServiceObserver>
      service_observation_{this};
  base::ScopedObservation<RebaAdblockTabHelper, RebaAdblockTabHelper::Observer>
      tab_observation_{this};
};

}  // namespace reba_adblock

#endif  // CHROME_BROWSER_UI_WEBUI_REBA_ADBLOCK_ADBLOCK_POPUP_HANDLER_H_
