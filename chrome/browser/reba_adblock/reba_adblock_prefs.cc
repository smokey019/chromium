// Copyright 2026 The REBA Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/reba_adblock/reba_adblock_prefs.h"

#include "base/values.h"
#include "components/reba_adblock/core/filter_list_catalog.h"
#include "components/prefs/pref_registry_simple.h"

namespace reba_adblock::prefs {

void RegisterLocalStatePrefs(PrefRegistrySimple* registry) {
  registry->RegisterBooleanPref(kEnabled, true);
  registry->RegisterBooleanPref(kCosmeticFilteringEnabled, true);

  base::ListValue default_lists;
  for (const std::string& id : DefaultEnabledListIds()) {
    default_lists.Append(id);
  }
  registry->RegisterListPref(kEnabledLists, std::move(default_lists));
  registry->RegisterListPref(kCustomLists);
  registry->RegisterStringPref(kUserFilters, std::string());
  registry->RegisterListPref(kTrustedSites);
  registry->RegisterBooleanPref(kShowBlockedCount, true);
  registry->RegisterBooleanPref(kContextMenuEnabled, true);
  registry->RegisterBooleanPref(kPopupGuard, true);
  registry->RegisterBooleanPref(kAutoUpdate, true);
  registry->RegisterDictionaryPref(kListStates);
  registry->RegisterIntegerPref(kTotalBlocked, 0);
}

}  // namespace reba_adblock::prefs
