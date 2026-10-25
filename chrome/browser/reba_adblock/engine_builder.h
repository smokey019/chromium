// Copyright 2026 The REBA Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_REBA_ADBLOCK_ENGINE_BUILDER_H_
#define CHROME_BROWSER_REBA_ADBLOCK_ENGINE_BUILDER_H_

#include <memory>
#include <string>
#include <vector>

#include "base/files/file_path.h"
#include "base/memory/scoped_refptr.h"
#include "components/reba_adblock/core/engine_set.h"
#include "components/reba_adblock/core/filter_list_catalog.h"

namespace reba_adblock {

// One filter list that goes into the engine.
struct ListSource {
  ListSource();
  ListSource(const ListSource&);
  ListSource& operator=(const ListSource&);
  ~ListSource();

  FilterList list;
  // Where the downloaded copy of the list is.
  base::FilePath path;
  // Identifies the content of `path`, for the cache of compiled engines: the
  // same value means the same text.
  std::string content_stamp;
};

struct EngineBuildInput {
  EngineBuildInput();
  EngineBuildInput(const EngineBuildInput&);
  EngineBuildInput& operator=(const EngineBuildInput&);
  ~EngineBuildInput();

  std::vector<ListSource> lists;
  // JSON file with the scriptlets and redirect resources; may not exist.
  base::FilePath resources_path;
  // Where compiled engines are kept between runs.
  base::FilePath cache_dir;
  std::string user_filters;
};

// What BuildEngineSet() made.
struct EngineBuildResult {
  EngineBuildResult();
  EngineBuildResult(const EngineBuildResult&);
  EngineBuildResult(EngineBuildResult&&);
  EngineBuildResult& operator=(const EngineBuildResult&);
  EngineBuildResult& operator=(EngineBuildResult&&);
  ~EngineBuildResult();

  // Null if there is nothing to build.
  scoped_refptr<EngineSet> engines;
  // Lists whose downloaded file is missing or can't be read. They are left
  // out of `engines`, and an engine without them isn't cached, so they should
  // be downloaded again.
  std::vector<std::string> missing_list_ids;
};

// Compiles the lists into an EngineSet, or loads the compiled form from
// `cache_dir` if it is there. Reads files and takes a few seconds, so it must
// run where blocking is allowed.
//
// The lines of `input.user_filters` that act on the lists' own filters
// (UserListExceptions()) are compiled in with the lists, so the lists have to
// be built again when they change.
EngineBuildResult BuildEngineSet(const EngineBuildInput& input);

// An EngineSet with the user's filters only, for when the lists aren't there
// yet.
scoped_refptr<EngineSet> BuildUserOnlyEngineSet(
    const std::string& user_filters,
    const base::FilePath& resources_path);

// Reads the resources file, empty if it can't be read.
std::string ReadResourcesFile(const base::FilePath& path);

// Number of filters in the text of a list (lines that aren't comments).
int CountRules(std::string_view list_text);

}  // namespace reba_adblock

#endif  // CHROME_BROWSER_REBA_ADBLOCK_ENGINE_BUILDER_H_
