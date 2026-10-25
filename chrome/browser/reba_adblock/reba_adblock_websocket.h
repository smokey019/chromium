// Copyright 2026 The REBA Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_REBA_ADBLOCK_REBA_ADBLOCK_WEBSOCKET_H_
#define CHROME_BROWSER_REBA_ADBLOCK_REBA_ADBLOCK_WEBSOCKET_H_

#include <optional>
#include <string>

#include "content/public/browser/content_browser_client.h"
#include "mojo/public/cpp/bindings/pending_remote.h"
#include "services/network/public/mojom/websocket.mojom.h"

class GURL;

namespace content {
class RenderFrameHost;
}

namespace reba_adblock {

// Whether the ad blocker wants to see the WebSocket connections that `frame`
// makes: when it's on and the frame belongs to a web page.
bool ShouldInterceptWebSocket(content::RenderFrameHost* frame);

// If the ad blocker blocks the connection to `url`, fails the handshake and
// returns true.
bool MaybeBlockWebSocket(
    content::RenderFrameHost* frame,
    const GURL& url,
    mojo::PendingRemote<network::mojom::WebSocketHandshakeClient>&
        handshake_client);

// Connects a WebSocket that only the ad blocker intercepted, the way content
// would have without it.
void ConnectWebSocket(
    content::ContentBrowserClient::WebSocketFactory factory,
    const GURL& url,
    const std::optional<std::string>& user_agent,
    mojo::PendingRemote<network::mojom::WebSocketHandshakeClient>
        handshake_client,
    mojo::PendingRemote<network::mojom::TrustedHeaderClient> header_client);

}  // namespace reba_adblock

#endif  // CHROME_BROWSER_REBA_ADBLOCK_REBA_ADBLOCK_WEBSOCKET_H_
