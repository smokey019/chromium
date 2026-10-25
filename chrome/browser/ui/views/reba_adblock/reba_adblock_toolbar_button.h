// Copyright 2026 The REBA Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_UI_VIEWS_REBA_ADBLOCK_REBA_ADBLOCK_TOOLBAR_BUTTON_H_
#define CHROME_BROWSER_UI_VIEWS_REBA_ADBLOCK_REBA_ADBLOCK_TOOLBAR_BUTTON_H_

#include "base/callback_list.h"
#include "base/memory/raw_ptr.h"
#include "base/scoped_observation.h"
#include "base/timer/timer.h"
#include "chrome/browser/reba_adblock/reba_adblock_service.h"
#include "chrome/browser/reba_adblock/reba_adblock_tab_helper.h"
#include "chrome/browser/ui/views/toolbar/toolbar_button.h"
#include "ui/base/metadata/metadata_header_macros.h"

class BrowserWindowInterface;

namespace content {
class WebContents;
}

namespace reba_adblock {

// The shield in the toolbar: shows whether the ad blocker is working on the
// page, how many requests it blocked there, and opens the pop-up with the
// controls for the site.
class RebaAdblockToolbarButton : public ToolbarButton,
                                 public RebaAdblockServiceObserver,
                                 public RebaAdblockTabHelper::Observer {
  METADATA_HEADER(RebaAdblockToolbarButton, ToolbarButton)

 public:
  explicit RebaAdblockToolbarButton(BrowserWindowInterface* browser);
  RebaAdblockToolbarButton(const RebaAdblockToolbarButton&) = delete;
  RebaAdblockToolbarButton& operator=(const RebaAdblockToolbarButton&) = delete;
  ~RebaAdblockToolbarButton() override;

  // RebaAdblockServiceObserver:
  void OnSettingsChanged() override;
  void OnAdblockServiceShutdown() override;

  // RebaAdblockTabHelper::Observer:
  void OnBlockedCountChanged(RebaAdblockTabHelper* helper) override;
  void OnTabHelperDestroyed(RebaAdblockTabHelper* helper) override;

 private:
  void ButtonPressed();
  void OnActiveTabChanged(BrowserWindowInterface* browser);
  content::WebContents* ActiveContents() const;
  void UpdateState();

  const raw_ptr<BrowserWindowInterface> browser_;
  base::CallbackListSubscription active_tab_subscription_;
  base::ScopedObservation<RebaAdblockService, RebaAdblockServiceObserver>
      service_observation_{this};
  base::ScopedObservation<RebaAdblockTabHelper, RebaAdblockTabHelper::Observer>
      tab_observation_{this};
  // Batches the blocked count updates of a loading page.
  base::OneShotTimer update_timer_;
  // Whether the button shows the shield that means "blocking here".
  bool shows_active_icon_ = true;
};

}  // namespace reba_adblock

#endif  // CHROME_BROWSER_UI_VIEWS_REBA_ADBLOCK_REBA_ADBLOCK_TOOLBAR_BUTTON_H_
