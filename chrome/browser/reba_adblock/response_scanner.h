// Copyright 2026 The REBA Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_REBA_ADBLOCK_RESPONSE_SCANNER_H_
#define CHROME_BROWSER_REBA_ADBLOCK_RESPONSE_SCANNER_H_

#include <cstdint>
#include <string>
#include <utility>

#include "base/containers/span.h"
#include "base/functional/callback.h"
#include "base/memory/weak_ptr.h"
#include "chrome/browser/reba_adblock/antivirus_scanner.h"
#include "mojo/public/cpp/system/data_pipe.h"
#include "mojo/public/cpp/system/simple_watcher.h"
#include "services/network/public/mojom/url_response_head.mojom-forward.h"

class GURL;

namespace reba_adblock {

// Which responses that a page fetches are worth handing to the antivirus.
enum class ScanNeed {
  kNone,
  // A program, installer, script file or archive by its type, its name or
  // because the server wants it saved.
  kScan,
  // Of no stated type (application/octet-stream and the like): scanned if the
  // first bytes are those of a program or an archive.
  kSniff,
};

ScanNeed ScanNeedFor(const network::mojom::URLResponseHead& head,
                     const GURL& url);

// Whether `start` (the first bytes of a body) is a Windows program, an
// installer, an archive, a shortcut or a PDF.
bool LooksLikeProgramOrArchive(base::span<const uint8_t> start);

// Reads a response body, has the antivirus look at it, and hands back a body
// for the page: the original pipe when it wasn't worth scanning, a new one
// with the same bytes when it was clean. Destroying it stops everything.
class ResponseScanner {
 public:
  // Bodies are kept in memory while they are scanned; larger ones that come
  // without a Content-Length can't be checked.
  static constexpr size_t kMaxScanBytes = 64 * 1024 * 1024;

  enum class Outcome {
    // Not a program or an archive after all; `body` is the original pipe.
    kNotScanned,
    // Scanned (or there is no antivirus to ask); `body` has the bytes.
    kClean,
    // The antivirus found a threat. No body.
    kThreat,
    // Too large to check. No body.
    kTooLarge,
    // The body couldn't be passed on. No body.
    kFailed,
  };
  using Callback =
      base::OnceCallback<void(Outcome, mojo::ScopedDataPipeConsumerHandle)>;

  // `content_name` is what the antivirus reports as the source.
  ResponseScanner(mojo::ScopedDataPipeConsumerHandle body,
                  ScanNeed need,
                  std::string content_name,
                  Callback callback);
  ResponseScanner(const ResponseScanner&) = delete;
  ResponseScanner& operator=(const ResponseScanner&) = delete;
  ~ResponseScanner();

 private:
  void OnReadable(MojoResult result, const mojo::HandleSignalsState& state);
  // Looks at the first bytes without taking them. Returns false to wait for
  // more.
  bool Sniff(bool peer_closed);
  void ReadAll();
  void Scan();
  void OnScanned(std::pair<AntivirusVerdict, std::string> result);
  void Finish(Outcome outcome, mojo::ScopedDataPipeConsumerHandle body);

  mojo::ScopedDataPipeConsumerHandle body_;
  ScanNeed need_;
  const std::string content_name_;
  Callback callback_;
  mojo::SimpleWatcher watcher_;
  std::string data_;
  base::WeakPtrFactory<ResponseScanner> weak_factory_{this};
};

}  // namespace reba_adblock

#endif  // CHROME_BROWSER_REBA_ADBLOCK_RESPONSE_SCANNER_H_
