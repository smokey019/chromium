// Copyright 2026 The REBA Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/webui/reba_adblock/adblock_popup_handler.h"

#include <string>
#include <utility>

#include "base/functional/bind.h"
#include "base/time/time.h"
#include "chrome/browser/ui/webui/reba_adblock/adblock_popup_context.h"
#include "components/reba_adblock/common/reba_adblock.mojom.h"
#include "content/public/browser/navigation_controller.h"
#include "content/public/browser/page_navigator.h"
#include "content/public/browser/render_frame_host.h"
#include "content/public/browser/web_contents.h"
#include "mojo/public/cpp/bindings/associated_remote.h"
#include "third_party/blink/public/common/associated_interfaces/associated_interface_provider.h"
#include "ui/base/page_transition_types.h"
#include "ui/base/window_open_disposition.h"
#include "url/gurl.h"

namespace reba_adblock {

namespace {

constexpr base::TimeDelta kNotifyDelay = base::Milliseconds(100);

}  // namespace

AdblockPopupHandler::AdblockPopupHandler() = default;
AdblockPopupHandler::~AdblockPopupHandler() = default;

void AdblockPopupHandler::RegisterMessages() {
  struct Message {
    const char* name;
    void (AdblockPopupHandler::*method)(const base::ListValue&);
  };
  static const Message kMessages[] = {
      {"rebaAdblockPopupGetState", &AdblockPopupHandler::HandleGetState},
      {"rebaAdblockPopupSetSiteTrusted",
       &AdblockPopupHandler::HandleSetSiteTrusted},
      {"rebaAdblockPopupSetEnabled", &AdblockPopupHandler::HandleSetEnabled},
      {"rebaAdblockPopupReload", &AdblockPopupHandler::HandleReload},
      {"rebaAdblockPopupPick", &AdblockPopupHandler::HandlePick},
      {"rebaAdblockPopupOpenSettings",
       &AdblockPopupHandler::HandleOpenSettings},
  };
  for (const Message& message : kMessages) {
    web_ui()->RegisterMessageCallback(
        message.name,
        base::BindRepeating(message.method, base::Unretained(this)));
  }
}

void AdblockPopupHandler::OnJavascriptAllowed() {
  if (RebaAdblockService* service = RebaAdblockService::Get()) {
    service_observation_.Observe(service);
  }
  if (content::WebContents* source = SourceContents()) {
    if (RebaAdblockTabHelper* helper =
            RebaAdblockTabHelper::FromWebContents(source)) {
      tab_observation_.Observe(helper);
    }
  }
}

void AdblockPopupHandler::OnJavascriptDisallowed() {
  service_observation_.Reset();
  tab_observation_.Reset();
  notify_timer_.Stop();
}

content::WebContents* AdblockPopupHandler::SourceContents() {
  AdblockPopupContext* context =
      AdblockPopupContext::FromWebContents(web_ui()->GetWebContents());
  return context ? context->source() : nullptr;
}

base::DictValue AdblockPopupHandler::BuildState() {
  base::DictValue state;
  RebaAdblockService* service = RebaAdblockService::Get();
  content::WebContents* source = SourceContents();
  GURL url = source ? source->GetLastCommittedURL() : GURL();
  const bool filterable = url.SchemeIsHTTPOrHTTPS();

  state.Set("host", filterable ? std::string(url.host()) : std::string());
  state.Set("enabled", service && service->IsEnabled());
  state.Set("siteTrusted",
            service && filterable && service->IsSiteTrusted(url));
  state.Set("canReload", !!source);
  state.Set("totalBlocked",
            service ? static_cast<double>(service->total_blocked()) : 0.0);

  int blocked = 0;
  int hidden = 0;
  base::ListValue items;
  if (source) {
    if (RebaAdblockTabHelper* helper =
            RebaAdblockTabHelper::FromWebContents(source)) {
      blocked = helper->blocked_count();
      hidden = helper->hidden_element_count();
      for (const RebaAdblockTabHelper::BlockedItem& item :
           helper->blocked_items()) {
        base::DictValue dict;
        dict.Set("url", item.url);
        dict.Set("type", item.type);
        dict.Set("filter", item.filter);
        dict.Set("list", item.list_title);
        items.Append(std::move(dict));
      }
    }
  }
  state.Set("blockedCount", blocked);
  state.Set("hiddenCount", hidden);
  state.Set("items", std::move(items));
  return state;
}

void AdblockPopupHandler::NotifySoon() {
  if (!notify_timer_.IsRunning()) {
    notify_timer_.Start(FROM_HERE, kNotifyDelay,
                        base::BindOnce(&AdblockPopupHandler::Notify,
                                       base::Unretained(this)));
  }
}

void AdblockPopupHandler::Notify() {
  if (IsJavascriptAllowed()) {
    FireWebUIListener("reba-adblock-popup-state", BuildState());
  }
}

void AdblockPopupHandler::OnSettingsChanged() {
  NotifySoon();
}

void AdblockPopupHandler::OnBlockedRequestsLogged() {
  NotifySoon();
}

void AdblockPopupHandler::OnAdblockServiceShutdown() {
  service_observation_.Reset();
  notify_timer_.Stop();
}

void AdblockPopupHandler::OnBlockedCountChanged(RebaAdblockTabHelper* helper) {
  NotifySoon();
}

void AdblockPopupHandler::OnTabHelperDestroyed(RebaAdblockTabHelper* helper) {
  // The source tab closed while the pop-up was open.
  tab_observation_.Reset();
}

void AdblockPopupHandler::HandleGetState(const base::ListValue& args) {
  AllowJavascript();
  CHECK_GE(args.size(), 1u);
  ResolveJavascriptCallback(args[0], BuildState());
}

void AdblockPopupHandler::HandleSetSiteTrusted(const base::ListValue& args) {
  content::WebContents* source = SourceContents();
  if (!source || args.size() < 1 || !args[0].is_bool()) {
    return;
  }
  if (RebaAdblockTabHelper* helper =
          RebaAdblockTabHelper::FromWebContents(source)) {
    helper->SetSiteTrusted(args[0].GetBool());
  }
}

void AdblockPopupHandler::HandleSetEnabled(const base::ListValue& args) {
  RebaAdblockService* service = RebaAdblockService::Get();
  if (service && args.size() >= 1 && args[0].is_bool()) {
    service->SetEnabled(args[0].GetBool());
  }
}

void AdblockPopupHandler::HandleReload(const base::ListValue& args) {
  if (content::WebContents* source = SourceContents()) {
    source->GetController().Reload(content::ReloadType::NORMAL, false);
  }
  if (AdblockPopupContext* context =
          AdblockPopupContext::FromWebContents(web_ui()->GetWebContents())) {
    context->Close();
  }
}

void AdblockPopupHandler::HandlePick(const base::ListValue& args) {
  content::WebContents* source = SourceContents();
  if (!source || args.size() < 1 || !args[0].is_bool()) {
    return;
  }
  if (RebaAdblockTabHelper* helper =
          RebaAdblockTabHelper::FromWebContents(source)) {
    helper->StartPicker(args[0].GetBool());
  }
  if (AdblockPopupContext* context =
          AdblockPopupContext::FromWebContents(web_ui()->GetWebContents())) {
    context->Close();
  }
}

void AdblockPopupHandler::HandleOpenSettings(const base::ListValue& args) {
  content::WebContents* source = SourceContents();
  if (!source) {
    return;
  }
  std::string url = "chrome://settings/adblock";
  if (args.size() >= 1 && args[0].is_string() &&
      args[0].GetString() == "log") {
    url += "?tab=log";
  }
  source->OpenURL(
      content::OpenURLParams(GURL(url), content::Referrer(),
                             WindowOpenDisposition::kNewForegroundTab,
                             ui::PAGE_TRANSITION_LINK,
                             /*is_renderer_initiated=*/false),
      /*navigation_handle_callback=*/{});
  if (AdblockPopupContext* context =
          AdblockPopupContext::FromWebContents(web_ui()->GetWebContents())) {
    context->Close();
  }
}

}  // namespace reba_adblock
