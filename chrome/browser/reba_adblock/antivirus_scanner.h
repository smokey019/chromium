// Copyright 2026 The REBA Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_REBA_ADBLOCK_ANTIVIRUS_SCANNER_H_
#define CHROME_BROWSER_REBA_ADBLOCK_ANTIVIRUS_SCANNER_H_

#include <cstdint>
#include <string_view>

#include "base/containers/span.h"
#include "base/memory/scoped_refptr.h"
#include "base/task/sequenced_task_runner.h"

namespace reba_adblock {

enum class AntivirusVerdict {
  kClean,
  // The antivirus found a threat.
  kThreat,
  // There is no antivirus to ask, or it didn't answer.
  kUnavailable,
};

// The sequence that scans run on (a COM thread on Windows).
scoped_refptr<base::SequencedTaskRunner> AntivirusTaskRunner();

// Hands `data` to the antivirus registered with Windows (through AMSI, the
// Antimalware Scan Interface; Microsoft Defender unless another antivirus
// replaced it) and returns its verdict. `content_name` is what the antivirus
// shows as the source, usually the address. Blocks; run it on
// AntivirusTaskRunner(). Elsewhere than on Windows it always answers
// kUnavailable.
AntivirusVerdict ScanWithAntivirus(base::span<const uint8_t> data,
                                   std::string_view content_name);

}  // namespace reba_adblock

#endif  // CHROME_BROWSER_REBA_ADBLOCK_ANTIVIRUS_SCANNER_H_
