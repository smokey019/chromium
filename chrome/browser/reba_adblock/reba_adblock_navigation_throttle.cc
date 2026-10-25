// Copyright 2026 The REBA Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/reba_adblock/reba_adblock_navigation_throttle.h"

#include <memory>
#include <utility>

#include "base/functional/bind.h"
#include "base/strings/strcat.h"
#include "base/task/single_thread_task_runner.h"
#include "chrome/browser/reba_adblock/reba_adblock_service.h"
#include "chrome/browser/reba_adblock/reba_adblock_tab_helper.h"
#include "chrome/common/webui_url_constants.h"
#include "components/blocked_content/popup_tracker.h"
#include "content/public/browser/browser_context.h"
#include "content/public/browser/navigation_controller.h"
#include "content/public/browser/navigation_handle.h"
#include "content/public/browser/navigation_throttle_registry.h"
#include "content/public/browser/page_navigator.h"
#include "content/public/browser/render_frame_host.h"
#include "content/public/browser/web_contents.h"
#include "net/base/net_errors.h"
#include "net/base/url_util.h"
#include "ui/base/page_transition_types.h"
#include "ui/base/window_open_disposition.h"
#include "url/origin.h"

namespace reba_adblock {

namespace {

// What a blocked frame shows: nothing.
constexpr char kEmptyFrameHtml[] =
    "<!doctype html><html><head><meta name=\"color-scheme\" content=\"light "
    "dark\"></head><body></body></html>";

// The longest a page waits for the first engine at startup.
constexpr base::TimeDelta kMaxWaitForFirstEngine = base::Seconds(3);

void ShowBlockedPage(base::WeakPtr<content::WebContents> web_contents,
                     GURL blocked_page) {
  if (!web_contents) {
    return;
  }
  content::OpenURLParams params(blocked_page, content::Referrer(),
                                WindowOpenDisposition::kCurrentTab,
                                ui::PAGE_TRANSITION_AUTO_TOPLEVEL,
                                /*is_renderer_initiated=*/false);
  web_contents->OpenURL(params, /*navigation_handle_callback=*/{});
}

// Closes a tab that a page opened and whose first load was blocked, the way
// window.close() would.
void ClosePopup(base::WeakPtr<content::WebContents> web_contents) {
  if (web_contents) {
    web_contents->ClosePage();
  }
}

// Whether `handle` is a load of a tab that a page opened itself
// (window.open(), a link with target=_blank, ...), as opposed to one the user
// opened, and that nobody has used yet. Such loads start in the page that asked
// for them; the user's own ("open in new tab", the address bar, a restored
// tab) are started by the browser.
//
// That is the first load (including the redirects of the request), and the
// hops after it that the landing page makes by itself: ad pop-ups often land on
// a page that then sends the tab on to the real ad.
bool IsPopupLoad(content::NavigationHandle* handle) {
  if (!handle->IsInPrimaryMainFrame() || !handle->IsRendererInitiated()) {
    return false;
  }
  content::WebContents* web_contents = handle->GetWebContents();
  content::NavigationController& controller = web_contents->GetController();
  if (controller.IsInitialNavigation()) {
    return true;
  }
  return blocked_content::PopupTracker::FromWebContents(web_contents) &&
         !handle->HasUserGesture() && controller.GetEntryCount() <= 1;
}

}  // namespace

// static
void RebaAdblockNavigationThrottle::MaybeCreateAndAdd(
    content::NavigationThrottleRegistry& registry) {
  content::NavigationHandle& handle = registry.GetNavigationHandle();
  if (!handle.GetURL().SchemeIsHTTPOrHTTPS()) {
    return;
  }
  registry.AddThrottle(
      base::WrapUnique(new RebaAdblockNavigationThrottle(registry)));
}

// static
GURL RebaAdblockNavigationThrottle::BlockedPageUrl(const GURL& blocked_url,
                                                   const Decision& decision) {
  GURL url(base::StrCat({"chrome://", chrome::kChromeUIRebaBlockedHost, "/"}));
  url = net::AppendQueryParameter(url, "url", blocked_url.spec());
  url = net::AppendQueryParameter(url, "filter", decision.filter);
  url = net::AppendQueryParameter(url, "list", decision.list_title);
  url = net::AppendQueryParameter(url, "listid", decision.list_id);
  return url;
}

RebaAdblockNavigationThrottle::RebaAdblockNavigationThrottle(
    content::NavigationThrottleRegistry& registry)
    : content::NavigationThrottle(registry) {}

RebaAdblockNavigationThrottle::~RebaAdblockNavigationThrottle() = default;

content::NavigationThrottle::ThrottleCheckResult
RebaAdblockNavigationThrottle::WillStartRequest() {
  // Restored tabs start loading as soon as the browser is up, often before
  // the engine is loaded from disk. Hold top level pages back until it is (or
  // for a few seconds at most), as SafeSitesNavigationThrottle does while it
  // asks its service.
  RebaAdblockService* service = RebaAdblockService::Get();
  if (service && service->IsWaitingForFirstEngine() &&
      navigation_handle()->IsInOutermostMainFrame()) {
    waiting_ = true;
    service_observation_.Observe(service);
    wait_timer_.Start(
        FROM_HERE, kMaxWaitForFirstEngine,
        base::BindOnce(&RebaAdblockNavigationThrottle::StopWaiting,
                       base::Unretained(this)));
    return DEFER;
  }
  return Check();
}

content::NavigationThrottle::ThrottleCheckResult
RebaAdblockNavigationThrottle::WillRedirectRequest() {
  return Check();
}

const char* RebaAdblockNavigationThrottle::GetNameForLogging() {
  return "RebaAdblockNavigationThrottle";
}

void RebaAdblockNavigationThrottle::OnEnginesChanged() {
  StopWaiting();
}

void RebaAdblockNavigationThrottle::OnAdblockServiceShutdown() {
  // The timer still ends the wait.
  service_observation_.Reset();
}

void RebaAdblockNavigationThrottle::StopWaiting() {
  if (!waiting_) {
    return;
  }
  waiting_ = false;
  wait_timer_.Stop();
  service_observation_.Reset();
  const ThrottleCheckResult result = Check();
  // Resume() and CancelDeferredNavigation() may delete this throttle.
  if (result.action() == PROCEED) {
    Resume();
  } else {
    CancelDeferredNavigation(result);
  }
}

content::NavigationThrottle::ThrottleCheckResult
RebaAdblockNavigationThrottle::Check() {
  RebaAdblockService* service = RebaAdblockService::Get();
  content::NavigationHandle* handle = navigation_handle();
  if (!service || !service->IsEnabled() ||
      !handle->GetURL().SchemeIsHTTPOrHTTPS()) {
    return PROCEED;
  }

  const GURL url = handle->GetURL();
  const bool main_frame = handle->IsInOutermostMainFrame();
  GURL page_url = url;
  if (!main_frame) {
    content::RenderFrameHost* parent = handle->GetParentFrameOrOuterDocument();
    if (parent) {
      page_url = parent->GetOutermostMainFrame()->GetLastCommittedURL();
    }
  }

  RequestType type =
      main_frame ? RequestType::kMainFrame : RequestType::kSubFrame;
  Decision decision;

  // A tab that a page opened is judged as a pop-up first, from the page that
  // opened it. Most pop-ups are refused when they are created (see
  // ShouldBlockPopup()); this catches the ones that open as about:blank or
  // redirect to an ad.
  const bool popup = main_frame && IsPopupLoad(handle);
  if (popup) {
    GURL opener_url = url;
    if (handle->GetInitiatorOrigin() &&
        !handle->GetInitiatorOrigin()->opaque()) {
      opener_url = handle->GetInitiatorOrigin()->GetURL();
    }
    decision = service->CheckRequest(url, opener_url, RequestType::kPopup,
                                     handle->GetRequestMethod());
    if (decision.block) {
      type = RequestType::kPopup;
      page_url = opener_url;
    }
  }
  if (!decision.block) {
    decision =
        service->CheckRequest(url, page_url, type, handle->GetRequestMethod());
  }
  if (!decision.block) {
    return PROCEED;
  }

  if (main_frame) {
    if (!handle->IsInPrimaryMainFrame()) {
      // A page that isn't shown (yet), such as a prerendered one. It must not
      // touch the tab the user sees; just don't load it.
      return ThrottleCheckResult(CANCEL, net::ERR_BLOCKED_BY_CLIENT);
    }
    if (popup) {
      // Nobody asked for this tab, so don't leave an empty or "blocked" one
      // behind: count it for the page that opened it and close it.
      content::WebContents* web_contents = handle->GetWebContents();
      service->RecordBlocked(url, type, page_url, decision,
                             web_contents->GetBrowserContext()->IsOffTheRecord());
      if (content::RenderFrameHost* opener_frame = web_contents->GetOpener()) {
        if (RebaAdblockTabHelper* helper = RebaAdblockTabHelper::FromWebContents(
                content::WebContents::FromRenderFrameHost(opener_frame))) {
          helper->OnRequestBlocked(url, type, decision);
        }
      }
      base::SingleThreadTaskRunner::GetCurrentDefault()->PostTask(
          FROM_HERE, base::BindOnce(&ClosePopup, web_contents->GetWeakPtr()));
      return CANCEL_AND_IGNORE;
    }
    service->RecordBlocked(
        url, type, page_url, decision,
        handle->GetWebContents()->GetBrowserContext()->IsOffTheRecord());
    base::SingleThreadTaskRunner::GetCurrentDefault()->PostTask(
        FROM_HERE,
        base::BindOnce(&ShowBlockedPage, handle->GetWebContents()->GetWeakPtr(),
                       BlockedPageUrl(url, decision)));
    return CANCEL_AND_IGNORE;
  }

  service->RecordBlocked(
      url, type, page_url, decision,
      handle->GetWebContents()->GetBrowserContext()->IsOffTheRecord());
  if (RebaAdblockTabHelper* helper =
          RebaAdblockTabHelper::FromWebContents(handle->GetWebContents())) {
    helper->OnRequestBlocked(url, type, decision);
  }
  return ThrottleCheckResult(CANCEL, net::ERR_BLOCKED_BY_CLIENT,
                             kEmptyFrameHtml);
}

}  // namespace reba_adblock
