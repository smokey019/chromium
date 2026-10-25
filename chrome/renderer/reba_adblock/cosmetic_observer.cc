// Copyright 2026 The REBA Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/renderer/reba_adblock/cosmetic_observer.h"

#include <optional>
#include <utility>

#include "base/functional/bind.h"
#include "base/json/json_reader.h"
#include "base/json/json_writer.h"
#include "base/location.h"
#include "base/strings/strcat.h"
#include "base/strings/string_util.h"
#include "base/task/single_thread_task_runner.h"
#include "base/unguessable_token.h"
#include "base/values.h"
#include "chrome/common/chrome_isolated_world_ids.h"
#include "chrome/grit/renderer_resources.h"
#include "content/public/renderer/render_frame.h"
#include "third_party/blink/public/common/associated_interfaces/associated_interface_registry.h"
#include "third_party/blink/public/platform/browser_interface_broker_proxy.h"
#include "third_party/blink/public/platform/scheduler/web_agent_group_scheduler.h"
#include "third_party/blink/public/platform/task_type.h"
#include "third_party/blink/public/platform/web_isolated_world_info.h"
#include "third_party/blink/public/platform/web_security_origin.h"
#include "third_party/blink/public/web/web_document.h"
#include "third_party/blink/public/web/web_frame.h"
#include "third_party/blink/public/web/web_local_frame.h"
#include "third_party/blink/public/web/web_script_source.h"
#include "ui/base/resource/resource_bundle.h"
#include "url/gurl.h"
#include "url/origin.h"
#include "v8/include/v8-context.h"
#include "v8/include/v8-function.h"
#include "v8/include/v8-isolate.h"
#include "v8/include/v8-local-handle.h"
#include "v8/include/v8-primitive.h"

namespace reba_adblock {

namespace {

constexpr char kBridgeName[] = "__rebaBridge";

// The isolated world of the ad blocker's scripts.
constexpr int kWorldId = ISOLATED_WORLD_ID_REBA_ADBLOCK;

// How far up FilterUrl() looks for the page that made a frame. Openers can
// form cycles.
constexpr int kMaxCreatorDepth = 8;

std::string ToStdString(v8::Isolate* isolate, v8::Local<v8::Value> value) {
  if (value.IsEmpty() || !value->IsString()) {
    return std::string();
  }
  v8::String::Utf8Value utf8(isolate, value);
  return *utf8 ? std::string(*utf8, utf8.length()) : std::string();
}

bool IsFilterableUrl(const blink::WebURL& url) {
  const GURL gurl(url);
  return gurl.SchemeIsHTTPOrHTTPS();
}

// The address whose cosmetic filters apply to the document of `frame`, or an
// empty URL if none do. Frames at about:blank or about:srcdoc that a page of
// the same origin made (ad scripts like to write their ads into such frames)
// get the filters of that page, as in uBlock Origin.
GURL FilterUrl(blink::WebLocalFrame* frame) {
  GURL url(frame->GetDocument().Url());
  for (int depth = 0;
       depth < kMaxCreatorDepth && (url.IsAboutBlank() || url.IsAboutSrcdoc());
       ++depth) {
    blink::WebFrame* creator =
        frame->Parent() ? frame->Parent() : frame->Opener();
    if (!creator || !creator->IsWebLocalFrame()) {
      return GURL();
    }
    blink::WebLocalFrame* local_creator = creator->ToWebLocalFrame();
    if (!local_creator->GetSecurityOrigin().IsSameOriginWith(
            frame->GetSecurityOrigin())) {
      return GURL();
    }
    frame = local_creator;
    url = GURL(frame->GetDocument().Url());
  }
  return url.SchemeIsHTTPOrHTTPS() ? url : GURL();
}

// Gives the isolated world a content security policy of its own, so the
// page's policy doesn't apply to what the ad blocker's scripts do (the element
// picker's styles, for one) and the site gets no reports about them. The
// setting is for the whole process. Modelled on IndigoAgent::InjectScript()
// (chrome/renderer/indigo/indigo_agent.cc).
void SetUpIsolatedWorld() {
  static bool done = false;
  if (done) {
    return;
  }
  done = true;
  blink::WebIsolatedWorldInfo info;
  info.human_readable_name = blink::WebString::FromUtf8("REBA ad blocker");
  // A policy needs an origin. The world makes no requests, so an opaque one.
  info.security_origin = blink::WebSecurityOrigin(url::Origin());
  info.content_security_policy =
      blink::WebString::FromUtf8("script-src 'none'; object-src 'none'");
  blink::SetIsolatedWorldInfo(kWorldId, info);
}

}  // namespace

CosmeticObserver::CosmeticObserver(content::RenderFrame* render_frame)
    : content::RenderFrameObserver(render_frame),
      content::RenderFrameObserverTracker<CosmeticObserver>(render_frame) {
  render_frame->GetAssociatedInterfaceRegistry()
      ->AddInterface<mojom::PickerAgent>(base::BindRepeating(
          &CosmeticObserver::BindPickerAgent, base::Unretained(this)));
}

CosmeticObserver::~CosmeticObserver() = default;

void CosmeticObserver::OnDestruct() {
  delete this;
}

void CosmeticObserver::BindPickerAgent(
    mojo::PendingAssociatedReceiver<mojom::PickerAgent> receiver) {
  picker_receiver_.reset();
  // On the frame's task runner, so the picker doesn't start in a frozen page
  // (one in the back/forward cache, say).
  picker_receiver_.Bind(
      std::move(receiver),
      render_frame()->GetTaskRunner(blink::TaskType::kInternalDefault));
}

mojom::CosmeticFilters* CosmeticObserver::filters() {
  if (!filters_.is_bound()) {
    // Replies come on the frame's task runner, so none arrives while the page
    // is frozen.
    render_frame()->GetBrowserInterfaceBroker().GetInterface(
        filters_.BindNewPipeAndPassReceiver(
            render_frame()->GetTaskRunner(blink::TaskType::kInternalDefault)));
  }
  return filters_.get();
}

void CosmeticObserver::DidCreateNewDocument() {
  // The browser side serves one document per connection, and replies for the
  // previous document must not reach this one.
  filters_.reset();
  document_element_seen_ = false;
  pending_resources_.reset();
  init_json_.clear();
  generic_exceptions_.clear();
  generic_css_.clear();
  generic_sheet_key_.Reset();
  preview_sheet_key_.Reset();
}

void CosmeticObserver::DidCreateDocumentElement() {
  // After document.open() the parser makes a new document element in the same
  // document. What was done for the first one is still in place.
  if (document_element_seen_) {
    return;
  }
  document_element_seen_ = true;
  const GURL url = FilterUrl(render_frame()->GetWebFrame());
  if (url.is_empty()) {
    return;
  }
  // This has to be answered before the page runs its first script, hence the
  // synchronous call.
  mojom::CosmeticResourcesPtr resources;
  if (!filters()->GetCosmeticResources(url, &resources) || !resources) {
    return;
  }
  ApplyStyles(*resources);
  pending_resources_ = std::move(resources);
}

void CosmeticObserver::ApplyStyles(const mojom::CosmeticResources& resources) {
  if (!resources.enabled || resources.hide_selectors.empty()) {
    return;
  }
  // Elements to hide: a user style sheet with one rule per selector, so a
  // selector this browser can't parse only loses itself.
  std::string css;
  for (const std::string& selector : resources.hide_selectors) {
    if (selector.find_first_of("{}") != std::string::npos) {
      continue;
    }
    base::StrAppend(&css, {selector, "{display:none!important}\n"});
  }
  InsertCss(css);
}

bool CosmeticObserver::RunScriptsAtDocumentStart() {
  mojom::CosmeticResourcesPtr resources = std::move(pending_resources_);
  if (!resources || (!resources->enabled && !resources->popup_guard)) {
    return true;
  }
  // The page's scripts can run too while ours do (microtasks, custom element
  // callbacks) and remove the frame, which deletes this observer.
  base::WeakPtr<CosmeticObserver> weak_this = weak_factory_.GetWeakPtr();

  // The pop-up guard goes in before anything else, so no script of the page
  // can get hold of the unguarded window.open().
  if (resources->popup_guard && !InstallPopupGuard()) {
    return false;
  }
  if (!resources->enabled) {
    return true;
  }

  // Scriptlets, in the page's own world, before anything of the page runs.
  if (!resources->injected_script.empty() && ClaimWindow(scriptlet_context_)) {
    const std::string wrapped =
        base::StrCat({"(function(){'use strict';try{\n",
                      resources->injected_script, "\n}catch(e){}})();"});
    render_frame()->GetWebFrame()->ExecuteScript(
        blink::WebScriptSource(blink::WebString::FromUtf8(wrapped)));
    if (!weak_this) {
      return false;
    }
  }

  // The rest needs the interpreter in an isolated world.
  const bool needs_script =
      !resources->procedural_actions.empty() || !resources->generichide;
  if (!needs_script) {
    return true;
  }
  // Kept here for the generic hiding requests of this document.
  generic_exceptions_ = std::move(resources->generic_exceptions);
  base::DictValue init;
  base::ListValue procedural;
  for (std::string& action : resources->procedural_actions) {
    procedural.Append(std::move(action));
  }
  init.Set("procedural", std::move(procedural));
  init.Set("generichide", resources->generichide);
  init_json_ = base::WriteJson(init).value_or("{}");

  RunInIsolatedWorld(
      ui::ResourceBundle::GetSharedInstance().LoadDataResourceString(
          IDR_REBA_ADBLOCK_COSMETIC_JS));
  return !!weak_this;
}

bool CosmeticObserver::InstallPopupGuard() {
  // The guard is in the window already, with its own event name (a frame's
  // first document hands its window to the next one, see ClaimWindow()).
  if (!ClaimWindow(guard_context_)) {
    return true;
  }
  base::WeakPtr<CosmeticObserver> weak_this = weak_factory_.GetWeakPtr();
  guard_event_ = base::StrCat(
      {"r", base::UnguessableToken::Create().ToString()});

  // The isolated world listens for the guard's reports and passes them on.
  RunInIsolatedWorld(base::StrCat(
      {"(function(){const n='", guard_event_,
       "';window.addEventListener(n,function(){const r=document.documentElement;",
       "const v=r&&r.getAttribute(n);if(r){r.removeAttribute(n);}",
       "if(v){__rebaBridge('popup',v);}},true);})();"}));
  if (!weak_this) {
    return false;
  }

  std::string script =
      ui::ResourceBundle::GetSharedInstance().LoadDataResourceString(
          IDR_REBA_ADBLOCK_POPUP_GUARD_JS);
  base::ReplaceSubstringsAfterOffset(&script, 0, "__REBA_EVENT__",
                                     guard_event_);
  render_frame()->GetWebFrame()->ExecuteScript(
      blink::WebScriptSource(blink::WebString::FromUtf8(script)));
  return !!weak_this;
}

// Things that are done once per window ask here first. A frame's first
// document hands its window, with whatever the scripts changed in it, to the
// next document if that is from the same origin; same origin means same host,
// hence the same scriptlets and the same guard.
bool CosmeticObserver::ClaimWindow(v8::Global<v8::Context>& slot) {
  blink::WebLocalFrame* frame = render_frame()->GetWebFrame();
  v8::Isolate* isolate = frame->GetAgentGroupScheduler()->Isolate();
  v8::HandleScope handle_scope(isolate);
  v8::Local<v8::Context> context = frame->MainWorldScriptContext();
  if (slot == context) {
    return false;
  }
  slot.Reset(isolate, context);
  // Weak, so it doesn't keep the window alive.
  slot.SetWeak();
  return true;
}

void CosmeticObserver::InsertCss(const std::string& css) {
  if (css.empty()) {
    return;
  }
  render_frame()->GetWebFrame()->GetDocument().InsertStyleSheet(
      blink::WebString::FromUtf8(css), /*key=*/nullptr,
      blink::WebCssOrigin::kUser);
}

void CosmeticObserver::RunInIsolatedWorld(const std::string& source) {
  SetUpIsolatedWorld();
  render_frame()->GetWebFrame()->ExecuteScriptInIsolatedWorld(
      kWorldId, blink::WebScriptSource(blink::WebString::FromUtf8(source)),
      blink::BackForwardCacheAware::kAllow);
}

void CosmeticObserver::StartPicker(bool zapper) {
  if (!IsFilterableUrl(render_frame()->GetWebFrame()->GetDocument().Url())) {
    return;
  }
  base::WeakPtr<CosmeticObserver> weak_this = weak_factory_.GetWeakPtr();
  RunInIsolatedWorld(
      ui::ResourceBundle::GetSharedInstance().LoadDataResourceString(
          IDR_REBA_ADBLOCK_PICKER_JS));
  // Running script can end with the frame gone.
  if (!weak_this) {
    return;
  }
  RunInIsolatedWorld(base::StrCat(
      {"window.__rebaStartPicker(", zapper ? "true" : "false", ");"}));
}

// --- The native bridge for the scripts of the isolated world ---

void CosmeticObserver::DidCreateScriptContext(v8::Local<v8::Context> context,
                                              int32_t world_id) {
  if (world_id != kWorldId) {
    return;
  }
  v8::Isolate* isolate =
      render_frame()->GetWebFrame()->GetAgentGroupScheduler()->Isolate();
  v8::Context::Scope context_scope(context);
  v8::Local<v8::Function> function;
  if (!v8::Function::New(context, &CosmeticObserver::BridgeCallback)
           .ToLocal(&function)) {
    return;
  }
  context->Global()
      ->Set(context,
            v8::String::NewFromUtf8(isolate, kBridgeName).ToLocalChecked(),
            function)
      .Check();
}

// static
void CosmeticObserver::BridgeCallback(
    const v8::FunctionCallbackInfo<v8::Value>& info) {
  // The frame comes from the context the function belongs to, not from a
  // pointer bound to the function, which could outlive the frame.
  blink::WebLocalFrame* frame = blink::WebLocalFrame::FrameForCurrentContext();
  content::RenderFrame* render_frame =
      frame ? content::RenderFrame::FromWebFrame(frame) : nullptr;
  if (CosmeticObserver* observer = render_frame ? Get(render_frame) : nullptr) {
    observer->HandleBridgeCall(info);
  }
}

void CosmeticObserver::HandleBridgeCall(
    const v8::FunctionCallbackInfo<v8::Value>& info) {
  v8::Isolate* isolate = info.GetIsolate();
  if (info.Length() < 1) {
    return;
  }
  const std::string op = ToStdString(isolate, info[0]);
  const std::string arg =
      info.Length() > 1 ? ToStdString(isolate, info[1]) : std::string();

  if (op == "init") {
    info.GetReturnValue().Set(
        v8::String::NewFromUtf8(isolate, init_json_.c_str(),
                                v8::NewStringType::kNormal,
                                static_cast<int>(init_json_.size()))
            .ToLocalChecked());
  } else if (op == "css") {
    InsertCss(arg);
  } else if (op == "preview") {
    SetPreviewCss(arg);
  } else if (op == "hidden") {
    double count = 0;
    if (info.Length() > 1 && info[1]->IsNumber()) {
      count = info[1].As<v8::Number>()->Value();
    }
    if (count > 0) {
      filters()->ReportHiddenElements(static_cast<uint32_t>(count));
    }
  } else if (op == "addFilter") {
    filters()->AddCosmeticFilter(arg);
  } else if (op == "popup") {
    // From the guard in the page, through the isolated world's listener.
    std::optional<base::DictValue> parsed = base::JSONReader::ReadDict(
        arg, base::JSON_PARSE_RFC | base::JSON_REPLACE_INVALID_CHARACTERS);
    const std::string* text = parsed ? parsed->FindString("url") : nullptr;
    GURL url = text ? GURL(*text) : GURL();
    if (!url.is_valid() || !url.SchemeIsHTTPOrHTTPS()) {
      url = GURL("about:blank");
    }
    filters()->ReportBlockedPopup(url);
  } else if (op == "generic") {
    // Lone surrogates in names arrive as \uD800 escapes, which the strict
    // parser rejects for the whole request.
    std::optional<base::DictValue> parsed = base::JSONReader::ReadDict(
        arg, base::JSON_PARSE_RFC | base::JSON_REPLACE_INVALID_CHARACTERS);
    if (!parsed) {
      info.GetReturnValue().Set(false);
      return;
    }
    auto to_vector = [](const base::ListValue* list) {
      std::vector<std::string> out;
      if (list) {
        for (const base::Value& v : *list) {
          if (v.is_string()) {
            out.push_back(v.GetString());
          }
        }
      }
      return out;
    };
    filters()->HiddenClassIdSelectors(
        to_vector(parsed->FindList("classes")),
        to_vector(parsed->FindList("ids")), generic_exceptions_,
        base::BindOnce(&CosmeticObserver::OnGenericSelectors,
                       weak_factory_.GetWeakPtr()));
    info.GetReturnValue().Set(true);
  }
}

// The rules are inserted here rather than by the isolated world's script,
// which would mean running script: that doesn't happen where the site's
// JavaScript is blocked.
void CosmeticObserver::OnGenericSelectors(
    const std::vector<std::string>& selectors) {
  bool added = false;
  for (const std::string& selector : selectors) {
    if (selector.find_first_of("{}") != std::string::npos) {
      continue;
    }
    base::StrAppend(&generic_css_, {selector, "{display:none!important}\n"});
    added = true;
  }
  if (!added || generic_flush_pending_) {
    return;
  }
  // Replies that are already queued go in together.
  generic_flush_pending_ = true;
  render_frame()
      ->GetTaskRunner(blink::TaskType::kInternalDefault)
      ->PostTask(FROM_HERE, base::BindOnce(&CosmeticObserver::FlushGenericCss,
                                           weak_factory_.GetWeakPtr()));
}

void CosmeticObserver::FlushGenericCss() {
  generic_flush_pending_ = false;
  if (generic_css_.empty()) {
    return;
  }
  // One sheet replaced as it grows, not one more sheet per reply: with
  // hundreds of sheets every style recalculation gets slow.
  blink::WebDocument document = render_frame()->GetWebFrame()->GetDocument();
  if (!generic_sheet_key_.IsNull()) {
    document.RemoveInsertedStyleSheet(generic_sheet_key_,
                                      blink::WebCssOrigin::kUser);
  }
  generic_sheet_key_ =
      document.InsertStyleSheet(blink::WebString::FromUtf8(generic_css_),
                                /*key=*/nullptr, blink::WebCssOrigin::kUser);
}

void CosmeticObserver::SetPreviewCss(const std::string& css) {
  blink::WebDocument document = render_frame()->GetWebFrame()->GetDocument();
  if (!preview_sheet_key_.IsNull()) {
    document.RemoveInsertedStyleSheet(preview_sheet_key_,
                                      blink::WebCssOrigin::kUser);
    preview_sheet_key_.Reset();
  }
  if (!css.empty()) {
    preview_sheet_key_ =
        document.InsertStyleSheet(blink::WebString::FromUtf8(css),
                                  /*key=*/nullptr, blink::WebCssOrigin::kUser);
  }
}

}  // namespace reba_adblock
