// Copyright 2026 The REBA Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/reba_adblock/reba_adblock_url_loader_throttle.h"

#include <string_view>
#include <utility>

#include "chrome/browser/reba_adblock/reba_adblock_service.h"
#include "net/url_request/redirect_info.h"
#include "services/network/public/cpp/resource_request.h"
#include "url/gurl.h"
#include "url/origin.h"

namespace reba_adblock {

namespace {

// `url` without the parameters that `$removeparam` filters take out, or an
// empty GURL if there is nothing to take out.
GURL CleanedUrl(const GURL& url, std::string_view method) {
  RebaAdblockService* service = RebaAdblockService::Get();
  if (!service || !url.SchemeIsHTTPOrHTTPS()) {
    return GURL();
  }
  const Decision decision =
      service->CheckRequest(url, url, RequestType::kMainFrame, method);
  if (decision.block || decision.rewritten_url.empty()) {
    // A blocked page is RebaAdblockNavigationThrottle's to handle.
    return GURL();
  }
  GURL cleaned(decision.rewritten_url);
  // Filters only take parameters out. Holding them to that keeps the redirect
  // same-origin, as the loader requires, and means a cleaned address is never
  // changed again, so this can't loop.
  if (!cleaned.is_valid() || !url::IsSameOriginWith(url, cleaned) ||
      cleaned.spec().size() >= url.spec().size()) {
    return GURL();
  }
  return cleaned;
}

}  // namespace

// static
std::unique_ptr<blink::URLLoaderThrottle>
RebaAdblockURLLoaderThrottle::MaybeCreate(
    const network::ResourceRequest& request,
    std::optional<int64_t> navigation_id) {
  // Only navigations have an id. Like the rest of the ad blocker, this lives
  // on the UI thread, where navigations are loaded.
  if (!navigation_id || !request.is_outermost_main_frame) {
    return nullptr;
  }
  return std::make_unique<RebaAdblockURLLoaderThrottle>();
}

RebaAdblockURLLoaderThrottle::RebaAdblockURLLoaderThrottle() = default;

RebaAdblockURLLoaderThrottle::~RebaAdblockURLLoaderThrottle() = default;

// Modelled on GoogleURLLoaderThrottle, which forces SafeSearch the same way.
void RebaAdblockURLLoaderThrottle::WillStartRequest(
    network::ResourceRequest* request,
    bool* defer) {
  if (GURL cleaned = CleanedUrl(request->url, request->method);
      !cleaned.is_empty()) {
    request->url = std::move(cleaned);
  }
}

void RebaAdblockURLLoaderThrottle::WillRedirectRequest(
    net::RedirectInfo* redirect_info,
    const network::mojom::URLResponseHead& response_head,
    bool* defer,
    network::HttpRequestHeadersUpdateParams* headers_update_params) {
  // The network then redirects internally from the address the server gave
  // to the cleaned one, without requesting the former, and the navigation
  // follows that redirect too.
  if (GURL cleaned =
          CleanedUrl(redirect_info->new_url, redirect_info->new_method);
      !cleaned.is_empty()) {
    redirect_info->new_url = std::move(cleaned);
  }
}

}  // namespace reba_adblock
