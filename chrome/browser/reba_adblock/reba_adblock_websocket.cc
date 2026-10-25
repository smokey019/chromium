// Copyright 2026 The REBA Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/reba_adblock/reba_adblock_websocket.h"

#include <utility>

#include "chrome/browser/reba_adblock/reba_adblock_service.h"
#include "chrome/browser/reba_adblock/reba_adblock_tab_helper.h"
#include "content/public/browser/browser_context.h"
#include "content/public/browser/render_frame_host.h"
#include "content/public/browser/web_contents.h"
#include "mojo/public/cpp/bindings/remote.h"
#include "net/base/net_errors.h"
#include "net/http/http_request_headers.h"
#include "services/network/public/mojom/network_context.mojom.h"
#include "url/gurl.h"

namespace reba_adblock {

bool ShouldInterceptWebSocket(content::RenderFrameHost* frame) {
  RebaAdblockService* service = RebaAdblockService::Get();
  return frame && service && service->IsEnabled() &&
         frame->GetOutermostMainFrame()
             ->GetLastCommittedURL()
             .SchemeIsHTTPOrHTTPS();
}

bool MaybeBlockWebSocket(
    content::RenderFrameHost* frame,
    const GURL& url,
    mojo::PendingRemote<network::mojom::WebSocketHandshakeClient>&
        handshake_client) {
  RebaAdblockService* service = RebaAdblockService::Get();
  if (!frame || !service) {
    return false;
  }
  const GURL page_url = frame->GetOutermostMainFrame()->GetLastCommittedURL();
  if (!page_url.SchemeIsHTTPOrHTTPS()) {
    return false;
  }
  // The handshake is a GET.
  const Decision decision =
      service->CheckRequest(url, page_url, RequestType::kWebSocket, "GET");
  if (!decision.block) {
    return false;
  }

  content::WebContents* web_contents =
      content::WebContents::FromRenderFrameHost(frame);
  const bool incognito =
      web_contents && web_contents->GetBrowserContext()->IsOffTheRecord();
  service->RecordBlocked(url, RequestType::kWebSocket, page_url, decision,
                         incognito);
  if (web_contents) {
    if (RebaAdblockTabHelper* helper =
            RebaAdblockTabHelper::FromWebContents(web_contents)) {
      helper->OnRequestBlocked(url, RequestType::kWebSocket, decision);
    }
  }

  mojo::Remote<network::mojom::WebSocketHandshakeClient> client(
      std::move(handshake_client));
  client->OnFailure("Blocked by the ad blocker", net::ERR_BLOCKED_BY_CLIENT,
                    /*response_code=*/-1);
  return true;
}

void ConnectWebSocket(
    content::ContentBrowserClient::WebSocketFactory factory,
    const GURL& url,
    const std::optional<std::string>& user_agent,
    mojo::PendingRemote<network::mojom::WebSocketHandshakeClient>
        handshake_client,
    mojo::PendingRemote<network::mojom::TrustedHeaderClient> header_client) {
  // The headers content sends when nobody intercepts
  // (WebSocketConnectorImpl::Connect()), as the webRequest proxy does
  // (WebRequestProxyingWebSocket::StartProxying()): the User-Agent override.
  // DevTools' extra headers are added by `factory` itself.
  std::vector<network::mojom::HttpHeaderPtr> headers;
  if (user_agent) {
    headers.push_back(network::mojom::HttpHeader::New(
        net::HttpRequestHeaders::kUserAgent, *user_agent));
  }
  std::move(factory).Run(url, std::move(headers), std::move(handshake_client),
                         mojo::NullRemote(), std::move(header_client));
}

}  // namespace reba_adblock
