// Copyright 2026 The REBA Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/reba_adblock/adblock_url_loader_factory.h"

#include <utility>

#include "base/byte_size.h"
#include "base/functional/bind.h"
#include "base/memory/raw_ptr.h"
#include "chrome/browser/reba_adblock/reba_adblock_service.h"
#include "chrome/browser/reba_adblock/reba_adblock_tab_helper.h"
#include "content/public/browser/browser_context.h"
#include "content/public/browser/render_frame_host.h"
#include "content/public/browser/web_contents.h"
#include "mojo/public/cpp/bindings/receiver.h"
#include "mojo/public/cpp/bindings/self_owned_receiver.h"
#include "mojo/public/cpp/system/data_pipe.h"
#include "net/base/data_url.h"
#include "net/base/net_errors.h"
#include "net/http/http_response_headers.h"
#include "services/network/public/cpp/resource_request.h"
#include "services/network/public/cpp/url_loader_completion_status.h"
#include "services/network/public/mojom/early_hints.mojom.h"
#include "services/network/public/mojom/fetch_api.mojom.h"
#include "services/network/public/mojom/url_loader.mojom.h"
#include "services/network/public/mojom/url_response_head.mojom.h"

namespace reba_adblock {

namespace {

using URLLoaderFactoryType =
    content::ContentBrowserClient::URLLoaderFactoryType;

RequestType TypeFor(const network::ResourceRequest& request) {
  using Destination = network::mojom::RequestDestination;
  switch (request.destination) {
    case Destination::kScript:
    case Destination::kServiceWorker:
    case Destination::kSharedWorker:
    case Destination::kWorker:
    case Destination::kAudioWorklet:
    case Destination::kPaintWorklet:
      return RequestType::kScript;
    case Destination::kStyle:
    case Destination::kXslt:
      return RequestType::kStylesheet;
    case Destination::kImage:
      return RequestType::kImage;
    case Destination::kFont:
      return RequestType::kFont;
    case Destination::kAudio:
    case Destination::kVideo:
    case Destination::kTrack:
      return RequestType::kMedia;
    case Destination::kObject:
    case Destination::kEmbed:
      return RequestType::kObject;
    case Destination::kDocument:
    case Destination::kFrame:
    case Destination::kIframe:
    case Destination::kFencedframe:
      return request.is_outermost_main_frame ? RequestType::kMainFrame
                                             : RequestType::kSubFrame;
    case Destination::kReport:
      return RequestType::kCspReport;
    case Destination::kEmpty:
      return request.keepalive ? RequestType::kPing : RequestType::kXhr;
    default:
      return RequestType::kOther;
  }
}

bool IsFilterable(const GURL& url) {
  return url.SchemeIsHTTPOrHTTPS() || url.SchemeIsWSOrWSS();
}

// The page that the frame belongs to, as far as ad blocking is concerned.
GURL PageUrlForFrame(content::GlobalRenderFrameHostId frame_id,
                     const url::Origin& fallback_origin) {
  if (content::RenderFrameHost* frame =
          content::RenderFrameHost::FromID(frame_id)) {
    const GURL page = frame->GetOutermostMainFrame()->GetLastCommittedURL();
    if (page.is_valid() && !page.is_empty()) {
      return page;
    }
  }
  return fallback_origin.GetURL();
}

void RecordBlock(content::GlobalRenderFrameHostId frame_id,
                 bool incognito,
                 const GURL& url,
                 RequestType type,
                 const GURL& page_url,
                 const Decision& decision) {
  if (RebaAdblockService* service = RebaAdblockService::Get()) {
    service->RecordBlocked(url, type, page_url, decision, incognito);
  }
  content::RenderFrameHost* frame = content::RenderFrameHost::FromID(frame_id);
  content::WebContents* web_contents =
      frame ? content::WebContents::FromRenderFrameHost(frame) : nullptr;
  if (web_contents) {
    if (RebaAdblockTabHelper* helper =
            RebaAdblockTabHelper::FromWebContents(web_contents)) {
      helper->OnRequestBlocked(url, type, decision);
    }
  }
}

// One request that went through the factory. It owns itself. As in the
// webRequest proxy (extensions/browser/api/web_request/
// web_request_proxying_url_loader_factory.cc), it goes away with the request:
// when the network says the request is complete, or when the renderer drops
// it (an aborted fetch, a page that is closed), which also cancels it in the
// network by closing its pipes.
class ProxiedRequest : public network::mojom::URLLoader,
                       public network::mojom::URLLoaderClient {
 public:
  ProxiedRequest(content::GlobalRenderFrameHostId frame_id,
                 bool incognito,
                 GURL page_url,
                 RequestType type,
                 GURL url,
                 mojo::PendingReceiver<network::mojom::URLLoader> loader,
                 mojo::PendingRemote<network::mojom::URLLoaderClient> client)
      : frame_id_(frame_id),
        incognito_(incognito),
        page_url_(std::move(page_url)),
        type_(type),
        url_(std::move(url)),
        loader_receiver_(this, std::move(loader)),
        target_client_(std::move(client)) {
    // Everything the renderer sent before closing a pipe has been handled
    // by the time the pipe's disconnection is, and it no longer wants
    // anything back.
    loader_receiver_.set_disconnect_handler(
        base::BindOnce(&ProxiedRequest::Destroy, base::Unretained(this)));
    target_client_.set_disconnect_handler(
        base::BindOnce(&ProxiedRequest::Destroy, base::Unretained(this)));
  }
  ProxiedRequest(const ProxiedRequest&) = delete;
  ProxiedRequest& operator=(const ProxiedRequest&) = delete;
  ~ProxiedRequest() override = default;

  // Sends the request on to the network.
  void Forward(network::mojom::URLLoaderFactory* target,
               int32_t request_id,
               uint32_t options,
               const network::ResourceRequest& request,
               const net::MutableNetworkTrafficAnnotationTag& annotation) {
    mojo::PendingRemote<network::mojom::URLLoaderClient> proxy_client =
        client_receiver_.BindNewPipeAndPassRemote();
    client_receiver_.set_disconnect_handler(base::BindOnce(
        &ProxiedRequest::OnNetworkDisconnected, base::Unretained(this)));
    target->CreateLoaderAndStart(target_loader_.BindNewPipeAndPassReceiver(),
                                 request_id, options, request,
                                 std::move(proxy_client), annotation);
  }

  // Ends the request with an error that makes the element that asked for it
  // collapse, as for any request the browser blocks.
  void Block(const Decision& decision) {
    RecordBlock(frame_id_, incognito_, url_, type_, page_url_, decision);
    network::URLLoaderCompletionStatus status(net::ERR_BLOCKED_BY_CLIENT);
    status.should_collapse_initiator = true;
    target_client_->OnComplete(status);
    // The loader receiver is closed with this object.
    Destroy();
  }

  // Answers with the body of a `data:` URL, as a request that succeeded.
  void Serve(const Decision& decision) {
    std::string mime_type, charset, data;
    if (!net::DataURL::Parse(GURL(decision.redirect_url), &mime_type, &charset,
                             &data)) {
      Block(decision);
      return;
    }
    mojo::ScopedDataPipeProducerHandle producer;
    mojo::ScopedDataPipeConsumerHandle consumer;
    if (mojo::CreateDataPipe(std::max<size_t>(data.size(), 1), producer,
                             consumer) != MOJO_RESULT_OK) {
      Block(decision);
      return;
    }
    size_t written = data.size();
    if (!data.empty() && producer->WriteData(base::as_byte_span(data),
                                             MOJO_WRITE_DATA_FLAG_ALL_OR_NONE,
                                             written) != MOJO_RESULT_OK) {
      Block(decision);
      return;
    }
    RecordBlock(frame_id_, incognito_, url_, type_, page_url_, decision);

    auto head = network::mojom::URLResponseHead::New();
    head->mime_type = mime_type;
    head->charset = charset;
    head->content_length = static_cast<int64_t>(data.size());
    head->request_time = base::Time::Now();
    head->response_time = base::Time::Now();
    head->headers = base::MakeRefCounted<net::HttpResponseHeaders>(
        "HTTP/1.1 200 OK\nContent-Type: " + mime_type +
        "\nAccess-Control-Allow-Origin: *\nCross-Origin-Resource-Policy: "
        "cross-origin\n");
    target_client_->OnReceiveResponse(std::move(head), std::move(consumer),
                                      std::nullopt);
    network::URLLoaderCompletionStatus status(net::OK);
    status.encoded_data_length = base::ByteSize(data.size());
    status.encoded_body_length = base::ByteSize(data.size());
    status.decoded_body_length = base::ByteSize(data.size());
    target_client_->OnComplete(status);
    Destroy();
  }

  void Destroy() { delete this; }

  // The network closed its end without saying how the request ended (it
  // always calls OnComplete() first, which is handled before this).
  void OnNetworkDisconnected() {
    target_client_->OnComplete(
        network::URLLoaderCompletionStatus(net::ERR_ABORTED));
    Destroy();
  }

  // network::mojom::URLLoader:
  void FollowRedirect(
      network::HttpRequestHeadersUpdateParams headers_update_params,
      const std::optional<GURL>& new_url) override {
    if (target_loader_) {
      target_loader_->FollowRedirect(std::move(headers_update_params), new_url);
    }
  }
  void SetPriority(net::RequestPriority priority,
                   int32_t intra_priority_value) override {
    if (target_loader_) {
      target_loader_->SetPriority(priority, intra_priority_value);
    }
  }

  // network::mojom::URLLoaderClient:
  void OnReceiveEarlyHints(network::mojom::EarlyHintsPtr early_hints) override {
    target_client_->OnReceiveEarlyHints(std::move(early_hints));
  }
  void OnReceiveResponse(
      network::mojom::URLResponseHeadPtr head,
      mojo::ScopedDataPipeConsumerHandle body,
      std::optional<mojo_base::BigBuffer> cached_metadata) override {
    target_client_->OnReceiveResponse(std::move(head), std::move(body),
                                      std::move(cached_metadata));
  }
  void OnReceiveRedirect(const net::RedirectInfo& redirect_info,
                         network::mojom::URLResponseHeadPtr head) override {
    // A redirect can lead to something that is blocked; check where it goes.
    if (RebaAdblockService* service = RebaAdblockService::Get();
        service && IsFilterable(redirect_info.new_url)) {
      const Decision decision = service->CheckRequest(
          redirect_info.new_url, page_url_, type_, redirect_info.new_method);
      if (decision.block) {
        url_ = redirect_info.new_url;
        // Abandon the real request.
        target_loader_.reset();
        client_receiver_.reset();
        Block(decision);
        return;
      }
    }
    url_ = redirect_info.new_url;
    target_client_->OnReceiveRedirect(redirect_info, std::move(head));
  }
  void OnUploadProgress(int64_t current_position,
                        int64_t total_size,
                        OnUploadProgressCallback callback) override {
    target_client_->OnUploadProgress(current_position, total_size,
                                     std::move(callback));
  }
  void OnTransferSizeUpdated(int32_t transfer_size_diff) override {
    target_client_->OnTransferSizeUpdated(transfer_size_diff);
  }
  void OnComplete(const network::URLLoaderCompletionStatus& status) override {
    target_client_->OnComplete(status);
    // The body, if any, goes straight to the renderer through its data pipe;
    // nothing more comes through here.
    Destroy();
  }

 private:
  const content::GlobalRenderFrameHostId frame_id_;
  const bool incognito_;
  const GURL page_url_;
  const RequestType type_;
  GURL url_;

  // The renderer's end.
  mojo::Receiver<network::mojom::URLLoader> loader_receiver_;
  mojo::Remote<network::mojom::URLLoaderClient> target_client_;

  // The network's end.
  mojo::Receiver<network::mojom::URLLoaderClient> client_receiver_{this};
  mojo::Remote<network::mojom::URLLoader> target_loader_;
};

}  // namespace

// static
void AdblockURLLoaderFactory::MaybeProxy(
    content::BrowserContext* browser_context,
    content::RenderFrameHost* frame,
    URLLoaderFactoryType type,
    const url::Origin& request_initiator,
    network::URLLoaderFactoryBuilder& factory_builder) {
  switch (type) {
    case URLLoaderFactoryType::kDocumentSubResource:
    case URLLoaderFactoryType::kWorkerMainResource:
    case URLLoaderFactoryType::kWorkerSubResource:
    case URLLoaderFactoryType::kServiceWorkerScript:
    case URLLoaderFactoryType::kServiceWorkerSubResource:
      break;
    default:
      return;
  }
  // The on/off switch is checked for each request, so that turning the ad
  // blocker on reaches pages that are already open.
  auto [receiver, target] = factory_builder.Append();
  const content::GlobalRenderFrameHostId frame_id =
      frame ? frame->GetGlobalId() : content::GlobalRenderFrameHostId();
  // Workers (and fetchLater) have no frame to ask, so whether the requests
  // are off the record comes from the profile.
  mojo::MakeSelfOwnedReceiver(base::WrapUnique(new AdblockURLLoaderFactory(
                                  frame_id, browser_context->IsOffTheRecord(),
                                  request_initiator, std::move(target))),
                              std::move(receiver));
}

AdblockURLLoaderFactory::AdblockURLLoaderFactory(
    content::GlobalRenderFrameHostId frame_id,
    bool incognito,
    const url::Origin& request_initiator,
    mojo::PendingRemote<network::mojom::URLLoaderFactory> target_factory)
    : frame_id_(frame_id),
      incognito_(incognito),
      request_initiator_(request_initiator),
      target_factory_(std::move(target_factory)) {}

AdblockURLLoaderFactory::~AdblockURLLoaderFactory() = default;

GURL AdblockURLLoaderFactory::PageUrl() const {
  return PageUrlForFrame(frame_id_, request_initiator_);
}

void AdblockURLLoaderFactory::CreateLoaderAndStart(
    mojo::PendingReceiver<network::mojom::URLLoader> loader,
    int32_t request_id,
    uint32_t options,
    const network::ResourceRequest& request,
    mojo::PendingRemote<network::mojom::URLLoaderClient> client,
    const net::MutableNetworkTrafficAnnotationTag& traffic_annotation) {
  RebaAdblockService* service = RebaAdblockService::Get();
  if (!service || !service->IsEnabled() || !IsFilterable(request.url)) {
    target_factory_->CreateLoaderAndStart(std::move(loader), request_id,
                                          options, request, std::move(client),
                                          traffic_annotation);
    return;
  }

  const RequestType type = TypeFor(request);
  const GURL page_url = PageUrl();
  if (!page_url.SchemeIsHTTPOrHTTPS()) {
    // Pages of the browser and of extensions are not filtered.
    target_factory_->CreateLoaderAndStart(std::move(loader), request_id,
                                          options, request, std::move(client),
                                          traffic_annotation);
    return;
  }
  auto* proxied =
      new ProxiedRequest(frame_id_, incognito_, page_url, type, request.url,
                         std::move(loader), std::move(client));

  const Decision decision =
      service->CheckRequest(request.url, page_url, type, request.method);
  if (!decision.redirect_url.empty()) {
    proxied->Serve(decision);
    return;
  }
  if (decision.block) {
    proxied->Block(decision);
    return;
  }
  proxied->Forward(target_factory_.get(), request_id, options, request,
                   traffic_annotation);
}

void AdblockURLLoaderFactory::Clone(
    mojo::PendingReceiver<network::mojom::URLLoaderFactory> receiver) {
  mojo::PendingRemote<network::mojom::URLLoaderFactory> target;
  target_factory_->Clone(target.InitWithNewPipeAndPassReceiver());
  mojo::MakeSelfOwnedReceiver(
      base::WrapUnique(new AdblockURLLoaderFactory(
          frame_id_, incognito_, request_initiator_, std::move(target))),
      std::move(receiver));
}

}  // namespace reba_adblock
