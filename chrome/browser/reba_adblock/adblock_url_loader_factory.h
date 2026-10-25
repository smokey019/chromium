// Copyright 2026 The REBA Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_REBA_ADBLOCK_ADBLOCK_URL_LOADER_FACTORY_H_
#define CHROME_BROWSER_REBA_ADBLOCK_ADBLOCK_URL_LOADER_FACTORY_H_

#include "content/public/browser/content_browser_client.h"
#include "content/public/browser/global_routing_id.h"
#include "mojo/public/cpp/bindings/pending_receiver.h"
#include "mojo/public/cpp/bindings/pending_remote.h"
#include "mojo/public/cpp/bindings/remote.h"
#include "services/network/public/cpp/url_loader_factory_builder.h"
#include "services/network/public/mojom/url_loader_factory.mojom.h"
#include "url/gurl.h"
#include "url/origin.h"

namespace content {
class BrowserContext;
class RenderFrameHost;
}  // namespace content

namespace reba_adblock {

// Sits in front of the network for the requests of pages and workers, and
// fails the ones the ad blocker blocks (or answers them with a stand-in
// resource). Navigations are handled by RebaAdblockNavigationThrottle.
class AdblockURLLoaderFactory : public network::mojom::URLLoaderFactory {
 public:
  // Puts a factory of this kind in front of the one being built, if requests of
  // `type` should be filtered.
  static void MaybeProxy(
      content::BrowserContext* browser_context,
      content::RenderFrameHost* frame,
      content::ContentBrowserClient::URLLoaderFactoryType type,
      const url::Origin& request_initiator,
      network::URLLoaderFactoryBuilder& factory_builder);

  AdblockURLLoaderFactory(const AdblockURLLoaderFactory&) = delete;
  AdblockURLLoaderFactory& operator=(const AdblockURLLoaderFactory&) = delete;
  ~AdblockURLLoaderFactory() override;

  // network::mojom::URLLoaderFactory:
  void CreateLoaderAndStart(
      mojo::PendingReceiver<network::mojom::URLLoader> loader,
      int32_t request_id,
      uint32_t options,
      const network::ResourceRequest& request,
      mojo::PendingRemote<network::mojom::URLLoaderClient> client,
      const net::MutableNetworkTrafficAnnotationTag& traffic_annotation)
      override;
  void Clone(mojo::PendingReceiver<network::mojom::URLLoaderFactory> receiver)
      override;

 private:
  AdblockURLLoaderFactory(
      content::GlobalRenderFrameHostId frame_id,
      bool incognito,
      const url::Origin& request_initiator,
      mojo::PendingRemote<network::mojom::URLLoaderFactory> target_factory);

  // The page to credit and to compare requests against: the top level page of
  // the frame, or for workers the page that created them.
  GURL PageUrl() const;

  const content::GlobalRenderFrameHostId frame_id_;
  // The requests are made off the record, so their addresses aren't logged.
  const bool incognito_;
  const url::Origin request_initiator_;
  mojo::Remote<network::mojom::URLLoaderFactory> target_factory_;
};

}  // namespace reba_adblock

#endif  // CHROME_BROWSER_REBA_ADBLOCK_ADBLOCK_URL_LOADER_FACTORY_H_
