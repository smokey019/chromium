// Copyright 2026 The REBA Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/reba_adblock/reba_adblock_main_parts.h"

#include "base/files/file_path.h"
#include "base/path_service.h"
#include "chrome/browser/browser_process.h"
#include "chrome/browser/reba_adblock/reba_adblock_service.h"
#include "chrome/common/chrome_paths.h"

namespace reba_adblock {

RebaAdblockMainParts::RebaAdblockMainParts() = default;
RebaAdblockMainParts::~RebaAdblockMainParts() = default;

void RebaAdblockMainParts::PreProfileInit() {
  base::FilePath user_data_dir;
  if (!base::PathService::Get(chrome::DIR_USER_DATA, &user_data_dir)) {
    return;
  }
  service_ = std::make_unique<RebaAdblockService>(
      g_browser_process->local_state(),
      user_data_dir.AppendASCII("REBA Adblock"),
      g_browser_process->shared_url_loader_factory());
  service_->Start();
}

void RebaAdblockMainParts::PostMainMessageLoopRun() {
  service_.reset();
}

}  // namespace reba_adblock
