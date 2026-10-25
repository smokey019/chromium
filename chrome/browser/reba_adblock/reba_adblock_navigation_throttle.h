// Copyright 2026 The REBA Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_REBA_ADBLOCK_REBA_ADBLOCK_NAVIGATION_THROTTLE_H_
#define CHROME_BROWSER_REBA_ADBLOCK_REBA_ADBLOCK_NAVIGATION_THROTTLE_H_

#include "base/scoped_observation.h"
#include "base/timer/timer.h"
#include "chrome/browser/reba_adblock/reba_adblock_service.h"
#include "components/reba_adblock/core/engine_set.h"
#include "content/public/browser/navigation_throttle.h"
#include "url/gurl.h"

namespace content {
class NavigationThrottleRegistry;
}

namespace reba_adblock {

// Applies the filters to navigations: a top level page that is blocked is
// replaced by the "this website is blocked" page, and a blocked frame ends up
// empty. `$removeparam` filters are applied by RebaAdblockURLLoaderThrottle.
//
// At startup, top level pages wait (a few seconds at most) for the first
// engine, so that restored tabs are filtered too.
class RebaAdblockNavigationThrottle : public content::NavigationThrottle,
                                      public RebaAdblockServiceObserver {
 public:
  static void MaybeCreateAndAdd(content::NavigationThrottleRegistry& registry);

  // The address of the page that says `blocked_url` is blocked.
  static GURL BlockedPageUrl(const GURL& blocked_url, const Decision& decision);

  ~RebaAdblockNavigationThrottle() override;

  // content::NavigationThrottle:
  ThrottleCheckResult WillStartRequest() override;
  ThrottleCheckResult WillRedirectRequest() override;
  const char* GetNameForLogging() override;

  // RebaAdblockServiceObserver:
  void OnEnginesChanged() override;
  void OnAdblockServiceShutdown() override;

 private:
  explicit RebaAdblockNavigationThrottle(
      content::NavigationThrottleRegistry& registry);

  ThrottleCheckResult Check();
  // Stops waiting for the first engine and carries on with the navigation.
  void StopWaiting();

  // Whether the navigation is deferred for the first engine. A flag rather than
  // wait_timer_.IsRunning(), which is already false when the timer fires.
  bool waiting_ = false;
  base::OneShotTimer wait_timer_;
  base::ScopedObservation<RebaAdblockService, RebaAdblockServiceObserver>
      service_observation_{this};
};

}  // namespace reba_adblock

#endif  // CHROME_BROWSER_REBA_ADBLOCK_REBA_ADBLOCK_NAVIGATION_THROTTLE_H_
