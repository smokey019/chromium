// Copyright 2026 The REBA Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/reba_adblock/response_scanner.h"

#include <algorithm>
#include <memory>
#include <string_view>
#include <utility>

#include "base/containers/fixed_flat_set.h"
#include "base/functional/bind.h"
#include "base/strings/string_util.h"
#include "base/task/task_runner.h"
#include "mojo/public/cpp/system/data_pipe_producer.h"
#include "mojo/public/cpp/system/string_data_source.h"
#include "net/http/http_response_headers.h"
#include "services/network/public/mojom/url_response_head.mojom.h"
#include "url/gurl.h"

namespace reba_adblock {

namespace {

// Types that are programs, installers, script files or archives whatever is
// in them.
constexpr auto kProgramTypes = base::MakeFixedFlatSet<std::string_view>({
    "application/hta",
    "application/java-archive",
    "application/vnd.android.package-archive",
    "application/vnd.microsoft.portable-executable",
    "application/vnd.ms-cab-compressed",
    "application/vnd.rar",
    "application/x-7z-compressed",
    "application/x-apple-diskimage",
    "application/x-bat",
    "application/x-dosexec",
    "application/x-executable",
    "application/x-iso9660-image",
    "application/x-java-archive",
    "application/x-ms-application",
    "application/x-ms-installer",
    "application/x-ms-shortcut",
    "application/x-msdos-program",
    "application/x-msdownload",
    "application/x-msi",
    "application/x-ole-storage",
    "application/x-rar",
    "application/x-rar-compressed",
    "application/x-zip-compressed",
    "application/zip",
});

// Types that say nothing about what the bytes are.
constexpr auto kUnstatedTypes = base::MakeFixedFlatSet<std::string_view>({
    "",
    "application/download",
    "application/force-download",
    "application/octet-stream",
    "application/unknown",
    "application/x-download",
    "binary/octet-stream",
});

// File name endings of programs, installers, scripts, shortcuts, archives and
// disk images.
constexpr auto kProgramExtensions = base::MakeFixedFlatSet<std::string_view>({
    "7z",  "apk", "appx", "bat", "cab", "cmd", "com",  "cpl", "dll", "exe",
    "hta", "img", "iso",  "jar", "lnk", "msi", "msix", "msp", "ps1", "rar",
    "reg", "scr", "sys",  "vbe", "vbs", "vhd", "vhdx", "wsf", "zip",
});

std::string_view ExtensionOf(std::string_view path) {
  const size_t slash = path.rfind('/');
  if (slash != std::string_view::npos) {
    path = path.substr(slash + 1);
  }
  const size_t dot = path.rfind('.');
  return dot == std::string_view::npos ? std::string_view()
                                       : path.substr(dot + 1);
}

bool StartsWith(base::span<const uint8_t> data, std::string_view prefix) {
  return data.size() >= prefix.size() &&
         std::equal(
             prefix.begin(), prefix.end(), data.begin(),
             [](char a, uint8_t b) { return static_cast<uint8_t>(a) == b; });
}

}  // namespace

ScanNeed ScanNeedFor(const network::mojom::URLResponseHead& head,
                     const GURL& url) {
  const std::string mime = base::ToLowerASCII(head.mime_type);
  if (kProgramTypes.contains(mime)) {
    return ScanNeed::kScan;
  }
  if (kProgramExtensions.contains(
          base::ToLowerASCII(ExtensionOf(url.path())))) {
    return ScanNeed::kScan;
  }
  if (head.headers) {
    const std::string disposition = base::ToLowerASCII(
        head.headers->GetNormalizedHeader("Content-Disposition")
            .value_or(std::string()));
    if (disposition.starts_with("attachment")) {
      return mime.starts_with("text/") || mime.starts_with("image/") ||
                     mime.starts_with("video/") || mime.starts_with("audio/")
                 ? ScanNeed::kNone
                 : ScanNeed::kScan;
    }
  }
  return kUnstatedTypes.contains(mime) ? ScanNeed::kSniff : ScanNeed::kNone;
}

bool LooksLikeProgramOrArchive(base::span<const uint8_t> start) {
  return StartsWith(start, "MZ") ||                                // .exe
         StartsWith(start, "PK\x03\x04") ||                        // .zip
         StartsWith(start, "PK\x05\x06") ||                        // .zip
         StartsWith(start, "Rar!\x1A\x07") ||                      // .rar
         StartsWith(start, "7z\xBC\xAF\x27\x1C") ||                // .7z
         StartsWith(start, "\xD0\xCF\x11\xE0\xA1\xB1\x1A\xE1") ||  // .msi
         StartsWith(start, "MSCF") ||                              // .cab
         StartsWith(start, "%PDF-") ||                             // .pdf
         StartsWith(start,
                    std::string_view("L\0\0\0\x01\x14\x02\0", 8));  // .lnk
}

ResponseScanner::ResponseScanner(mojo::ScopedDataPipeConsumerHandle body,
                                 ScanNeed need,
                                 std::string content_name,
                                 Callback callback)
    : body_(std::move(body)),
      need_(need),
      content_name_(std::move(content_name)),
      callback_(std::move(callback)),
      watcher_(FROM_HERE, mojo::SimpleWatcher::ArmingPolicy::MANUAL) {
  watcher_.Watch(body_.get(),
                 MOJO_HANDLE_SIGNAL_READABLE | MOJO_HANDLE_SIGNAL_PEER_CLOSED,
                 MOJO_WATCH_CONDITION_SATISFIED,
                 base::BindRepeating(&ResponseScanner::OnReadable,
                                     base::Unretained(this)));
  watcher_.ArmOrNotify();
}

ResponseScanner::~ResponseScanner() = default;

void ResponseScanner::OnReadable(MojoResult result,
                                 const mojo::HandleSignalsState& state) {
  if (need_ == ScanNeed::kSniff) {
    if (!Sniff(state.peer_closed())) {
      return;
    }
    if (need_ != ScanNeed::kScan) {
      // Not a program: the page gets the body as it is.
      watcher_.Cancel();
      Finish(Outcome::kNotScanned, std::move(body_));
      return;
    }
  }
  ReadAll();
}

bool ResponseScanner::Sniff(bool peer_closed) {
  uint8_t start[16] = {};
  size_t peeked = 0;
  const MojoResult result =
      body_->ReadData(MOJO_READ_DATA_FLAG_PEEK, start, peeked);
  if (result == MOJO_RESULT_SHOULD_WAIT) {
    watcher_.ArmOrNotify();
    return false;
  }
  if (result != MOJO_RESULT_OK) {
    // Empty and closed.
    need_ = ScanNeed::kNone;
    return true;
  }
  if (peeked < 8 && !peer_closed) {
    // Too little to tell; wait for more. The watcher fires again only for new
    // data.
    watcher_.Cancel();
    watcher_.Watch(
        body_.get(),
        MOJO_HANDLE_SIGNAL_NEW_DATA_READABLE | MOJO_HANDLE_SIGNAL_PEER_CLOSED,
        MOJO_WATCH_CONDITION_SATISFIED,
        base::BindRepeating(&ResponseScanner::OnReadable,
                            base::Unretained(this)));
    watcher_.ArmOrNotify();
    return false;
  }
  need_ = LooksLikeProgramOrArchive(base::span(start).first(peeked))
              ? ScanNeed::kScan
              : ScanNeed::kNone;
  return true;
}

void ResponseScanner::ReadAll() {
  while (true) {
    base::span<const uint8_t> chunk;
    const MojoResult result =
        body_->BeginReadData(MOJO_BEGIN_READ_DATA_FLAG_NONE, chunk);
    if (result == MOJO_RESULT_SHOULD_WAIT) {
      watcher_.ArmOrNotify();
      return;
    }
    if (result != MOJO_RESULT_OK) {
      // The whole body is here.
      watcher_.Cancel();
      body_.reset();
      Scan();
      return;
    }
    if (data_.size() + chunk.size() > kMaxScanBytes) {
      body_->EndReadData(0);
      watcher_.Cancel();
      body_.reset();
      data_.clear();
      Finish(Outcome::kTooLarge, {});
      return;
    }
    data_.append(base::as_string_view(chunk));
    body_->EndReadData(chunk.size());
  }
}

void ResponseScanner::Scan() {
  // The bytes travel with the task and come back with the verdict, so that
  // this object can go away in the meantime.
  AntivirusTaskRunner()->PostTaskAndReplyWithResult(
      FROM_HERE,
      base::BindOnce(
          [](std::string data, std::string name) {
            const AntivirusVerdict verdict =
                ScanWithAntivirus(base::as_byte_span(data), name);
            return std::make_pair(verdict, std::move(data));
          },
          std::move(data_), content_name_),
      base::BindOnce(&ResponseScanner::OnScanned, weak_factory_.GetWeakPtr()));
}

void ResponseScanner::OnScanned(
    std::pair<AntivirusVerdict, std::string> result) {
  data_ = std::move(result.second);
  if (result.first == AntivirusVerdict::kThreat) {
    data_.clear();
    Finish(Outcome::kThreat, {});
    return;
  }
  // Clean, or nobody to ask: pass the bytes on in a new pipe.
  mojo::ScopedDataPipeProducerHandle producer;
  mojo::ScopedDataPipeConsumerHandle consumer;
  if (mojo::CreateDataPipe(std::clamp<size_t>(data_.size(), 1, 2 * 1024 * 1024),
                           producer, consumer) != MOJO_RESULT_OK) {
    data_.clear();
    Finish(Outcome::kFailed, {});
    return;
  }
  auto writer = std::make_unique<mojo::DataPipeProducer>(std::move(producer));
  mojo::DataPipeProducer* raw_writer = writer.get();
  auto text = std::make_unique<std::string>(std::move(data_));
  const base::span<const char> view(*text);
  raw_writer->Write(
      std::make_unique<mojo::StringDataSource>(
          view, mojo::StringDataSource::AsyncWritingMode::
                    STRING_STAYS_VALID_UNTIL_COMPLETION),
      // Owns the writer and the bytes until everything is written.
      base::BindOnce([](std::unique_ptr<mojo::DataPipeProducer>,
                        std::unique_ptr<std::string>, MojoResult) {},
                     std::move(writer), std::move(text)));
  Finish(Outcome::kClean, std::move(consumer));
}

void ResponseScanner::Finish(Outcome outcome,
                             mojo::ScopedDataPipeConsumerHandle body) {
  if (callback_) {
    std::move(callback_).Run(outcome, std::move(body));
  }
}

}  // namespace reba_adblock
