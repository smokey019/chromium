# REBA ad blocker

A uBlock Origin style ad blocker built into the browser. It has no extension
and no web store; everything runs in the browser itself.

## How it fits together

| Part | Where | What it does |
|---|---|---|
| Engine | `components/reba_adblock/engine` | [adblock-rust](https://github.com/brave/adblock-rust) (Brave's engine, MPL-2.0) behind a cxx bridge. Compiles filter lists, matches requests, returns cosmetic filters and scriptlets. |
| Core | `components/reba_adblock/core` | No browser dependencies: the list catalog (uBlock Origin's), `EngineSet` (compiled lists plus a small engine for the user's filters), the blocked request log, helpers for My filters. Unit tested. |
| Service | `chrome/browser/reba_adblock` | `RebaAdblockService` owns everything; `FilterListManager` downloads lists; `ResourcesManager` downloads the signed scriptlets file; `AdblockURLLoaderFactory` blocks subresources and workers; `RebaAdblockNavigationThrottle` handles pages and frames (and at startup holds top-level pages back, 3 s at most, until the first engine is loaded); `RebaAdblockURLLoaderThrottle` applies `$removeparam` to top-level navigations as an internal redirect, which keeps the method, body and initiator; `CosmeticFiltersImpl` answers the renderer (granted to prerendered pages, since the renderer calls it synchronously); WebSockets are blocked from `ChromeContentBrowserClient::CreateWebSocket`. |
| Renderer | `chrome/renderer/reba_adblock` and `chrome/renderer/resources/reba_adblock` | `CosmeticObserver` hides elements, runs scriptlets, and runs `cosmetic_filters.js` (procedural filters, generic hiding by class and id) and `picker.js` (element picker and zapper) in an isolated world. |
| UI | `chrome/browser/ui/webui/reba_adblock`, `chrome/browser/resources/settings/adblock_page`, `chrome/browser/ui/views/reba_adblock` | The settings section (chrome://settings/adblock), the blocked page (chrome://reba-blocked), the toolbar shield and its pop-up (chrome://reba-adblock-popup), and the page context menu items. |

Settings are browser-wide (Local State, `reba_adblock.*`), not per profile.
Lists, the compiled engine and the scriptlets live in
`<User Data>\REBA Adblock` (the folder is named by `RebaAdblockMainParts`).

## Two engines

Compiling every list takes a moment, so the lists are compiled once into a big
engine that is cached on disk (`engine\engine-<hash>.bin`). My filters, the
sites allowed for the session and the rules from the blocked requests log go in
a second, tiny engine that is rebuilt in milliseconds. `EngineSet::CheckRequest`
asks both and merges the answers: exceptions from either win over blocks from
either, `$important` wins over exceptions, except that the user's own
exceptions also win over the lists' `$important` filters. A `$redirect-rule`
stand-in is only used for a request that something blocked.

Some of the user's lines act on the lists' own filters (`$badfilter`, `@@` with
`$generichide`, `$redirect`, `$redirect-rule` or `$csp`, and `site#@#...`
exceptions such as `site#@#+js()`). adblock-rust only applies those within one
engine, so `UserListExceptions()` picks them out and they are compiled into the
lists engine as well, as its last list; changing them means compiling the lists
again. `#@#selector` and `#@#+js()` without a site, which adblock-rust rejects,
are applied by `EngineSet` to what the engines return.

The cache key includes the browser version, so an update never loads an engine
compiled by an older adblock-rust. A list whose file can't be read is left out
and the result isn't cached; `BuildEngineSet` reports it so it gets downloaded
again.

## Scriptlets and redirect resources

uBlock Origin keeps its scriptlets (`+js(...)`) and stand-in resources (`$redirect=`)
as JavaScript modules, which adblock-rust can't read. `tools/build_resources.mjs`
downloads uBlock Origin's sources, runs them with Node, and writes
`resources.json` in the format the engine wants. It is GPL-3.0 content, so it is
published on the update server and downloaded at run time rather than shipped
in the browser. The browser only uses it if the detached signature (ECDSA
P-256, SHA-256, DER, base64) checks out against the public key in
`resources_manager.cc`.

```
node components/reba_adblock/tools/build_resources.mjs --out resources.json
node components/reba_adblock/tools/sign_resources.mjs resources.json <private key>
```

To publish, `bun tools/publish-adblock-resources.ts resources.json` in `E:\reba-infra` signs and uploads
both files to `adblock/` in the release bucket (it replaces the `sign_resources.mjs` step).

For development, `--reba-adblock-resources-file=<path>` uses a file as it is.

## Pop-ups

New windows that a page opens are checked in three places. `$popup` rules (the engine crate was patched to know them) are applied in `ChromeContentBrowserClient::CanCreateWindow()` through `ShouldBlockPopup()`, before the window exists. A tab that a page opened and whose first load is blocked is closed by the navigation throttle instead of showing the blocked page. For ad addresses no list knows, `popup_guard.js` runs in the page and refuses a window that a click opened without being on a link or control for it, or that a capture-phase listener on the window or document opened whatever was clicked ("click hijacking"); it is the setting "Stop pop-ups opened by clicking the page". A pop-up tab that its landing page redirects to a blocked address is closed too (`IsPopupLoad()` in the navigation throttle, using `blocked_content::PopupTracker`). A bubble-phase document listener is not treated as a hijack, because Solid, Svelte 5 and others handle all clicks there; an ad listening that way and clicked on a real button still opens.

## Not done

- `$csp` filters (they need the response headers of navigations).
- `$removeparam` on subresources (it works on top-level navigations).
- uBlock Origin's dynamic filtering ("My rules") and the per-site switches for
  pop-ups, large media, remote fonts and JavaScript.
- WebTransport.
- `$elemhide` and `$specifichide` (adblock-rust doesn't parse them), and
  `#@#` exceptions without a site other than plain selectors and `+js()`
  (`#@#+js(name, ...)`, procedural ones).

## Tests

```
autoninja -C out\Default components/reba_adblock:reba_adblock_unittests
out\Default\reba_adblock_unittests.exe
```

Two tests need real data and are skipped without it:
`REBA_ADBLOCK_TEST_RESOURCES` (the `resources.json`) and
`REBA_ADBLOCK_TEST_LISTS_DIR` (a folder with the filter lists as `.txt` files).
