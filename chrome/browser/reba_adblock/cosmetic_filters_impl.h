// Copyright 2026 The REBA Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_REBA_ADBLOCK_COSMETIC_FILTERS_IMPL_H_
#define CHROME_BROWSER_REBA_ADBLOCK_COSMETIC_FILTERS_IMPL_H_

#include "components/reba_adblock/common/reba_adblock.mojom.h"
#include "content/public/browser/document_service.h"
#include "mojo/public/cpp/bindings/pending_receiver.h"

namespace content {
class RenderFrameHost;
}

namespace reba_adblock {

// Answers a frame's questions about what to hide and which scriptlets to run.
class CosmeticFiltersImpl
    : public content::DocumentService<mojom::CosmeticFilters> {
 public:
  static void Create(content::RenderFrameHost* host,
                     mojo::PendingReceiver<mojom::CosmeticFilters> receiver);

  CosmeticFiltersImpl(const CosmeticFiltersImpl&) = delete;
  CosmeticFiltersImpl& operator=(const CosmeticFiltersImpl&) = delete;

  // mojom::CosmeticFilters:
  void GetCosmeticResources(const GURL& url,
                            GetCosmeticResourcesCallback callback) override;
  void HiddenClassIdSelectors(const std::vector<std::string>& classes,
                              const std::vector<std::string>& ids,
                              const std::vector<std::string>& exceptions,
                              HiddenClassIdSelectorsCallback callback) override;
  void ReportHiddenElements(uint32_t count) override;
  void ReportBlockedPopup(const GURL& url) override;
  void AddCosmeticFilter(const std::string& selector) override;

 private:
  CosmeticFiltersImpl(content::RenderFrameHost& host,
                      mojo::PendingReceiver<mojom::CosmeticFilters> receiver);
  ~CosmeticFiltersImpl() override;

  // The page the frame is in, for trusted sites.
  GURL TopLevelUrl() const;

  // How many refused pop-ups the document reported, to keep a page from
  // flooding the log.
  int reported_popups_ = 0;
};

}  // namespace reba_adblock

#endif  // CHROME_BROWSER_REBA_ADBLOCK_COSMETIC_FILTERS_IMPL_H_
