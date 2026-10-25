// Copyright 2026 The REBA Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/reba_adblock/engine_builder.h"

#include <utility>

#include "base/containers/span.h"
#include "base/files/file_enumerator.h"
#include "base/files/file_util.h"
#include "base/files/important_file_writer.h"
#include "base/logging.h"
#include "base/strings/strcat.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/string_split.h"
#include "base/strings/string_util.h"
#include "base/strings/string_view_util.h"
#include "base/timer/elapsed_timer.h"
#include "components/reba_adblock/core/user_filters.h"
#include "components/reba_adblock/engine/adblock_engine.h"
#include "components/version_info/version_info.h"
#include "crypto/hash.h"

namespace reba_adblock {

namespace {

// Bump when the way lists are compiled changes, so old caches are ignored.
constexpr char kCacheVersion[] = "3";

std::string HexEncodeSha256(std::string_view data) {
  return base::HexEncode(crypto::hash::Sha256(base::as_byte_span(data)));
}

// The cache file for the lists of `input` compiled together with
// `user_exceptions` (see UserListExceptions()). The browser version is part of
// the key, so that an update, which may come with another adblock-rust, never
// loads what an older one compiled. The resources aren't: they are added to
// the engine after it is loaded.
base::FilePath CacheFileFor(const EngineBuildInput& input,
                            std::string_view user_exceptions) {
  std::string key =
      base::StrCat({kCacheVersion, "|", version_info::GetVersionNumber(), "|",
                    HexEncodeSha256(user_exceptions)});
  for (const ListSource& source : input.lists) {
    base::StrAppend(&key, {"|", source.list.id, ":", source.content_stamp, ":",
                           source.list.trusted ? "t" : "u"});
  }
  return input.cache_dir.AppendASCII(
      base::StrCat({"engine-", HexEncodeSha256(key).substr(0, 24), ".bin"}));
}

void DeleteStaleCaches(const base::FilePath& cache_dir,
                       const base::FilePath& keep) {
  base::FileEnumerator files(cache_dir, /*recursive=*/false,
                             base::FileEnumerator::FILES,
                             FILE_PATH_LITERAL("engine-*.bin"));
  for (base::FilePath file = files.Next(); !file.empty(); file = files.Next()) {
    if (file != keep) {
      base::DeleteFile(file);
    }
  }
}

}  // namespace

ListSource::ListSource() = default;
ListSource::ListSource(const ListSource&) = default;
ListSource& ListSource::operator=(const ListSource&) = default;
ListSource::~ListSource() = default;

EngineBuildInput::EngineBuildInput() = default;
EngineBuildInput::EngineBuildInput(const EngineBuildInput&) = default;
EngineBuildInput& EngineBuildInput::operator=(const EngineBuildInput&) =
    default;
EngineBuildInput::~EngineBuildInput() = default;

EngineBuildResult::EngineBuildResult() = default;
EngineBuildResult::EngineBuildResult(const EngineBuildResult&) = default;
EngineBuildResult::EngineBuildResult(EngineBuildResult&&) = default;
EngineBuildResult& EngineBuildResult::operator=(const EngineBuildResult&) =
    default;
EngineBuildResult& EngineBuildResult::operator=(EngineBuildResult&&) = default;
EngineBuildResult::~EngineBuildResult() = default;

std::string ReadResourcesFile(const base::FilePath& path) {
  std::string contents;
  if (path.empty() || !base::ReadFileToString(path, &contents)) {
    return std::string();
  }
  return contents;
}

int CountRules(std::string_view list_text) {
  int count = 0;
  for (std::string_view line : base::SplitStringPiece(
           list_text, "\n", base::TRIM_WHITESPACE, base::SPLIT_WANT_NONEMPTY)) {
    if (line.starts_with('!') || line.starts_with('[') || line.starts_with('#') ||
        line.starts_with("# ")) {
      // `##.ad` is a filter, `# comment` and `#!if` aren't.
      if (line.starts_with("##") || line.starts_with("#@#") ||
          line.starts_with("#?#")) {
        ++count;
      }
      continue;
    }
    ++count;
  }
  return count;
}

EngineBuildResult BuildEngineSet(const EngineBuildInput& input) {
  EngineBuildResult result;
  if (input.lists.empty()) {
    result.engines =
        BuildUserOnlyEngineSet(input.user_filters, input.resources_path);
    return result;
  }
  base::ElapsedTimer timer;

  auto resources_json = std::make_shared<const std::string>(
      ReadResourcesFile(input.resources_path));
  // `$badfilter`, `site#@#+js()` and the like in the user's filters change
  // what the lists' own filters do, which adblock-rust only does within one
  // engine. They go in as a last list.
  const std::string user_exceptions = UserListExceptions(input.user_filters);
  const base::FilePath cache_file = CacheFileFor(input, user_exceptions);

  std::shared_ptr<const AdblockEngine> engine;
  std::vector<EngineSet::ListInfo> infos;
  if (std::string cached; !input.cache_dir.empty() &&
                          base::ReadFileToString(cache_file, &cached)) {
    engine = AdblockEngine::Deserialize(base::as_byte_span(cached),
                                        *resources_json);
    if (engine) {
      for (const ListSource& source : input.lists) {
        infos.push_back({.id = source.list.id, .title = source.list.title});
      }
      VLOG(1) << "REBA adblock: loaded cached engine in "
              << timer.Elapsed().InMilliseconds() << " ms";
    }
  }

  if (!engine) {
    std::vector<FilterListInput> inputs;
    for (const ListSource& source : input.lists) {
      std::string text;
      if (!base::ReadFileToString(source.path, &text)) {
        // Compiled as empty, it would stay empty until the list's next
        // update. Left out, the caller can download it again.
        result.missing_list_ids.push_back(source.list.id);
        continue;
      }
      FilterListInput list_input;
      list_input.is_hosts = LooksLikeHostsFile(text);
      list_input.trusted = source.list.trusted;
      list_input.text = std::move(text);
      inputs.push_back(std::move(list_input));
      infos.push_back({.id = source.list.id, .title = source.list.title});
    }
    if (inputs.empty()) {
      result.engines = EngineSet::Create(nullptr, {}, std::move(resources_json),
                                         input.user_filters);
      return result;
    }
    if (!user_exceptions.empty()) {
      inputs.push_back({.text = user_exceptions});
    }
    std::unique_ptr<AdblockEngine> built =
        AdblockEngine::Build(std::move(inputs), *resources_json);
    VLOG(1) << "REBA adblock: compiled " << infos.size() << " lists in "
            << timer.Elapsed().InMilliseconds() << " ms";
    // The cache file is named after every list in `input`, so an engine
    // without some of them must not be saved under that name.
    if (result.missing_list_ids.empty() && !input.cache_dir.empty() &&
        base::CreateDirectory(input.cache_dir)) {
      const std::vector<uint8_t> data = built->Serialize();
      if (!data.empty() && base::ImportantFileWriter::WriteFileAtomically(
                               cache_file, base::as_string_view(data))) {
        DeleteStaleCaches(input.cache_dir, cache_file);
      }
    }
    engine = std::move(built);
  }

  if (!user_exceptions.empty()) {
    infos.push_back({.id = kUserFiltersListId, .title = kUserFiltersListTitle});
  }
  result.engines =
      EngineSet::Create(std::move(engine), std::move(infos),
                        std::move(resources_json), input.user_filters);
  return result;
}

scoped_refptr<EngineSet> BuildUserOnlyEngineSet(
    const std::string& user_filters,
    const base::FilePath& resources_path) {
  return EngineSet::Create(
      nullptr, {},
      std::make_shared<const std::string>(ReadResourcesFile(resources_path)),
      user_filters);
}

}  // namespace reba_adblock
