// Copyright 2026 The REBA Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_REBA_ADBLOCK_RESOURCES_MANAGER_H_
#define CHROME_BROWSER_REBA_ADBLOCK_RESOURCES_MANAGER_H_

#include <memory>
#include <string>

#include "base/files/file_path.h"
#include "base/functional/callback.h"
#include "base/memory/raw_ptr.h"
#include "base/memory/ref_counted.h"
#include "base/memory/weak_ptr.h"
#include "base/time/time.h"

class PrefService;

namespace base {
class SequencedTaskRunner;
}

namespace network {
class SharedURLLoaderFactory;
class SimpleURLLoader;
}  // namespace network

namespace reba_adblock {

// Keeps the file with the scriptlets and stand-in resources up to date. The
// file is made by tools/build_resources.mjs from uBlock Origin, signed, and
// published; it is downloaded here (not shipped in the browser) and only used
// if the signature checks out.
class ResourcesManager {
 public:
  ResourcesManager(
      const base::FilePath& path,
      scoped_refptr<network::SharedURLLoaderFactory> url_loader_factory,
      scoped_refptr<base::SequencedTaskRunner> file_task_runner,
      PrefService* local_state,
      base::RepeatingClosure on_changed);
  ResourcesManager(const ResourcesManager&) = delete;
  ResourcesManager& operator=(const ResourcesManager&) = delete;
  ~ResourcesManager();

  const base::FilePath& path() const { return path_; }

  // Downloads a new copy if there is none or it is old. With `force`, always.
  // Only a copy whose signature checks out is written to `path`.
  void Update(bool force);

  // Checks `signature` (base64 of a DER ECDSA P-256 / SHA-256 signature) of
  // `data` against the key built into the browser.
  static bool VerifySignature(const std::string& data,
                              const std::string& signature);

 private:
  void OnFileDownloaded(std::unique_ptr<network::SimpleURLLoader> loader,
                        std::optional<std::string> body);
  void OnSignatureDownloaded(std::string data,
                             std::unique_ptr<network::SimpleURLLoader> loader,
                             std::optional<std::string> signature);
  void OnWritten(bool ok);

  const base::FilePath path_;
  scoped_refptr<network::SharedURLLoaderFactory> url_loader_factory_;
  scoped_refptr<base::SequencedTaskRunner> file_task_runner_;
  const raw_ptr<PrefService> local_state_;
  base::RepeatingClosure on_changed_;
  bool downloading_ = false;
  base::WeakPtrFactory<ResourcesManager> weak_factory_{this};
};

}  // namespace reba_adblock

#endif  // CHROME_BROWSER_REBA_ADBLOCK_RESOURCES_MANAGER_H_
