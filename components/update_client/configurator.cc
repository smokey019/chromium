// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "components/update_client/configurator.h"

namespace update_client {

std::optional<CupKey> Configurator::GetCupKey() const {
  return std::nullopt;
}

}  // namespace update_client
