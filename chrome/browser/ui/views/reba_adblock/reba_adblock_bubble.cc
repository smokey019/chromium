// Copyright 2026 The REBA Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/views/reba_adblock/reba_adblock_bubble.h"

#include <memory>
#include <utility>

#include "base/functional/bind.h"
#include "base/memory/ptr_util.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/webui/reba_adblock/adblock_popup_context.h"
#include "chrome/common/webui_url_constants.h"
#include "content/public/browser/web_contents.h"
#include "ui/base/metadata/metadata_impl_macros.h"
#include "ui/base/mojom/dialog_button.mojom.h"
#include "ui/gfx/geometry/insets.h"
#include "ui/gfx/geometry/size.h"
#include "ui/views/bubble/bubble_border.h"
#include "ui/views/controls/webview/webview.h"
#include "ui/views/layout/fill_layout.h"
#include "ui/views/widget/widget.h"

namespace reba_adblock {

namespace {

constexpr int kBubbleWidth = 340;
constexpr int kBubbleHeight = 520;

// The pop-up that is open, if any: only one at a time.
RebaAdblockBubble* g_bubble = nullptr;

}  // namespace

// static
void RebaAdblockBubble::ShowOrClose(views::View* anchor,
                                    Profile* profile,
                                    content::WebContents* source) {
  if (g_bubble) {
    // A press on the shield already took the focus from the pop-up, which then
    // closes on its own, but later; the button acts on press so that the
    // pop-up is still found here and not opened again.
    const bool same_button = g_bubble->GetAnchorView() == anchor;
    if (views::Widget* widget = g_bubble->GetWidget()) {
      widget->Close();
    }
    if (same_button) {
      return;
    }
    // The pop-up was another window's; open this window's.
  }
  views::BubbleDialogDelegateView::CreateBubble(
      base::WrapUnique(new RebaAdblockBubble(anchor, profile, source)))
      ->Show();
}

RebaAdblockBubble::RebaAdblockBubble(views::View* anchor,
                                     Profile* profile,
                                     content::WebContents* source)
    : views::BubbleDialogDelegateView(views::BubbleAnchor(anchor),
                                      views::BubbleBorder::TOP_RIGHT) {
  g_bubble = this;
  SetButtons(static_cast<int>(ui::mojom::DialogButton::kNone));
  set_margins(gfx::Insets());
  SetLayoutManager(std::make_unique<views::FillLayout>());

  web_view_ = AddChildView(std::make_unique<views::WebView>(profile));
  web_view_->SetPreferredSize(gfx::Size(kBubbleWidth, kBubbleHeight));
  // Esc closes the pop-up, as it does other bubbles. The page itself has no
  // use for the key.
  web_view_->SetOnEscCloseWidget();
  AdblockPopupContext::CreateForWebContents(web_view_->GetWebContents(),
                                            source);
  AdblockPopupContext::FromWebContents(web_view_->GetWebContents())
      ->SetCloseCallback(base::BindOnce(&RebaAdblockBubble::CloseBubble,
                                        weak_factory_.GetWeakPtr()));
  web_view_->LoadInitialURL(GURL(chrome::kChromeUIRebaAdblockPopupURL));
}

RebaAdblockBubble::~RebaAdblockBubble() {
  if (g_bubble == this) {
    g_bubble = nullptr;
  }
}

void RebaAdblockBubble::CloseBubble() {
  if (GetWidget()) {
    GetWidget()->Close();
  }
}

BEGIN_METADATA(RebaAdblockBubble)
END_METADATA

}  // namespace reba_adblock
