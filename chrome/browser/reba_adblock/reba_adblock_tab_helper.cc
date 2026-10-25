// Copyright 2026 The REBA Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/reba_adblock/reba_adblock_tab_helper.h"

#include <string_view>
#include <utility>

#include "chrome/browser/reba_adblock/reba_adblock_service.h"
#include "components/reba_adblock/common/reba_adblock.mojom.h"
#include "content/public/browser/browser_context.h"
#include "content/public/browser/navigation_controller.h"
#include "content/public/browser/navigation_handle.h"
#include "content/public/browser/render_frame_host.h"
#include "content/public/browser/web_contents.h"
#include "mojo/public/cpp/bindings/associated_remote.h"
#include "third_party/blink/public/common/associated_interfaces/associated_interface_provider.h"

namespace reba_adblock {

namespace {
// The popup lists the most recent blocked URLs of the page; more than this
// isn't useful.
constexpr size_t kMaxBlockedItems = 300;
// Addresses can be megabytes long (data in the query); the popup only needs
// to show where a request went.
constexpr size_t kMaxItemUrlLength = 2048;
// How long after the element picker starts it may add a filter.
constexpr base::TimeDelta kPickerTimeout = base::Minutes(10);
}  // namespace

WEB_CONTENTS_USER_DATA_KEY_IMPL(RebaAdblockTabHelper);

RebaAdblockTabHelper::RebaAdblockTabHelper(content::WebContents* web_contents)
    : content::WebContentsObserver(web_contents),
      content::WebContentsUserData<RebaAdblockTabHelper>(*web_contents) {}

RebaAdblockTabHelper::~RebaAdblockTabHelper() {
  for (Observer& observer : observers_) {
    observer.OnTabHelperDestroyed(this);
  }
}

void RebaAdblockTabHelper::OnRequestBlocked(const GURL& url,
                                            RequestType type,
                                            const Decision& decision) {
  ++blocked_count_;
  if (blocked_items_.size() >= kMaxBlockedItems) {
    blocked_items_.erase(blocked_items_.begin());
  }
  blocked_items_.push_back({.url = url.spec().substr(0, kMaxItemUrlLength),
                            .type = RequestTypeName(type),
                            .filter = decision.filter,
                            .list_title = decision.list_title});
  NotifyObservers();
}

void RebaAdblockTabHelper::AddHiddenElements(int count) {
  hidden_element_count_ += count;
  NotifyObservers();
}

void RebaAdblockTabHelper::DidFinishNavigation(
    content::NavigationHandle* handle) {
  if (!handle->IsInPrimaryMainFrame() || !handle->HasCommitted() ||
      handle->IsSameDocument()) {
    return;
  }
  blocked_count_ = 0;
  hidden_element_count_ = 0;
  blocked_items_.clear();
  EndPicker();
  NotifyObservers();
}

bool RebaAdblockTabHelper::picker_active() const {
  return !picker_started_.is_null() &&
         base::TimeTicks::Now() - picker_started_ < kPickerTimeout;
}

void RebaAdblockTabHelper::StartPicker(bool zapper) {
  // The zapper only removes elements on the spot; it never adds a filter.
  picker_started_ = zapper ? base::TimeTicks() : base::TimeTicks::Now();
  mojo::AssociatedRemote<mojom::PickerAgent> agent;
  web_contents()
      ->GetPrimaryMainFrame()
      ->GetRemoteAssociatedInterfaces()
      ->GetInterface(&agent);
  agent->StartPicker(zapper);
  web_contents()->Focus();
}

void RebaAdblockTabHelper::SetSiteTrusted(bool trusted) {
  RebaAdblockService* service = RebaAdblockService::Get();
  const GURL url = web_contents()->GetLastCommittedURL();
  if (!service || !url.SchemeIsHTTPOrHTTPS()) {
    return;
  }
  const std::string host(url.host());
  if (trusted) {
    // Off the record, nothing may be written to Local State.
    service->AddTrustedSite(
        host,
        /*persist=*/!web_contents()->GetBrowserContext()->IsOffTheRecord());
  } else {
    // The site may be trusted through a parent domain, saved or for the
    // session.
    std::string_view domain = host;
    while (!domain.empty()) {
      service->RemoveTrustedSite(std::string(domain));
      const size_t dot = domain.find('.');
      domain = dot == std::string_view::npos ? std::string_view()
                                             : domain.substr(dot + 1);
    }
  }
  // The page only changes after a reload.
  web_contents()->GetController().Reload(content::ReloadType::NORMAL, false);
}

void RebaAdblockTabHelper::NotifyObservers() {
  for (Observer& observer : observers_) {
    observer.OnBlockedCountChanged(this);
  }
}

}  // namespace reba_adblock
