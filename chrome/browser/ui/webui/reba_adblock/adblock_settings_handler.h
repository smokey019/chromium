// Copyright 2026 The REBA Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_UI_WEBUI_REBA_ADBLOCK_ADBLOCK_SETTINGS_HANDLER_H_
#define CHROME_BROWSER_UI_WEBUI_REBA_ADBLOCK_ADBLOCK_SETTINGS_HANDLER_H_

#include "base/memory/weak_ptr.h"
#include "base/scoped_observation.h"
#include "base/timer/timer.h"
#include "base/values.h"
#include "chrome/browser/reba_adblock/reba_adblock_service.h"
#include "content/public/browser/web_ui_message_handler.h"

namespace reba_adblock {

// Messages of the "Ad blocker" section of chrome://settings.
class AdblockSettingsHandler : public content::WebUIMessageHandler,
                               public RebaAdblockServiceObserver {
 public:
  AdblockSettingsHandler();
  AdblockSettingsHandler(const AdblockSettingsHandler&) = delete;
  AdblockSettingsHandler& operator=(const AdblockSettingsHandler&) = delete;
  ~AdblockSettingsHandler() override;

  // content::WebUIMessageHandler:
  void RegisterMessages() override;
  void OnJavascriptAllowed() override;
  void OnJavascriptDisallowed() override;

  // RebaAdblockServiceObserver:
  void OnEnginesChanged() override;
  void OnListStatesChanged() override;
  void OnSettingsChanged() override;
  void OnBlockedRequestsLogged() override;
  void OnAdblockServiceShutdown() override;

 private:
  void HandleGetState(const base::ListValue& args);
  void HandleSetBool(const base::ListValue& args);
  void HandleSetListEnabled(const base::ListValue& args);
  void HandleAddList(const base::ListValue& args);
  void HandleRemoveList(const base::ListValue& args);
  void HandleUpdateLists(const base::ListValue& args);
  void HandleResetLists(const base::ListValue& args);
  void HandleSetUserFilters(const base::ListValue& args);
  void HandleAddTrustedSite(const base::ListValue& args);
  void HandleRemoveTrustedSite(const base::ListValue& args);
  void HandleGetLog(const base::ListValue& args);
  void HandleClearLog(const base::ListValue& args);
  void HandleLogAction(const base::ListValue& args);

  // Tells the page that the state changed, soon (many changes come in bursts).
  void NotifyStateChangedSoon();
  void NotifyStateChanged();

  base::OneShotTimer state_timer_;
  base::ScopedObservation<RebaAdblockService, RebaAdblockServiceObserver>
      observation_{this};
};

}  // namespace reba_adblock

#endif  // CHROME_BROWSER_UI_WEBUI_REBA_ADBLOCK_ADBLOCK_SETTINGS_HANDLER_H_
