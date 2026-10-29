// Copyright 2026 The REBA Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_REBA_ADBLOCK_DOWNLOAD_GUARD_H_
#define CHROME_BROWSER_REBA_ADBLOCK_DOWNLOAD_GUARD_H_

#include <set>
#include <string>

#include "components/download/content/public/all_download_item_notifier.h"

namespace base {
class FilePath;
}

namespace content {
class DownloadManager;
}

namespace download {
class DownloadItem;
}

namespace reba_adblock {

// Whether `file_name` is a program, installer, script, shortcut, archive or
// disk image: what malicious pages want people to download.
bool IsRiskyDownloadType(const base::FilePath& file_name);

// The download guard: whether `download`, to be saved as `file_name`, is held
// for the user to keep or delete. That is so for a risky type when
//   * it started without a click (a script started it),
//   * a frame of another site than the page started it (an ad), or
//   * it started in a tab that a page of another site opened (a pop-up).
// Downloads typed into the address bar, saved from the context menu or started
// by extensions are not held. A held download is logged. Called while the
// download's target is determined; the caller marks it dangerous.
bool ShouldHoldDownload(download::DownloadItem* download,
                        const base::FilePath& file_name);

// Logs, in the protection log, the downloads of a profile that Windows'
// antivirus blocked or that Safe Browsing warned about.
class DownloadProtectionObserver
    : public download::AllDownloadItemNotifier::Observer {
 public:
  explicit DownloadProtectionObserver(content::DownloadManager* manager);
  DownloadProtectionObserver(const DownloadProtectionObserver&) = delete;
  DownloadProtectionObserver& operator=(const DownloadProtectionObserver&) =
      delete;
  ~DownloadProtectionObserver() override;

  // download::AllDownloadItemNotifier::Observer:
  void OnDownloadUpdated(content::DownloadManager* manager,
                         download::DownloadItem* item) override;
  void OnDownloadRemoved(content::DownloadManager* manager,
                         download::DownloadItem* item) override;

 private:
  download::AllDownloadItemNotifier notifier_;
  // GUIDs of the downloads already logged, so each is logged once.
  std::set<std::string> logged_;
};

}  // namespace reba_adblock

#endif  // CHROME_BROWSER_REBA_ADBLOCK_DOWNLOAD_GUARD_H_
