// Copyright 2026 The REBA Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/views/reba_adblock/reba_adblock_toolbar_button.h"

#include <string>

#include "base/functional/bind.h"
#include "base/strings/utf_string_conversions.h"
#include "base/time/time.h"
#include "chrome/app/vector_icons/vector_icons.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface.h"
#include "chrome/browser/ui/views/reba_adblock/reba_adblock_bubble.h"
#include "components/tabs/public/tab_interface.h"
#include "content/public/browser/web_contents.h"
#include "ui/base/metadata/metadata_impl_macros.h"
#include "ui/views/accessibility/view_accessibility.h"
#include "ui/views/controls/button/button_controller.h"

namespace reba_adblock {

namespace {

// A page can block hundreds of requests while it loads; the count is redrawn
// at most this often.
constexpr base::TimeDelta kUpdateDelay = base::Milliseconds(100);

}  // namespace

RebaAdblockToolbarButton::RebaAdblockToolbarButton(
    BrowserWindowInterface* browser)
    : ToolbarButton(base::BindRepeating(&RebaAdblockToolbarButton::ButtonPressed,
                                        base::Unretained(this))),
      browser_(browser) {
  // Act on press, like MediaToolbarButtonView: the press closes an open pop-up
  // (it loses focus), and acting on release would open it again at once.
  button_controller()->set_notify_action(
      views::ButtonController::NotifyAction::kOnPress);
  SetVectorIcon(kRebaAdblockShieldIcon);
  SetTooltipText(u"Ad blocker");
  GetViewAccessibility().SetName(u"Ad blocker");

  if (RebaAdblockService* service = RebaAdblockService::Get()) {
    service_observation_.Observe(service);
  }
  active_tab_subscription_ = browser_->RegisterActiveTabDidChange(
      base::BindRepeating(&RebaAdblockToolbarButton::OnActiveTabChanged,
                          base::Unretained(this)));
  OnActiveTabChanged(browser_);
}

RebaAdblockToolbarButton::~RebaAdblockToolbarButton() = default;

content::WebContents* RebaAdblockToolbarButton::ActiveContents() const {
  tabs::TabInterface* tab = browser_->GetActiveTabInterface();
  return tab ? tab->GetContents() : nullptr;
}

void RebaAdblockToolbarButton::OnActiveTabChanged(
    BrowserWindowInterface* browser) {
  tab_observation_.Reset();
  if (content::WebContents* contents = ActiveContents()) {
    if (RebaAdblockTabHelper* helper =
            RebaAdblockTabHelper::FromWebContents(contents)) {
      tab_observation_.Observe(helper);
    }
  }
  update_timer_.Stop();
  UpdateState();
}

void RebaAdblockToolbarButton::OnSettingsChanged() {
  UpdateState();
}

void RebaAdblockToolbarButton::OnAdblockServiceShutdown() {
  service_observation_.Reset();
}

void RebaAdblockToolbarButton::OnBlockedCountChanged(
    RebaAdblockTabHelper* helper) {
  // UpdateState() looks the active tab up again, so the timer never holds on
  // to `helper`.
  if (!update_timer_.IsRunning()) {
    update_timer_.Start(FROM_HERE, kUpdateDelay,
                        base::BindOnce(&RebaAdblockToolbarButton::UpdateState,
                                       base::Unretained(this)));
  }
}

void RebaAdblockToolbarButton::OnTabHelperDestroyed(
    RebaAdblockTabHelper* helper) {
  // The tab (and so its helper) is destroyed before the window's views when a
  // window closes; drop the observation now or the destructor would touch a
  // freed helper. The active tab change that follows refreshes the button.
  tab_observation_.Reset();
  update_timer_.Stop();
}

void RebaAdblockToolbarButton::UpdateState() {
  RebaAdblockService* service = RebaAdblockService::Get();
  content::WebContents* contents = ActiveContents();
  const GURL url = contents ? contents->GetLastCommittedURL() : GURL();

  const bool active = service && service->IsEnabled() &&
                      url.SchemeIsHTTPOrHTTPS() && !service->IsSiteTrusted(url);
  // Setting the icon or the text lays the toolbar out again; skip it when
  // nothing changed.
  if (active != shows_active_icon_) {
    shows_active_icon_ = active;
    SetVectorIcon(active ? kRebaAdblockShieldIcon : kRebaAdblockShieldOffIcon);
  }

  std::u16string text;
  if (active && service->ShowBlockedCount() && contents) {
    if (RebaAdblockTabHelper* helper =
            RebaAdblockTabHelper::FromWebContents(contents)) {
      const int count = helper->blocked_count();
      if (count > 999) {
        text = u"999+";
      } else if (count > 0) {
        text = base::NumberToString16(count);
      }
    }
  }
  if (text != GetText()) {
    SetText(text);
  }
}

void RebaAdblockToolbarButton::ButtonPressed() {
  content::WebContents* contents = ActiveContents();
  if (!contents) {
    return;
  }
  RebaAdblockBubble::ShowOrClose(this, browser_->GetProfile(), contents);
}

BEGIN_METADATA(RebaAdblockToolbarButton)
END_METADATA

}  // namespace reba_adblock
