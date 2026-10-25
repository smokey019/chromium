// Copyright 2026 The REBA Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_UI_WEBUI_REBA_ADBLOCK_ADBLOCK_POPUP_CONTEXT_H_
#define CHROME_BROWSER_UI_WEBUI_REBA_ADBLOCK_ADBLOCK_POPUP_CONTEXT_H_

#include "base/functional/callback.h"
#include "base/memory/weak_ptr.h"
#include "content/public/browser/web_contents.h"
#include "content/public/browser/web_contents_user_data.h"

namespace reba_adblock {

// Attached to the contents of the toolbar pop-up to tell its page about the tab
// it was opened for, and how to close the pop-up.
class AdblockPopupContext
    : public content::WebContentsUserData<AdblockPopupContext> {
 public:
  ~AdblockPopupContext() override;

  // The tab the pop-up is about. Null if the tab is gone.
  content::WebContents* source() const { return source_.get(); }

  void SetCloseCallback(base::OnceClosure close) { close_ = std::move(close); }
  void Close();

 private:
  friend class content::WebContentsUserData<AdblockPopupContext>;
  AdblockPopupContext(content::WebContents* popup,
                      content::WebContents* source);

  base::WeakPtr<content::WebContents> source_;
  base::OnceClosure close_;

  WEB_CONTENTS_USER_DATA_KEY_DECL();
};

}  // namespace reba_adblock

#endif  // CHROME_BROWSER_UI_WEBUI_REBA_ADBLOCK_ADBLOCK_POPUP_CONTEXT_H_
