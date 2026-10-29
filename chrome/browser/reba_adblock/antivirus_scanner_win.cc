// Copyright 2026 The REBA Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/reba_adblock/antivirus_scanner.h"

#include <windows.h>

#include <amsi.h>

#include <string>

#include "base/no_destructor.h"
#include "base/strings/utf_string_conversions.h"
#include "base/task/single_thread_task_runner.h"
#include "base/task/task_traits.h"
#include "base/task/thread_pool.h"
#include "base/threading/scoped_blocking_call.h"

namespace reba_adblock {

namespace {

// AMSI's context, made on first use and kept for the life of the browser.
// Only touched on AntivirusTaskRunner().
struct AmsiState {
  bool tried = false;
  HAMSICONTEXT context = nullptr;
};

AmsiState& State() {
  static AmsiState state;
  return state;
}

}  // namespace

scoped_refptr<base::SequencedTaskRunner> AntivirusTaskRunner() {
  // AMSI talks to the antivirus through COM.
  static base::NoDestructor<scoped_refptr<base::SequencedTaskRunner>> runner(
      base::ThreadPool::CreateCOMSTATaskRunner(
          {base::MayBlock(), base::TaskPriority::USER_VISIBLE,
           base::TaskShutdownBehavior::CONTINUE_ON_SHUTDOWN}));
  return *runner;
}

AntivirusVerdict ScanWithAntivirus(base::span<const uint8_t> data,
                                   std::string_view content_name) {
  base::ScopedBlockingCall scoped_blocking_call(FROM_HERE,
                                                base::BlockingType::MAY_BLOCK);
  AmsiState& state = State();
  if (!state.tried) {
    state.tried = true;
    if (FAILED(AmsiInitialize(L"REBA Browser", &state.context))) {
      state.context = nullptr;
    }
  }
  if (!state.context || data.size() > 0xFFFFFFFFu) {
    return AntivirusVerdict::kUnavailable;
  }

  HAMSISESSION session = nullptr;
  if (FAILED(AmsiOpenSession(state.context, &session))) {
    session = nullptr;
  }
  const std::wstring name = base::UTF8ToWide(content_name);
  AMSI_RESULT result = AMSI_RESULT_CLEAN;
  // AmsiScanBuffer doesn't write to the buffer; it only takes a PVOID.
  const HRESULT hr = AmsiScanBuffer(
      state.context, const_cast<uint8_t*>(data.data()),
      static_cast<ULONG>(data.size()), name.c_str(), session, &result);
  if (session) {
    AmsiCloseSession(state.context, session);
  }
  if (FAILED(hr)) {
    return AntivirusVerdict::kUnavailable;
  }
  return AmsiResultIsMalware(result) ? AntivirusVerdict::kThreat
                                     : AntivirusVerdict::kClean;
}

}  // namespace reba_adblock
