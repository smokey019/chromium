// Copyright 2026 The REBA Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_REBA_ADBLOCK_REBA_ADBLOCK_URL_LOADER_THROTTLE_H_
#define CHROME_BROWSER_REBA_ADBLOCK_REBA_ADBLOCK_URL_LOADER_THROTTLE_H_

#include <memory>
#include <optional>

#include "third_party/blink/public/common/loader/url_loader_throttle.h"

namespace network {
struct ResourceRequest;
}

namespace reba_adblock {

// Applies `$removeparam` filters to top level pages: takes the tracking
// parameters out of the address before the request is made. Changing the URL
// here makes the loader redirect internally (with a 307, so the method, the
// body, the initiator and the referrer are kept), and the navigation goes on
// as it was, only to the cleaned address.
class RebaAdblockURLLoaderThrottle : public blink::URLLoaderThrottle {
 public:
  // A throttle for the navigation `request`, if it is one the filters apply
  // to.
  static std::unique_ptr<blink::URLLoaderThrottle> MaybeCreate(
      const network::ResourceRequest& request,
      std::optional<int64_t> navigation_id);

  RebaAdblockURLLoaderThrottle();
  RebaAdblockURLLoaderThrottle(const RebaAdblockURLLoaderThrottle&) = delete;
  RebaAdblockURLLoaderThrottle& operator=(const RebaAdblockURLLoaderThrottle&) =
      delete;
  ~RebaAdblockURLLoaderThrottle() override;

  // blink::URLLoaderThrottle:
  void WillStartRequest(network::ResourceRequest* request,
                        bool* defer) override;
  void WillRedirectRequest(
      net::RedirectInfo* redirect_info,
      const network::mojom::URLResponseHead& response_head,
      bool* defer,
      network::HttpRequestHeadersUpdateParams* headers_update_params) override;
};

}  // namespace reba_adblock

#endif  // CHROME_BROWSER_REBA_ADBLOCK_REBA_ADBLOCK_URL_LOADER_THROTTLE_H_
