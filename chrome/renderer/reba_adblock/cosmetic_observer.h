// Copyright 2026 The REBA Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_RENDERER_REBA_ADBLOCK_COSMETIC_OBSERVER_H_
#define CHROME_RENDERER_REBA_ADBLOCK_COSMETIC_OBSERVER_H_

#include <string>
#include <vector>

#include "base/memory/weak_ptr.h"
#include "components/reba_adblock/common/reba_adblock.mojom.h"
#include "content/public/renderer/render_frame_observer.h"
#include "content/public/renderer/render_frame_observer_tracker.h"
#include "mojo/public/cpp/bindings/associated_receiver.h"
#include "mojo/public/cpp/bindings/pending_associated_receiver.h"
#include "mojo/public/cpp/bindings/remote.h"
#include "third_party/blink/public/platform/web_string.h"
#include "v8/include/v8-forward.h"
#include "v8/include/v8-persistent-handle.h"

namespace reba_adblock {

// Applies the ad blocker's cosmetic filters to the documents of one frame:
// hides elements, runs scriptlets, and runs the interpreter for procedural
// filters and generic hiding in an isolated world. Also hosts the element
// picker.
class CosmeticObserver
    : public content::RenderFrameObserver,
      public content::RenderFrameObserverTracker<CosmeticObserver>,
      public mojom::PickerAgent {
 public:
  explicit CosmeticObserver(content::RenderFrame* render_frame);
  CosmeticObserver(const CosmeticObserver&) = delete;
  CosmeticObserver& operator=(const CosmeticObserver&) = delete;
  ~CosmeticObserver() override;

  // Runs the scripts of the document that just got its document element.
  // Called by the content client; scripts can't run from
  // DidCreateDocumentElement(), which Blink calls with scripts forbidden.
  // Returns false if the frame, and this observer with it, is gone afterwards:
  // the page's own scripts can run in between and remove the frame.
  bool RunScriptsAtDocumentStart();

  // content::RenderFrameObserver:
  void DidCreateNewDocument() override;
  void DidCreateDocumentElement() override;
  void DidCreateScriptContext(v8::Local<v8::Context> context,
                              int32_t world_id) override;
  void OnDestruct() override;

  // mojom::PickerAgent:
  void StartPicker(bool zapper) override;

 private:
  mojom::CosmeticFilters* filters();
  void BindPickerAgent(
      mojo::PendingAssociatedReceiver<mojom::PickerAgent> receiver);
  void ApplyStyles(const mojom::CosmeticResources& resources);
  void InsertCss(const std::string& css);
  void RunInIsolatedWorld(const std::string& source);
  // Whether `slot` does not hold the current main world yet; if so it does
  // afterwards. Things that are done once per window ask this first.
  bool ClaimWindow(v8::Global<v8::Context>& slot);
  // Puts the pop-up guard in the page. Returns false if the frame is gone.
  bool InstallPopupGuard();

  // The native function the isolated world's scripts call, see
  // cosmetic_filters.js.
  static void BridgeCallback(const v8::FunctionCallbackInfo<v8::Value>& info);
  void HandleBridgeCall(const v8::FunctionCallbackInfo<v8::Value>& info);
  void OnGenericSelectors(const std::vector<std::string>& selectors);
  void FlushGenericCss();
  void SetPreviewCss(const std::string& css);

  // Bound per document, like the browser side.
  mojo::Remote<mojom::CosmeticFilters> filters_;
  mojo::AssociatedReceiver<mojom::PickerAgent> picker_receiver_{this};

  // Whether the current document got its document element. document.open()
  // makes the parser create another one in the same document.
  bool document_element_seen_ = false;

  // What the browser answered for the document being created, until its
  // scripts have run.
  mojom::CosmeticResourcesPtr pending_resources_;

  // What `__rebaBridge('init')` returns to the script of the current document.
  std::string init_json_;
  // Class and id selectors that generic hide rules must not hide on the
  // current document.
  std::vector<std::string> generic_exceptions_;

  // The generic hide rules of the current document. They are kept in one
  // style sheet, replaced as it grows.
  std::string generic_css_;
  blink::WebString generic_sheet_key_;
  bool generic_flush_pending_ = false;

  // The element picker's preview.
  blink::WebString preview_sheet_key_;

  // The main worlds the scriptlets and the pop-up guard last ran in. Weak.
  v8::Global<v8::Context> scriptlet_context_;
  v8::Global<v8::Context> guard_context_;
  // Names the event and attribute with which the guard in the page tells the
  // isolated world about a refused pop-up. Random, so the page can't forge
  // reports. Set with `guard_context_`.
  std::string guard_event_;

  base::WeakPtrFactory<CosmeticObserver> weak_factory_{this};
};

}  // namespace reba_adblock

#endif  // CHROME_RENDERER_REBA_ADBLOCK_COSMETIC_OBSERVER_H_
