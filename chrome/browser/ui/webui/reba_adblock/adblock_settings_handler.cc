// Copyright 2026 The REBA Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/webui/reba_adblock/adblock_settings_handler.h"

#include <optional>
#include <string>
#include <utility>

#include "base/functional/bind.h"
#include "base/time/time.h"
#include "chrome/browser/ui/webui/reba_adblock/adblock_ui_utils.h"
#include "url/gurl.h"

namespace reba_adblock {

namespace {

constexpr base::TimeDelta kStateNotificationDelay = base::Milliseconds(150);

RebaAdblockService* Service() {
  return RebaAdblockService::Get();
}

}  // namespace

AdblockSettingsHandler::AdblockSettingsHandler() = default;
AdblockSettingsHandler::~AdblockSettingsHandler() = default;

void AdblockSettingsHandler::RegisterMessages() {
  struct Message {
    const char* name;
    void (AdblockSettingsHandler::*method)(const base::ListValue&);
  };
  static const Message kMessages[] = {
      {"rebaAdblockGetState", &AdblockSettingsHandler::HandleGetState},
      {"rebaAdblockSetBool", &AdblockSettingsHandler::HandleSetBool},
      {"rebaAdblockSetListEnabled",
       &AdblockSettingsHandler::HandleSetListEnabled},
      {"rebaAdblockAddList", &AdblockSettingsHandler::HandleAddList},
      {"rebaAdblockRemoveList", &AdblockSettingsHandler::HandleRemoveList},
      {"rebaAdblockUpdateLists", &AdblockSettingsHandler::HandleUpdateLists},
      {"rebaAdblockResetLists", &AdblockSettingsHandler::HandleResetLists},
      {"rebaAdblockSetUserFilters",
       &AdblockSettingsHandler::HandleSetUserFilters},
      {"rebaAdblockAddTrustedSite",
       &AdblockSettingsHandler::HandleAddTrustedSite},
      {"rebaAdblockRemoveTrustedSite",
       &AdblockSettingsHandler::HandleRemoveTrustedSite},
      {"rebaAdblockGetLog", &AdblockSettingsHandler::HandleGetLog},
      {"rebaAdblockClearLog", &AdblockSettingsHandler::HandleClearLog},
      {"rebaAdblockLogAction", &AdblockSettingsHandler::HandleLogAction},
      {"rebaAdblockAddExceptionSite",
       &AdblockSettingsHandler::HandleAddExceptionSite},
      {"rebaAdblockRemoveExceptionSite",
       &AdblockSettingsHandler::HandleRemoveExceptionSite},
  };
  for (const Message& message : kMessages) {
    web_ui()->RegisterMessageCallback(
        message.name,
        base::BindRepeating(message.method, base::Unretained(this)));
  }
}

void AdblockSettingsHandler::OnJavascriptAllowed() {
  if (RebaAdblockService* service = Service()) {
    observation_.Observe(service);
  }
}

void AdblockSettingsHandler::OnJavascriptDisallowed() {
  observation_.Reset();
  state_timer_.Stop();
}

void AdblockSettingsHandler::OnEnginesChanged() {
  NotifyStateChangedSoon();
}

void AdblockSettingsHandler::OnListStatesChanged() {
  NotifyStateChangedSoon();
}

void AdblockSettingsHandler::OnSettingsChanged() {
  NotifyStateChangedSoon();
}

void AdblockSettingsHandler::OnBlockedRequestsLogged() {
  if (RebaAdblockService* service = Service()) {
    FireWebUIListener(
        "reba-adblock-log-changed",
        base::Value(static_cast<double>(service->total_blocked())));
  }
}

void AdblockSettingsHandler::OnAdblockServiceShutdown() {
  observation_.Reset();
  state_timer_.Stop();
}

void AdblockSettingsHandler::NotifyStateChangedSoon() {
  if (!state_timer_.IsRunning()) {
    state_timer_.Start(
        FROM_HERE, kStateNotificationDelay,
        base::BindOnce(&AdblockSettingsHandler::NotifyStateChanged,
                       base::Unretained(this)));
  }
}

void AdblockSettingsHandler::NotifyStateChanged() {
  if (!IsJavascriptAllowed()) {
    return;
  }
  if (RebaAdblockService* service = Service()) {
    FireWebUIListener("reba-adblock-state-changed",
                      BuildSettingsState(*service));
  }
}

void AdblockSettingsHandler::HandleGetState(const base::ListValue& args) {
  AllowJavascript();
  CHECK_GE(args.size(), 1u);
  const std::string& callback_id = args[0].GetString();
  RebaAdblockService* service = Service();
  if (!service) {
    RejectJavascriptCallback(args[0], base::Value());
    return;
  }
  ResolveJavascriptCallback(base::Value(callback_id),
                            BuildSettingsState(*service));
}

void AdblockSettingsHandler::HandleSetBool(const base::ListValue& args) {
  RebaAdblockService* service = Service();
  if (!service || args.size() < 2 || !args[0].is_string() ||
      !args[1].is_bool()) {
    return;
  }
  const std::string& name = args[0].GetString();
  const bool value = args[1].GetBool();
  if (name == "enabled") {
    service->SetEnabled(value);
  } else if (name == "cosmetic") {
    service->SetCosmeticFilteringEnabled(value);
  } else if (name == "autoUpdate") {
    service->SetAutoUpdate(value);
  } else if (name == "showBlockedCount") {
    service->SetShowBlockedCount(value);
  } else if (name == "contextMenu") {
    service->SetContextMenuEnabled(value);
  } else if (name == "popupGuard") {
    service->SetPopupGuard(value);
  } else if (name == "downloadGuard") {
    service->SetDownloadGuard(value);
  } else if (name == "antivirus") {
    service->SetAntivirusCheck(value);
  }
}

void AdblockSettingsHandler::HandleSetListEnabled(const base::ListValue& args) {
  RebaAdblockService* service = Service();
  if (!service || args.size() < 2 || !args[0].is_string() ||
      !args[1].is_bool()) {
    return;
  }
  service->SetListEnabled(args[0].GetString(), args[1].GetBool());
}

void AdblockSettingsHandler::HandleAddList(const base::ListValue& args) {
  AllowJavascript();
  CHECK_GE(args.size(), 3u);
  RebaAdblockService* service = Service();
  base::DictValue result;
  if (!service || !args[1].is_string() || !args[2].is_string()) {
    result.Set("ok", false);
    result.Set("error", "Can't add the list.");
  } else {
    const GURL url(args[1].GetString());
    if (!url.is_valid() || !url.SchemeIs("https")) {
      result.Set("ok", false);
      result.Set("error", "Enter the https:// address of a filter list.");
    } else if (!service->AddCustomList(url, args[2].GetString())) {
      result.Set("ok", false);
      result.Set("error", "That list is already there.");
    } else {
      result.Set("ok", true);
    }
  }
  ResolveJavascriptCallback(args[0], std::move(result));
}

void AdblockSettingsHandler::HandleRemoveList(const base::ListValue& args) {
  if (RebaAdblockService* service = Service();
      service && args.size() >= 1 && args[0].is_string()) {
    service->RemoveCustomList(args[0].GetString());
  }
}

void AdblockSettingsHandler::HandleUpdateLists(const base::ListValue& args) {
  if (RebaAdblockService* service = Service()) {
    service->UpdateListsNow();
  }
}

void AdblockSettingsHandler::HandleResetLists(const base::ListValue& args) {
  if (RebaAdblockService* service = Service()) {
    service->ResetListsToDefault();
  }
}

// args: callback id, text. Answers false if the text was not taken (it is
// larger than kMaxUserFiltersBytes).
void AdblockSettingsHandler::HandleSetUserFilters(const base::ListValue& args) {
  AllowJavascript();
  CHECK_GE(args.size(), 2u);
  RebaAdblockService* service = Service();
  const bool ok = service && args[1].is_string() &&
                  service->SetUserFilters(args[1].GetString());
  ResolveJavascriptCallback(args[0], base::Value(ok));
}

void AdblockSettingsHandler::HandleAddTrustedSite(const base::ListValue& args) {
  AllowJavascript();
  CHECK_GE(args.size(), 2u);
  RebaAdblockService* service = Service();
  // The list on this page is the saved one, whichever window it is open in.
  const bool ok = service && args[1].is_string() &&
                  service->AddTrustedSite(args[1].GetString(),
                                          /*persist=*/true);
  ResolveJavascriptCallback(args[0], base::Value(ok));
}

void AdblockSettingsHandler::HandleRemoveTrustedSite(
    const base::ListValue& args) {
  if (RebaAdblockService* service = Service();
      service && args.size() >= 1 && args[0].is_string()) {
    service->RemoveTrustedSite(args[0].GetString());
  }
}

void AdblockSettingsHandler::HandleGetLog(const base::ListValue& args) {
  AllowJavascript();
  CHECK_GE(args.size(), 1u);
  RebaAdblockService* service = Service();
  // The protection log and the blocked requests, in one list that the page
  // sorts.
  base::ListValue log;
  if (service) {
    log = SecurityEventsToList(service->GetSecurityEvents());
    for (base::Value& request :
         BlockedRequestsToList(service->GetBlockedRequests())) {
      log.Append(std::move(request));
    }
  }
  ResolveJavascriptCallback(args[0], std::move(log));
}

void AdblockSettingsHandler::HandleClearLog(const base::ListValue& args) {
  if (RebaAdblockService* service = Service()) {
    service->ClearBlockedRequests();
  }
}

// args: action, url, page host.
void AdblockSettingsHandler::HandleLogAction(const base::ListValue& args) {
  RebaAdblockService* service = Service();
  if (!service || args.size() < 3 || !args[0].is_string() ||
      !args[1].is_string() || !args[2].is_string()) {
    return;
  }
  const std::string& action = args[0].GetString();
  const GURL url(args[1].GetString());
  const std::string& page_host = args[2].GetString();
  // These are about the site, whatever the address was.
  if (action == "allowPopupsOnSite") {
    service->AddExceptionSite(RebaAdblockService::Exception::kPopupGuard,
                              page_host);
    return;
  }
  if (action == "stopHoldingOnSite") {
    service->AddExceptionSite(RebaAdblockService::Exception::kDownloadGuard,
                              page_host);
    return;
  }
  if (!url.is_valid()) {
    return;
  }
  if (action == "allowAlways") {
    service->AllowRequestAlways(url, std::string());
  } else if (action == "allowOnSite") {
    service->AllowRequestAlways(url, page_host);
  } else if (action == "allowSession") {
    service->AllowRequestForSession(url, std::string());
  } else if (action == "blockAlways") {
    service->BlockRequestAlways(url, std::string());
  }
}

namespace {

std::optional<RebaAdblockService::Exception> ExceptionFromName(
    const std::string& name) {
  if (name == "popup") {
    return RebaAdblockService::Exception::kPopupGuard;
  }
  if (name == "download") {
    return RebaAdblockService::Exception::kDownloadGuard;
  }
  return std::nullopt;
}

}  // namespace

// args: callback id, "popup" or "download", site. Answers whether the site was
// taken.
void AdblockSettingsHandler::HandleAddExceptionSite(
    const base::ListValue& args) {
  AllowJavascript();
  CHECK_GE(args.size(), 3u);
  RebaAdblockService* service = Service();
  std::optional<RebaAdblockService::Exception> which =
      args[1].is_string() ? ExceptionFromName(args[1].GetString())
                          : std::nullopt;
  const bool ok = service && which && args[2].is_string() &&
                  service->AddExceptionSite(*which, args[2].GetString());
  ResolveJavascriptCallback(args[0], base::Value(ok));
}

// args: "popup" or "download", site.
void AdblockSettingsHandler::HandleRemoveExceptionSite(
    const base::ListValue& args) {
  RebaAdblockService* service = Service();
  if (!service || args.size() < 2 || !args[0].is_string() ||
      !args[1].is_string()) {
    return;
  }
  if (std::optional<RebaAdblockService::Exception> which =
          ExceptionFromName(args[0].GetString())) {
    service->RemoveExceptionSite(*which, args[1].GetString());
  }
}

}  // namespace reba_adblock
