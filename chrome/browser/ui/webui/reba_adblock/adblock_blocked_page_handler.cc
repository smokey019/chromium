// Copyright 2026 The REBA Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/webui/reba_adblock/adblock_blocked_page_handler.h"

#include <utility>

#include "base/functional/bind.h"
#include "base/time/time.h"
#include "chrome/browser/profiles/profile.h"
#include "components/reba_adblock/engine/adblock_engine.h"
#include "content/public/browser/web_ui.h"

namespace reba_adblock {

namespace {
// The page goes on to the website even if the new filters take longer than
// this to compile.
constexpr base::TimeDelta kMaxWait = base::Seconds(5);
}  // namespace

AdblockBlockedPageHandler::AdblockBlockedPageHandler() = default;
AdblockBlockedPageHandler::~AdblockBlockedPageHandler() = default;

void AdblockBlockedPageHandler::RegisterMessages() {
  web_ui()->RegisterMessageCallback(
      "rebaAdblockBlockedPageAction",
      base::BindRepeating(&AdblockBlockedPageHandler::HandleAction,
                          base::Unretained(this)));
}

void AdblockBlockedPageHandler::OnJavascriptDisallowed() {
  observation_.Reset();
  timeout_.Stop();
  pending_.clear();
}

// args: callback id, action, URL of the blocked page.
void AdblockBlockedPageHandler::HandleAction(const base::ListValue& args) {
  AllowJavascript();
  CHECK_GE(args.size(), 3u);
  RebaAdblockService* service = RebaAdblockService::Get();
  if (!service || !args[1].is_string() || !args[2].is_string()) {
    RejectJavascriptCallback(args[0], base::Value());
    return;
  }
  const std::string& action = args[1].GetString();
  const GURL url(args[2].GetString());
  if (!url.is_valid() || !url.SchemeIsHTTPOrHTTPS()) {
    RejectJavascriptCallback(args[0], base::Value());
    return;
  }
  const std::string host(url.host());
  // Nothing done in Incognito and Guest windows is saved: there the choices
  // last until the browser closes. The page offers only those, but the
  // handler doesn't rely on it.
  const bool persist = !Profile::FromWebUI(web_ui())->IsOffTheRecord();

  bool wait_until_allowed = true;
  if (action == "allowSession" || (action == "allowAlways" && !persist)) {
    service->AllowDocumentForSession(host);
  } else if (action == "allowAlways") {
    service->AllowDocumentAlways(host);
  } else if (action == "blockAlways") {
    if (persist) {
      service->AlwaysBlockDocument(host);
    } else {
      service->BlockDocumentForSession(host);
    }
    wait_until_allowed = false;
  } else {
    RejectJavascriptCallback(args[0], base::Value());
    return;
  }

  if (!observation_.IsObserving()) {
    observation_.Observe(service);
  }
  pending_.push_back({args[0].Clone(), url, wait_until_allowed});
  timeout_.Start(FROM_HERE, kMaxWait,
                 base::BindOnce(&AdblockBlockedPageHandler::ResolvePending,
                                base::Unretained(this), /*everything=*/true));
  // Maybe already in effect (the filters of the lists weren't touched).
  OnEnginesChanged();
}

void AdblockBlockedPageHandler::OnEnginesChanged() {
  ResolvePending(/*everything=*/false);
}

void AdblockBlockedPageHandler::OnAdblockServiceShutdown() {
  // The pending answers go out when `timeout_` fires.
  observation_.Reset();
}

void AdblockBlockedPageHandler::ResolvePending(bool everything) {
  RebaAdblockService* service = RebaAdblockService::Get();
  std::vector<Pending> still_waiting;
  for (Pending& pending : pending_) {
    bool done = everything || !pending.wait_until_allowed || !service;
    if (!done) {
      // The page goes on with location.replace(), a GET.
      done = !service
                  ->CheckRequest(pending.url, pending.url,
                                 RequestType::kMainFrame, "GET")
                  .block;
    }
    if (done) {
      ResolveJavascriptCallback(pending.callback_id, base::Value(true));
    } else {
      still_waiting.push_back(std::move(pending));
    }
  }
  pending_ = std::move(still_waiting);
  if (pending_.empty()) {
    timeout_.Stop();
  }
}

}  // namespace reba_adblock
