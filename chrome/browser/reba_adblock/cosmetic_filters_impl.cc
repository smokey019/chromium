// Copyright 2026 The REBA Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/reba_adblock/cosmetic_filters_impl.h"

#include <algorithm>
#include <string_view>
#include <utility>

#include "chrome/browser/reba_adblock/reba_adblock_service.h"
#include "chrome/browser/reba_adblock/reba_adblock_tab_helper.h"
#include "components/reba_adblock/core/engine_set.h"
#include "content/public/browser/browser_context.h"
#include "content/public/browser/render_frame_host.h"
#include "content/public/browser/web_contents.h"

namespace reba_adblock {

namespace {

// A page could ask with absurd amounts of data; more than this is no real
// page.
constexpr size_t kMaxNamesPerRequest = 2000;
// The element picker makes short selectors.
constexpr size_t kMaxSelectorLength = 1000;
// Refused pop-ups one document may report, so a page can't flood the log.
constexpr int kMaxPopupReportsPerDocument = 20;

void Truncate(std::vector<std::string>& v) {
  if (v.size() > kMaxNamesPerRequest) {
    v.resize(kMaxNamesPerRequest);
  }
}

// Whether `selector` can only make a plain `host##selector` hiding rule: no
// line breaks that would start another rule, and none of the separators of
// the other kinds of cosmetic rules (scriptlets, exceptions, procedural and
// AdGuard rules) that a parser might pick instead.
bool IsPlainSelector(std::string_view selector) {
  if (selector.empty() || selector.size() > kMaxSelectorLength ||
      selector.find_first_of("\r\n") != std::string_view::npos) {
    return false;
  }
  for (std::string_view marker :
       {"+js(", "##", "#@#", "#?#", "#@?#", "#$#", "#@$#", "#%#", "#@%#"}) {
    if (selector.find(marker) != std::string_view::npos) {
      return false;
    }
  }
  return true;
}

}  // namespace

// static
void CosmeticFiltersImpl::Create(
    content::RenderFrameHost* host,
    mojo::PendingReceiver<mojom::CosmeticFilters> receiver) {
  // Owned by the frame.
  new CosmeticFiltersImpl(*host, std::move(receiver));
}

CosmeticFiltersImpl::CosmeticFiltersImpl(
    content::RenderFrameHost& host,
    mojo::PendingReceiver<mojom::CosmeticFilters> receiver)
    : content::DocumentService<mojom::CosmeticFilters>(host,
                                                       std::move(receiver)) {}

CosmeticFiltersImpl::~CosmeticFiltersImpl() = default;

GURL CosmeticFiltersImpl::TopLevelUrl() const {
  return render_frame_host().GetOutermostMainFrame()->GetLastCommittedURL();
}

void CosmeticFiltersImpl::GetCosmeticResources(
    const GURL& url,
    GetCosmeticResourcesCallback callback) {
  auto resources = mojom::CosmeticResources::New();
  RebaAdblockService* service = RebaAdblockService::Get();
  // For the top level frame the browser may not know yet where it is, so go by
  // what the renderer says; the filters for an address aren't secret. Frames
  // below it have a top level page that is committed.
  const GURL top_level =
      render_frame_host().GetParentOrOuterDocument() ? TopLevelUrl() : url;
  if (service && service->IsEnabled() && url.SchemeIsHTTPOrHTTPS() &&
      !service->IsSiteTrusted(top_level)) {
    resources->popup_guard = service->PopupGuardEnabled();
  }
  if (service && service->IsCosmeticFilteringEnabled() &&
      url.SchemeIsHTTPOrHTTPS() && !service->IsSiteTrusted(top_level)) {
    if (scoped_refptr<const EngineSet> engines = service->engines()) {
      PageCosmetics page = engines->GetPageCosmetics(url);
      resources->enabled = true;
      resources->hide_selectors = std::move(page.hide_selectors);
      resources->procedural_actions = std::move(page.procedural_actions);
      resources->generic_exceptions = std::move(page.generic_exceptions);
      resources->injected_script = std::move(page.injected_script);
      resources->generichide = page.generichide;
    }
  }
  std::move(callback).Run(std::move(resources));
}

void CosmeticFiltersImpl::HiddenClassIdSelectors(
    const std::vector<std::string>& classes,
    const std::vector<std::string>& ids,
    const std::vector<std::string>& exceptions,
    HiddenClassIdSelectorsCallback callback) {
  std::vector<std::string> selectors;
  RebaAdblockService* service = RebaAdblockService::Get();
  if (service && service->IsCosmeticFilteringEnabled() &&
      !service->IsSiteTrusted(TopLevelUrl())) {
    if (scoped_refptr<const EngineSet> engines = service->engines()) {
      std::vector<std::string> c = classes, i = ids, e = exceptions;
      Truncate(c);
      Truncate(i);
      Truncate(e);
      selectors = engines->HiddenClassIdSelectors(
          render_frame_host().GetLastCommittedURL(), c, i, e);
    }
  }
  std::move(callback).Run(std::move(selectors));
}

void CosmeticFiltersImpl::ReportHiddenElements(uint32_t count) {
  // A prerendered page isn't the one the tab shows (yet). The interface is
  // granted to prerendered pages (see
  // chrome_content_browser_client_binder_policies.cc) for
  // GetCosmeticResources(), so calls that change state are ignored here.
  if (render_frame_host().IsInLifecycleState(
          content::RenderFrameHost::LifecycleState::kPrerendering)) {
    return;
  }
  content::WebContents* web_contents =
      content::WebContents::FromRenderFrameHost(&render_frame_host());
  if (!web_contents) {
    return;
  }
  if (RebaAdblockTabHelper* helper =
          RebaAdblockTabHelper::FromWebContents(web_contents)) {
    helper->AddHiddenElements(
        static_cast<int>(std::min<uint32_t>(count, 10000)));
  }
}

void CosmeticFiltersImpl::ReportBlockedPopup(const GURL& url) {
  // As in ReportHiddenElements(): a prerendered page isn't shown yet.
  if (render_frame_host().IsInLifecycleState(
          content::RenderFrameHost::LifecycleState::kPrerendering)) {
    return;
  }
  RebaAdblockService* service = RebaAdblockService::Get();
  content::WebContents* web_contents =
      content::WebContents::FromRenderFrameHost(&render_frame_host());
  if (!service || !web_contents || !service->IsEnabled() ||
      !service->PopupGuardEnabled() ||
      ++reported_popups_ > kMaxPopupReportsPerDocument) {
    return;
  }
  const GURL page_url = TopLevelUrl();
  if (!page_url.SchemeIsHTTPOrHTTPS() || service->IsSiteTrusted(page_url)) {
    return;
  }
  Decision decision;
  decision.block = true;
  decision.list_title = "Pop-up guard";
  decision.filter = "Pop-up opened by a click that was not on a link for it";
  service->RecordBlocked(url, RequestType::kPopup, page_url, decision,
                         web_contents->GetBrowserContext()->IsOffTheRecord());
  if (RebaAdblockTabHelper* helper =
          RebaAdblockTabHelper::FromWebContents(web_contents)) {
    helper->OnRequestBlocked(url, RequestType::kPopup, decision);
  }
}

void CosmeticFiltersImpl::AddCosmeticFilter(const std::string& selector) {
  // The element picker runs in the main frame of the page the tab shows, so
  // nothing else (an ad's frame, a prerendered page) may add filters.
  if (!render_frame_host().IsInPrimaryMainFrame() ||
      !IsPlainSelector(selector)) {
    return;
  }
  content::WebContents* web_contents =
      content::WebContents::FromRenderFrameHost(&render_frame_host());
  RebaAdblockService* service = RebaAdblockService::Get();
  if (!web_contents || !service) {
    return;
  }
  RebaAdblockTabHelper* helper =
      RebaAdblockTabHelper::FromWebContents(web_contents);
  if (!helper || !helper->picker_active()) {
    // Only the element picker, started by the user, may add filters, and
    // only one each time it is started.
    return;
  }
  const std::string host(render_frame_host().GetLastCommittedURL().host());
  if (host.empty()) {
    return;
  }
  helper->EndPicker();
  // Off the record, the filter only lasts until the browser closes.
  service->AddUserFilter(
      host + "##" + selector,
      /*persist=*/!web_contents->GetBrowserContext()->IsOffTheRecord());
}

}  // namespace reba_adblock
