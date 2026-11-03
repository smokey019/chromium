// Copyright 2012 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_UI_BROWSER_UI_PREFS_H_
#define CHROME_BROWSER_UI_BROWSER_UI_PREFS_H_

class PrefRegistrySimple;

namespace user_prefs {
class PrefRegistrySyncable;
}

namespace prefs {

// REBA: A boolean pref set to true if the "Install app" icon should show in the
// address bar on sites that can be installed as an app. Off by default. Kept
// here instead of chrome/common/pref_names.h, which over a thousand files
// include.
inline constexpr char kShowInstallAppButton[] =
    "browser.show_install_app_button";

}  // namespace prefs

void RegisterBrowserPrefs(PrefRegistrySimple* registry);
void RegisterBrowserUserPrefs(user_prefs::PrefRegistrySyncable* registry);

#endif  // CHROME_BROWSER_UI_BROWSER_UI_PREFS_H_
