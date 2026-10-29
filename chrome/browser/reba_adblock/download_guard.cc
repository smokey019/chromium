// Copyright 2026 The REBA Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/reba_adblock/download_guard.h"

#include <string_view>
#include <utility>

#include "base/containers/fixed_flat_set.h"
#include "base/files/file_path.h"
#include "base/strings/string_util.h"
#include "chrome/browser/reba_adblock/reba_adblock_service.h"
#include "chrome/browser/reba_adblock/reba_adblock_tab_helper.h"
#include "components/download/public/common/download_danger_type.h"
#include "components/download/public/common/download_interrupt_reasons.h"
#include "components/download/public/common/download_item.h"
#include "components/download/public/common/download_source.h"
#include "components/reba_adblock/core/security_log.h"
#include "content/public/browser/browser_context.h"
#include "content/public/browser/download_item_utils.h"
#include "content/public/browser/web_contents.h"
#include "net/base/registry_controlled_domains/registry_controlled_domain.h"
#include "ui/base/page_transition_types.h"
#include "url/origin.h"

namespace reba_adblock {

namespace {

constexpr auto kRiskyExtensions = base::MakeFixedFlatSet<std::string_view>({
    // Programs and installers.
    "appinstaller",
    "application",
    "appx",
    "appxbundle",
    "com",
    "cpl",
    "dll",
    "exe",
    "gadget",
    "inf",
    "msc",
    "msi",
    "msix",
    "msixbundle",
    "msp",
    "pif",
    "scr",
    "sys",
    // Scripts and shortcuts.
    "bat",
    "cmd",
    "hta",
    "jar",
    "js",
    "jse",
    "lnk",
    "ps1",
    "psm1",
    "reg",
    "url",
    "vbe",
    "vbs",
    "wsf",
    "wsh",
    // Archives and disk images.
    "7z",
    "ace",
    "apk",
    "arj",
    "bz2",
    "cab",
    "dmg",
    "gz",
    "img",
    "iso",
    "lzh",
    "rar",
    "tar",
    "tgz",
    "vhd",
    "vhdx",
    "xz",
    "zip",
    "zst",
});

const char kHeldKey[] = "reba_download_held";

// Marks a download that the guard held, so that it isn't logged again as an
// ordinary dangerous file.
class HeldMark : public base::SupportsUserData::Data {};

bool IsCrossSite(const GURL& a, const url::Origin& b) {
  return !net::registry_controlled_domains::SameDomainOrHost(
      a, b, net::registry_controlled_domains::INCLUDE_PRIVATE_REGISTRIES);
}

bool IsIncognito(download::DownloadItem* download) {
  content::BrowserContext* context =
      content::DownloadItemUtils::GetBrowserContext(download);
  return !context || context->IsOffTheRecord();
}

std::string FileNameOf(download::DownloadItem* download) {
  return download->GetFileNameToReportUser().AsUTF8Unsafe();
}

std::string_view DangerName(download::DownloadDangerType type) {
  switch (type) {
    case download::DOWNLOAD_DANGER_TYPE_DANGEROUS_URL:
      return "dangerous-url";
    case download::DOWNLOAD_DANGER_TYPE_DANGEROUS_CONTENT:
      return "dangerous-content";
    case download::DOWNLOAD_DANGER_TYPE_DANGEROUS_HOST:
      return "dangerous-host";
    case download::DOWNLOAD_DANGER_TYPE_UNCOMMON_CONTENT:
      return "uncommon";
    case download::DOWNLOAD_DANGER_TYPE_POTENTIALLY_UNWANTED:
      return "unwanted";
    case download::DOWNLOAD_DANGER_TYPE_DANGEROUS_ACCOUNT_COMPROMISE:
      return "account-compromise";
    case download::DOWNLOAD_DANGER_TYPE_DANGEROUS_FILE:
      return "file-type";
    default:
      return {};
  }
}

}  // namespace

bool IsRiskyDownloadType(const base::FilePath& file_name) {
  const std::string name = file_name.BaseName().AsUTF8Unsafe();
  const size_t dot = name.rfind('.');
  return dot != std::string::npos &&
         kRiskyExtensions.contains(base::ToLowerASCII(name.substr(dot + 1)));
}

bool ShouldHoldDownload(download::DownloadItem* download,
                        const base::FilePath& file_name) {
  RebaAdblockService* service = RebaAdblockService::Get();
  if (!service || !service->download_guard_enabled() ||
      !IsRiskyDownloadType(file_name)) {
    return false;
  }
  // Only downloads that pages start; not ones the user started from the
  // browser (address bar, "Save link as...", the downloads page, extensions).
  const download::DownloadSource source = download->GetDownloadSource();
  if (source != download::DownloadSource::NAVIGATION &&
      source != download::DownloadSource::FROM_RENDERER &&
      source != download::DownloadSource::UNKNOWN) {
    return false;
  }
  if (download->GetTransitionType() & ui::PAGE_TRANSITION_FROM_ADDRESS_BAR) {
    return false;
  }

  content::WebContents* web_contents =
      content::DownloadItemUtils::GetWebContents(download);
  const GURL page_url = web_contents ? web_contents->GetLastCommittedURL()
                                     : download->GetTabUrl();
  if (service->IsDownloadGuardAllowedOn(page_url)) {
    return false;
  }

  std::string reason;
  const std::optional<url::Origin>& initiator = download->GetRequestInitiator();
  RebaAdblockTabHelper* helper =
      web_contents ? RebaAdblockTabHelper::FromWebContents(web_contents)
                   : nullptr;
  if (!download->HasUserGesture()) {
    reason = "no-click";
  } else if (page_url.SchemeIsHTTPOrHTTPS() && initiator &&
             !initiator->opaque() && IsCrossSite(page_url, *initiator)) {
    reason = "other-site-frame";
  } else if (helper && helper->opened_by() && !helper->opened_by()->opaque() &&
             page_url.SchemeIsHTTPOrHTTPS() &&
             IsCrossSite(page_url, *helper->opened_by())) {
    reason = "popup-tab";
  } else {
    return false;
  }

  download->SetUserData(kHeldKey, std::make_unique<HeldMark>());
  if (helper) {
    helper->OnDownloadHeld();
  }
  SecurityEvent event;
  event.kind = SecurityEventKind::kDownloadHeld;
  event.url = download->GetURL().spec();
  event.file_name = file_name.BaseName().AsUTF8Unsafe();
  event.page_host = page_url.host();
  event.reason = std::move(reason);
  service->RecordSecurityEvent(std::move(event), IsIncognito(download));
  return true;
}

DownloadProtectionObserver::DownloadProtectionObserver(
    content::DownloadManager* manager)
    : notifier_(manager, this) {}

DownloadProtectionObserver::~DownloadProtectionObserver() = default;

void DownloadProtectionObserver::OnDownloadUpdated(
    content::DownloadManager* manager,
    download::DownloadItem* item) {
  RebaAdblockService* service = RebaAdblockService::Get();
  if (!service || logged_.contains(item->GetGuid())) {
    return;
  }

  SecurityEvent event;
  const download::DownloadInterruptReason reason = item->GetLastReason();
  if (item->GetState() == download::DownloadItem::INTERRUPTED &&
      (reason == download::DOWNLOAD_INTERRUPT_REASON_FILE_VIRUS_INFECTED ||
       reason ==
           download::DOWNLOAD_INTERRUPT_REASON_FILE_SECURITY_CHECK_FAILED ||
       reason == download::DOWNLOAD_INTERRUPT_REASON_FILE_BLOCKED)) {
    // Windows' antivirus (through Attachment Services) refused the file.
    event.kind = SecurityEventKind::kAntivirus;
    event.reason = "download";
    event.detail = download::DownloadInterruptReasonToString(reason);
  } else {
    const std::string_view danger = DangerName(item->GetDangerType());
    if (danger.empty()) {
      return;
    }
    if (danger == "file-type") {
      // The guard's own holds are logged when they happen; this is Chromium's
      // warning for a program from a site visited for the first time.
      if (item->GetUserData(kHeldKey)) {
        return;
      }
      event.kind = SecurityEventKind::kDownloadHeld;
      event.reason = "file-type";
    } else {
      event.kind = SecurityEventKind::kSafeBrowsing;
      event.reason = "download";
      event.detail = std::string(danger);
    }
  }
  logged_.insert(item->GetGuid());

  content::WebContents* web_contents =
      content::DownloadItemUtils::GetWebContents(item);
  const GURL page_url =
      web_contents ? web_contents->GetLastCommittedURL() : item->GetTabUrl();
  if (event.kind == SecurityEventKind::kAntivirus && web_contents) {
    if (RebaAdblockTabHelper* helper =
            RebaAdblockTabHelper::FromWebContents(web_contents)) {
      helper->OnThreatBlocked();
    }
  }
  event.url = item->GetURL().spec();
  event.file_name = FileNameOf(item);
  event.page_host = page_url.host();
  service->RecordSecurityEvent(std::move(event), IsIncognito(item));
}

void DownloadProtectionObserver::OnDownloadRemoved(
    content::DownloadManager* manager,
    download::DownloadItem* item) {
  logged_.erase(item->GetGuid());
}

}  // namespace reba_adblock
