// Copyright 2026 The REBA Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/reba_adblock/resources_manager.h"

#include <utility>

#include "base/base64.h"
#include "base/files/file_util.h"
#include "base/files/important_file_writer.h"
#include "base/functional/bind.h"
#include "base/logging.h"
#include "base/strings/string_util.h"
#include "base/task/sequenced_task_runner.h"
#include "base/time/time.h"
#include "base/values.h"
#include "chrome/browser/reba_adblock/reba_adblock_prefs.h"
#include "components/prefs/pref_service.h"
#include "components/prefs/scoped_user_pref_update.h"
#include "crypto/keypair.h"
#include "crypto/sign.h"
#include "net/traffic_annotation/network_traffic_annotation.h"
#include "services/network/public/cpp/resource_request.h"
#include "services/network/public/cpp/shared_url_loader_factory.h"
#include "services/network/public/cpp/simple_url_loader.h"
#include "services/network/public/mojom/fetch_api.mojom.h"
#include "url/gurl.h"

namespace reba_adblock {

namespace {

// Where the signed file is published. The signature is at the same address
// with ".sig" added. Builds up to 158.0.8119.4 read it from the LEET-era Space
// (leet-releases), which keeps a copy until they have updated.
constexpr char kResourcesUrl[] =
    "https://reba-releases.nyc3.cdn.digitaloceanspaces.com/adblock/"
    "resources.json";

// Public key (SubjectPublicKeyInfo, DER, base64) that the file is signed with.
// The private half is not in any repository.
constexpr char kResourcesPublicKey[] =
    "MFkwEwYHKoZIzj0CAQYIKoZIzj0DAQcDQgAEPmMptpZ6qTNcvGhV8MDY/"
    "RlHnOIzvziWUq7iIXXB"
    "LqTlINLR8tNYkQTVrMfIfCecPfsvmCrwdnqykbbCg4bm3g==";

constexpr char kStateKey[] = "resources";
constexpr base::TimeDelta kMaxAge = base::Days(7);
constexpr base::TimeDelta kRetryAfterFailure = base::Hours(6);
constexpr size_t kMaxSize = 32 * 1024 * 1024;
constexpr base::TimeDelta kDownloadTimeout = base::Seconds(60);

constexpr net::NetworkTrafficAnnotationTag kTrafficAnnotation =
    net::DefineNetworkTrafficAnnotation("reba_adblock_resources", R"(
      semantics {
        sender: "REBA ad blocker"
        description:
          "Downloads the scriptlets and stand-in resources the built-in ad "
          "blocker's filters refer to, and a signature for them."
        trigger:
          "The copy on disk is missing or a week old (checked at startup and "
          "every half hour), or the user presses 'Update now'."
        data: "The request has no cookies and carries no user data."
        destination: OTHER
        internal {
          contacts {
            email: "reba-browser@example.invalid"
          }
        }
        user_data {
          type: NONE
        }
        last_reviewed: "2026-10-23"
      }
      policy {
        cookies_allowed: NO
        setting:
          "Settings > Ad blocker: turn off 'Update the lists automatically' or "
          "turn the ad blocker off."
        policy_exception_justification: "Not implemented."
      })");

bool WriteFile(const base::FilePath& path, std::string data) {
  if (!base::CreateDirectory(path.DirName())) {
    return false;
  }
  return base::ImportantFileWriter::WriteFileAtomically(path, data);
}

std::unique_ptr<network::SimpleURLLoader> MakeLoader(const std::string& url) {
  auto request = std::make_unique<network::ResourceRequest>();
  request->url = GURL(url);
  request->method = "GET";
  request->credentials_mode = network::mojom::CredentialsMode::kOmit;
  auto loader =
      network::SimpleURLLoader::Create(std::move(request), kTrafficAnnotation);
  loader->SetTimeoutDuration(kDownloadTimeout);
  return loader;
}

}  // namespace

ResourcesManager::ResourcesManager(
    const base::FilePath& path,
    scoped_refptr<network::SharedURLLoaderFactory> url_loader_factory,
    scoped_refptr<base::SequencedTaskRunner> file_task_runner,
    PrefService* local_state,
    base::RepeatingClosure on_changed)
    : path_(path),
      url_loader_factory_(std::move(url_loader_factory)),
      file_task_runner_(std::move(file_task_runner)),
      local_state_(local_state),
      on_changed_(std::move(on_changed)) {}

ResourcesManager::~ResourcesManager() = default;

// static
bool ResourcesManager::VerifySignature(const std::string& data,
                                       const std::string& signature) {
  std::string key_der, signature_der;
  if (!base::Base64Decode(kResourcesPublicKey, &key_der) ||
      !base::Base64Decode(base::TrimWhitespaceASCII(signature, base::TRIM_ALL),
                          &signature_der)) {
    return false;
  }
  std::optional<crypto::keypair::PublicKey> key =
      crypto::keypair::PublicKey::FromSubjectPublicKeyInfo(
          base::as_byte_span(key_der));
  if (!key) {
    return false;
  }
  return crypto::sign::Verify(crypto::sign::ECDSA_SHA256, *key,
                              base::as_byte_span(data),
                              base::as_byte_span(signature_der));
}

void ResourcesManager::Update(bool force) {
  if (downloading_) {
    return;
  }
  const base::DictValue* state =
      local_state_->GetDict(prefs::kListStates).FindDict(kStateKey);
  const double updated = state ? state->FindDouble("updated").value_or(0) : 0;
  const double failed = state ? state->FindDouble("failed").value_or(0) : 0;
  const base::Time now = base::Time::Now();
  const bool have_file = updated > 0;
  if (!force) {
    if (have_file &&
        now - base::Time::FromSecondsSinceUnixEpoch(updated) < kMaxAge) {
      return;
    }
    if (failed > 0 && now - base::Time::FromSecondsSinceUnixEpoch(failed) <
                          kRetryAfterFailure) {
      return;
    }
  }
  downloading_ = true;
  auto loader = MakeLoader(kResourcesUrl);
  network::SimpleURLLoader* raw = loader.get();
  raw->DownloadToString(
      url_loader_factory_.get(),
      base::BindOnce(&ResourcesManager::OnFileDownloaded,
                     weak_factory_.GetWeakPtr(), std::move(loader)),
      kMaxSize);
}

void ResourcesManager::OnFileDownloaded(
    std::unique_ptr<network::SimpleURLLoader> loader,
    std::optional<std::string> body) {
  if (!body || body->empty()) {
    LOG(WARNING) << "REBA adblock: could not download the resources";
    downloading_ = false;
    ScopedDictPrefUpdate update(local_state_, prefs::kListStates);
    update->EnsureDict(kStateKey)->Set(
        "failed", base::Time::Now().InSecondsFSinceUnixEpoch());
    return;
  }
  auto signature_loader = MakeLoader(std::string(kResourcesUrl) + ".sig");
  network::SimpleURLLoader* raw = signature_loader.get();
  raw->DownloadToString(
      url_loader_factory_.get(),
      base::BindOnce(&ResourcesManager::OnSignatureDownloaded,
                     weak_factory_.GetWeakPtr(), std::move(*body),
                     std::move(signature_loader)),
      4096);
}

void ResourcesManager::OnSignatureDownloaded(
    std::string data,
    std::unique_ptr<network::SimpleURLLoader> loader,
    std::optional<std::string> signature) {
  if (!signature || !VerifySignature(data, *signature)) {
    LOG(WARNING) << "REBA adblock: the resources are not signed correctly; "
                    "ignoring them";
    downloading_ = false;
    ScopedDictPrefUpdate update(local_state_, prefs::kListStates);
    update->EnsureDict(kStateKey)->Set(
        "failed", base::Time::Now().InSecondsFSinceUnixEpoch());
    return;
  }
  file_task_runner_->PostTaskAndReplyWithResult(
      FROM_HERE, base::BindOnce(&WriteFile, path_, std::move(data)),
      base::BindOnce(&ResourcesManager::OnWritten, weak_factory_.GetWeakPtr()));
}

void ResourcesManager::OnWritten(bool ok) {
  downloading_ = false;
  if (!ok) {
    return;
  }
  {
    ScopedDictPrefUpdate update(local_state_, prefs::kListStates);
    base::DictValue* state = update->EnsureDict(kStateKey);
    state->Set("updated", base::Time::Now().InSecondsFSinceUnixEpoch());
    state->Remove("failed");
  }
  on_changed_.Run();
}

}  // namespace reba_adblock
