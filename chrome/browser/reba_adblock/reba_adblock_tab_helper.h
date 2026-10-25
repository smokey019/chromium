// Copyright 2026 The REBA Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_REBA_ADBLOCK_REBA_ADBLOCK_TAB_HELPER_H_
#define CHROME_BROWSER_REBA_ADBLOCK_REBA_ADBLOCK_TAB_HELPER_H_

#include <string>
#include <vector>

#include "base/observer_list.h"
#include "base/observer_list_types.h"
#include "base/time/time.h"
#include "components/reba_adblock/core/engine_set.h"
#include "components/reba_adblock/engine/adblock_engine.h"
#include "content/public/browser/web_contents_observer.h"
#include "content/public/browser/web_contents_user_data.h"
#include "url/gurl.h"

namespace reba_adblock {

// Keeps track of what the ad blocker did on the page shown in a tab, for the
// toolbar button and its popup.
class RebaAdblockTabHelper
    : public content::WebContentsObserver,
      public content::WebContentsUserData<RebaAdblockTabHelper> {
 public:
  class Observer : public base::CheckedObserver {
   public:
    virtual void OnBlockedCountChanged(RebaAdblockTabHelper* helper) = 0;
    // The tab is going away. Observers must stop observing `helper` here: it
    // dies with its tab, which is destroyed before the window's views when a
    // window closes.
    virtual void OnTabHelperDestroyed(RebaAdblockTabHelper* helper) = 0;
  };

  struct BlockedItem {
    std::string url;
    std::string type;
    std::string filter;
    std::string list_title;
  };

  ~RebaAdblockTabHelper() override;

  // A request made by the page was blocked.
  void OnRequestBlocked(const GURL& url,
                        RequestType type,
                        const Decision& decision);

  // Requests blocked since the page started loading.
  int blocked_count() const { return blocked_count_; }
  // The most recent ones.
  const std::vector<BlockedItem>& blocked_items() const {
    return blocked_items_;
  }
  // Elements the page's cosmetic filters hid, which the page can't count by
  // itself.
  int hidden_element_count() const { return hidden_element_count_; }
  void AddHiddenElements(int count);

  // Whether the element picker was started in this tab (in the last few
  // minutes) and hasn't added its filter yet. Only then may the page ask to
  // add a filter.
  bool picker_active() const;
  // The picker added its filter; it may not add another.
  void EndPicker() { picker_started_ = base::TimeTicks(); }

  // Starts the element picker (or, with `zapper`, the zapper) on the page.
  void StartPicker(bool zapper);

  // Turns the ad blocker off for the site of the page, or back on, and
  // reloads the page. From an off-the-record window, the site is trusted only
  // until the browser closes.
  void SetSiteTrusted(bool trusted);

  void AddObserver(Observer* observer) { observers_.AddObserver(observer); }
  void RemoveObserver(Observer* observer) {
    observers_.RemoveObserver(observer);
  }

  // content::WebContentsObserver:
  void DidFinishNavigation(content::NavigationHandle* handle) override;

 private:
  friend class content::WebContentsUserData<RebaAdblockTabHelper>;
  explicit RebaAdblockTabHelper(content::WebContents* web_contents);

  void NotifyObservers();

  int blocked_count_ = 0;
  int hidden_element_count_ = 0;
  // When the element picker was started; null when it isn't running.
  base::TimeTicks picker_started_;
  std::vector<BlockedItem> blocked_items_;
  base::ObserverList<Observer> observers_;

  WEB_CONTENTS_USER_DATA_KEY_DECL();
};

}  // namespace reba_adblock

#endif  // CHROME_BROWSER_REBA_ADBLOCK_REBA_ADBLOCK_TAB_HELPER_H_
