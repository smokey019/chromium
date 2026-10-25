// Copyright 2026 The REBA Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_UI_VIEWS_REBA_ADBLOCK_REBA_ADBLOCK_BUBBLE_H_
#define CHROME_BROWSER_UI_VIEWS_REBA_ADBLOCK_REBA_ADBLOCK_BUBBLE_H_

#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "ui/base/metadata/metadata_header_macros.h"
#include "ui/views/bubble/bubble_dialog_delegate_view.h"

class Profile;

namespace content {
class WebContents;
}

namespace views {
class WebView;
}

namespace reba_adblock {

// The pop-up of the toolbar button: a web page (chrome://reba-adblock-popup) in
// a bubble under the button.
class RebaAdblockBubble : public views::BubbleDialogDelegateView {
  METADATA_HEADER(RebaAdblockBubble, views::BubbleDialogDelegateView)

 public:
  // Opens the pop-up for `source`, or closes it if it is already open under
  // `anchor`. A pop-up open in another window is closed and replaced.
  static void ShowOrClose(views::View* anchor,
                          Profile* profile,
                          content::WebContents* source);

  RebaAdblockBubble(const RebaAdblockBubble&) = delete;
  RebaAdblockBubble& operator=(const RebaAdblockBubble&) = delete;
  ~RebaAdblockBubble() override;

 private:
  RebaAdblockBubble(views::View* anchor,
                    Profile* profile,
                    content::WebContents* source);

  void CloseBubble();

  raw_ptr<views::WebView> web_view_ = nullptr;
  base::WeakPtrFactory<RebaAdblockBubble> weak_factory_{this};
};

}  // namespace reba_adblock

#endif  // CHROME_BROWSER_UI_VIEWS_REBA_ADBLOCK_REBA_ADBLOCK_BUBBLE_H_
