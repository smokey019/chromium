// Copyright 2026 The REBA Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_REBA_ADBLOCK_REBA_ADBLOCK_PREFS_H_
#define CHROME_BROWSER_REBA_ADBLOCK_REBA_ADBLOCK_PREFS_H_

class PrefRegistrySimple;

namespace reba_adblock::prefs {

// All of these are browser-wide (Local State), not per profile: the ad blocker
// has one set of compiled lists for the whole browser.

// bool. The ad blocker as a whole.
inline constexpr char kEnabled[] = "reba_adblock.enabled";
// bool. Hide page elements and run scriptlets. Network blocking is not
// affected.
inline constexpr char kCosmeticFilteringEnabled[] =
    "reba_adblock.cosmetic_filtering_enabled";
// list of strings. Ids of the enabled filter lists.
inline constexpr char kEnabledLists[] = "reba_adblock.enabled_lists";
// list of dicts {"url": string, "title": string}. Lists added by URL.
inline constexpr char kCustomLists[] = "reba_adblock.custom_lists";
// string. The user's own filters ("My filters").
inline constexpr char kUserFilters[] = "reba_adblock.user_filters";
// list of strings. Hosts of the sites where nothing is blocked (and their
// subdomains).
inline constexpr char kTrustedSites[] = "reba_adblock.trusted_sites";
// bool. Show the number of blocked requests on the toolbar button.
inline constexpr char kShowBlockedCount[] = "reba_adblock.show_blocked_count";
// bool. Offer "Block element..." and "Disable the ad blocker on this site" in
// the page context menu.
inline constexpr char kContextMenuEnabled[] =
    "reba_adblock.context_menu_enabled";
// bool. Refuse pop-ups that a page opens when you click somewhere that is not
// a link or control for them ("click hijacking"), even if no filter list knows
// the address.
inline constexpr char kPopupGuard[] = "reba_adblock.popup_guard";
// bool. Download new versions of the filter lists on their own.
inline constexpr char kAutoUpdate[] = "reba_adblock.auto_update";
// dict: list id -> dict with "updated" (double, seconds since the epoch),
// "size" (int), "rules" (int), "version" (string), "expires" (double, seconds),
// "error" (string). What is known about the downloaded copy of each list.
inline constexpr char kListStates[] = "reba_adblock.list_states";
// int. Requests blocked in total, over all browser runs.
inline constexpr char kTotalBlocked[] = "reba_adblock.total_blocked";

void RegisterLocalStatePrefs(PrefRegistrySimple* registry);

}  // namespace reba_adblock::prefs

#endif  // CHROME_BROWSER_REBA_ADBLOCK_REBA_ADBLOCK_PREFS_H_
