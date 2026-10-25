// Copyright 2026 The REBA Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/webui/reba_adblock/adblock_ui_utils.h"

namespace reba_adblock {

base::DictValue ListViewToDict(const ListView& view) {
  base::DictValue dict;
  dict.Set("id", view.list.id);
  dict.Set("title", view.list.title);
  dict.Set("group", view.list.group);
  dict.Set("group2", view.list.group2);
  dict.Set("parent", view.list.parent);
  dict.Set("supportUrl", view.list.support_url);
  dict.Set("lang", view.list.lang);
  dict.Set("custom", view.list.is_custom);
  dict.Set("url", view.list.urls.empty() ? std::string() : view.list.urls[0]);
  dict.Set("enabled", view.enabled);
  dict.Set("defaultEnabled", view.list.default_enabled);
  // Milliseconds since the epoch for JavaScript's Date; 0 if never fetched.
  dict.Set("updated", view.state.updated.InMillisecondsFSinceUnixEpoch());
  dict.Set("size", static_cast<double>(view.state.size));
  dict.Set("rules", view.state.rules);
  dict.Set("version", view.state.version);
  dict.Set("error", view.state.error);
  dict.Set("updating", view.state.updating);
  return dict;
}

base::ListValue BlockedRequestsToList(
    const std::vector<BlockedRequest>& requests) {
  base::ListValue list;
  for (const BlockedRequest& request : requests) {
    base::DictValue dict;
    dict.Set("url", request.url);
    dict.Set("type", request.type);
    dict.Set("pageHost", request.page_host);
    dict.Set("filter", request.filter);
    dict.Set("list", request.list_title);
    dict.Set("count", request.count);
    dict.Set("time", request.last_blocked.InMillisecondsFSinceUnixEpoch());
    list.Append(std::move(dict));
  }
  return list;
}

base::DictValue BuildSettingsState(RebaAdblockService& service) {
  base::DictValue state;
  state.Set("enabled", service.IsEnabled());
  state.Set("cosmetic", service.CosmeticFilteringSetting());
  state.Set("autoUpdate", service.AutoUpdate());
  state.Set("showBlockedCount", service.ShowBlockedCount());
  state.Set("contextMenu", service.ContextMenuEnabled());
  state.Set("popupGuard", service.PopupGuardEnabled());
  state.Set("totalBlocked", static_cast<double>(service.total_blocked()));
  state.Set("updating", service.IsUpdatingLists());
  state.Set("ready", !!service.engines());
  state.Set("building", service.IsBuilding());
  base::ListValue lists;
  for (const ListView& view : service.GetLists()) {
    lists.Append(ListViewToDict(view));
  }
  state.Set("lists", std::move(lists));
  state.Set("userFilters", service.GetUserFilters());
  base::ListValue sites;
  for (const std::string& site : service.GetTrustedSites()) {
    sites.Append(site);
  }
  state.Set("trustedSites", std::move(sites));
  return state;
}

}  // namespace reba_adblock
