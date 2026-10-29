// Copyright 2026 The REBA Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/reba_adblock/antivirus_scanner.h"

#include "base/no_destructor.h"
#include "base/task/thread_pool.h"

namespace reba_adblock {

scoped_refptr<base::SequencedTaskRunner> AntivirusTaskRunner() {
  static base::NoDestructor<scoped_refptr<base::SequencedTaskRunner>> runner(
      base::ThreadPool::CreateSequencedTaskRunner({base::MayBlock()}));
  return *runner;
}

AntivirusVerdict ScanWithAntivirus(base::span<const uint8_t> data,
                                   std::string_view content_name) {
  return AntivirusVerdict::kUnavailable;
}

}  // namespace reba_adblock
