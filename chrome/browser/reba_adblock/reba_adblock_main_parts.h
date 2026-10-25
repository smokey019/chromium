// Copyright 2026 The REBA Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_REBA_ADBLOCK_REBA_ADBLOCK_MAIN_PARTS_H_
#define CHROME_BROWSER_REBA_ADBLOCK_REBA_ADBLOCK_MAIN_PARTS_H_

#include <memory>

#include "chrome/browser/chrome_browser_main_extra_parts.h"

namespace reba_adblock {

class RebaAdblockService;

// Starts the ad blocker with the browser and stops it on the way out.
class RebaAdblockMainParts : public ChromeBrowserMainExtraParts {
 public:
  RebaAdblockMainParts();
  RebaAdblockMainParts(const RebaAdblockMainParts&) = delete;
  RebaAdblockMainParts& operator=(const RebaAdblockMainParts&) = delete;
  ~RebaAdblockMainParts() override;

  // ChromeBrowserMainExtraParts:
  void PreProfileInit() override;
  void PostMainMessageLoopRun() override;

 private:
  std::unique_ptr<RebaAdblockService> service_;
};

}  // namespace reba_adblock

#endif  // CHROME_BROWSER_REBA_ADBLOCK_REBA_ADBLOCK_MAIN_PARTS_H_
