# REBA ad blocker

A uBlock Origin style ad blocker built into the browser. It has no extension
and no web store; everything runs in the browser itself.

## How it fits together

| Part | Where | What it does |
|---|---|---|
| Engine | `components/reba_adblock/engine` | [adblock-rust](https://github.com/brave/adblock-rust) (Brave's engine, MPL-2.0) behind a cxx bridge. Compiles filter lists, matches requests, returns cosmetic filters and scriptlets. |
| Core | `components/reba_adblock/core` | No browser dependencies: the list catalog (uBlock Origin's, plus our own "REBA filters" list first), `EngineSet` (compiled lists plus a small engine for the user's filters), the blocked request log, helpers for My filters. Unit tested. |
| Service | `chrome/browser/reba_adblock` | `RebaAdblockService` owns everything; `FilterListManager` downloads lists; `ResourcesManager` downloads the signed scriptlets file; `AdblockURLLoaderFactory` blocks subresources and workers; `RebaAdblockNavigationThrottle` handles pages and frames (and at startup holds top-level pages back, 3 s at most, until the first engine is loaded); `RebaAdblockURLLoaderThrottle` applies `$removeparam` to top-level navigations as an internal redirect, which keeps the method, body and initiator; `CosmeticFiltersImpl` answers the renderer (granted to prerendered pages, since the renderer calls it synchronously); WebSockets are blocked from `ChromeContentBrowserClient::CreateWebSocket`. |
| Renderer | `chrome/renderer/reba_adblock` and `chrome/renderer/resources/reba_adblock` | `CosmeticObserver` hides elements, runs scriptlets, and runs `cosmetic_filters.js` (procedural filters, generic hiding by class and id) and `picker.js` (element picker and zapper) in an isolated world. |
| UI | `chrome/browser/ui/webui/reba_adblock`, `chrome/browser/resources/settings/adblock_page`, `chrome/browser/ui/views/reba_adblock` | The settings section (chrome://settings/adblock), the blocked page (chrome://reba-blocked), the toolbar shield and its pop-up (chrome://reba-adblock-popup), and the page context menu items. |

"REBA filters" (`reba-filters`, on by default) is our own list for ad networks and sites the other lists miss. It is not built into the browser: it is downloaded like the others, from the release bucket (`adblock/reba-filters.txt`), so rules can be added without a release. The source and its publishing tool are in the reba-infra repository (`adblock/reba-filters.txt`, `tools/publish-filters.ts`). It is not trusted (no trusted scriptlets). A default list added after the first release is switched on once for people who had already changed their choice of lists (`kLaterDefaultLists`, pref `reba_adblock.later_default_lists_added`); people who never changed it get it through the pref's default value.

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

New windows that a page opens are checked in three places. `$popup` rules (the engine crate was patched to know them) are applied in `ChromeContentBrowserClient::CanCreateWindow()` through `ShouldBlockPopup()`, before the window exists. A tab that a page opened and whose first load is blocked is closed by the navigation throttle instead of showing the blocked page. For ad addresses no list knows, `popup_guard.js` runs in the page and refuses a window that a click opened without being on a link or control for it, or that a capture-phase listener on the window or document opened whatever was clicked ("click hijacking"); it is the setting "Stop pop-ups opened by clicking the page". A pop-up tab that its landing page redirects to a blocked address is closed too (`IsPopupLoad()` in the navigation throttle, using `blocked_content::PopupTracker`). Chrome also lets a page open a window on a click in one of its frames, which ad scripts use: Adcash draws a fake dialog in a transparent same-origin frame over the whole page and opens the ad from the page when that frame is clicked, and other scripts open an ad when the video player's frame takes the focus. So when the permission to open comes from a click (or key press) that this window did not see, the guard refuses cross-site windows, except those to the site of one of the page's frames (a widget asking the page to open its own site). A key press in the window itself always explains a window. A bubble-phase document listener is not treated as a hijack, because Solid, Svelte 5 and others handle all clicks there; an ad listening that way and clicked on a real button still opens.

## Protection beyond ad blocking (158.0.8127.2)

Everything is on by default, shows up in the protection log with the reason, and can be undone from there (or from the Sites tab).

- **Protection log.** `SecurityLog` (core) keeps pop-ups refused, downloads held, antivirus detections and Safe Browsing download verdicts for 30 days (500 at most), merged by kind, address and site, in `<User Data>\REBA Adblock\security_log.json` (written by `base::ImportantFileWriter` about 10 s after a change). Blocked ad requests stay in the in-memory `BlockedRequestLog`; the settings page shows both in one list ("Protection log" tab) with a kind filter. Off-the-record events are not logged. Clearing history clears the log for that time range (`ChromeBrowsingDataRemoverDelegate`).
- **Download guard** (`download_guard.cc`, pref `reba_adblock.download_guard`). `DownloadTargetDeterminer::DoCheckVisitedReferrerBefore()` asks `ShouldHoldDownload()`, which marks a program, installer, script, shortcut, archive or disk image `DOWNLOAD_DANGER_TYPE_DANGEROUS_FILE` (Keep / Delete in the downloads bubble) when it started without a click, was started by a frame of another site than the page, or started in a tab that a page of another site opened (`RebaAdblockTabHelper::opened_by()`, recorded in `DidOpenRequestedURL`). Typed URLs, "Save link as", the downloads page and extensions are never held. `DownloadProtectionObserver` (created by `ChromeDownloadManagerDelegate`) logs downloads that Windows' antivirus refused (Attachment Services: `FILE_VIRUS_INFECTED`/`FILE_BLOCKED`/`FILE_SECURITY_CHECK_FAILED`), Safe Browsing verdicts and Chromium's own "program from a site you haven't visited" warnings. Per-site off switch: `reba_adblock.download_guard_allowed_sites`.
- **Antivirus check** (`response_scanner.cc`, `antivirus_scanner_win.cc`, pref `reba_adblock.antivirus_check`). Subresource responses that are programs, installers or archives by type, name or `Content-Disposition: attachment` (and `application/octet-stream`-like ones whose first bytes are MZ, ZIP, RAR, 7z, OLE/MSI, CAB, PDF or a shortcut) are read in full in `AdblockURLLoaderFactory`, handed to AMSI (Windows' Antimalware Scan Interface, i.e. Microsoft Defender or the installed antivirus) on a COM thread, and given to the page in a new pipe only if clean. A detection ends the request with `ERR_BLOCKED_BY_CLIENT` and is logged; there is no override. Bodies over 64 MB with a Content-Length go through unchecked; without one they are blocked as "too large to check". The proxy runs for this even with ad blocking off. Navigations and downloads are not scanned here (the download path has Attachment Services).
- **Not in the cache.** `net/http/http_cache_transaction.cc` (`IsProgramOrArchive()` in `UpdateAndReportCacheability()`) never stores programs, installers and archives (by MIME type, or attachments of no particular type). A malicious one in `Cache_Data` makes the antivirus quarantine it and blame the browser for writing it (it did, on 2026-10-28: Defender flagged `chrome.exe` 158.0.8127.0 as `Trojan:Win32/Bearfoos.A!ml` 42 s after quarantining a `Suschil` cache file).
- **Pop-up guard per site.** `reba_adblock.popup_guard_allowed_sites`; `CosmeticFiltersImpl` leaves the guard out on those sites.
- **Quiet notification prompts.** `prefs::kEnableQuietNotificationPermissionUi` defaults to true (`quiet_notification_permission_ui_state.cc`): sites can't show the notification permission bubble, the address bar shows a bell instead.
- The toolbar shield's pop-up shows the page's pop-ups stopped, downloads held and threats blocked.
- Safe Browsing's warning pages are not in the log: `SafeBrowsingUIManager` has no observer to hear them from, and the page itself offers to continue.

## Best video quality (158.0.8127.3)

The setting "Play videos in the best quality" (pref `reba_adblock.best_video_quality`, **off** by default because a player left with only its best quality pauses to buffer on a slow connection instead of dropping lower). It is not ad blocking, so it applies whatever the main switch and the trusted sites say: `CosmeticFiltersImpl` sets `best_video_quality` for every web page, and `CosmeticObserver::InstallVideoQuality()` puts `video_quality.js` in the page's main world before its scripts, once per window, in every frame.

`video_quality.js` wraps `fetch()` and the `XMLHttpRequest` response getters (`responseText`, `response` for text and arraybuffer, `responseXML`) and rewrites manifests: DASH (`.mpd`, `application/dash+xml`, or text that starts like an MPD) keeps in every video adaptation set only the best representation `MediaSource.isTypeSupported()` accepts (height, then frame rate, then bit rate); audio, trick-mode and dependent sets are left alone. HLS master playlists (`.m3u8`, `mpegurl`) keep the best variant per audio group and video codec, drop audio-only variants and keep everything else. Keeping a best per adaptation set matters for DRM: sites put SD, HD and full HD in sets with their own keys and only license some of them to a given CDM, and the player falls back to the set it got keys for. Responses rewritten through `fetch()` keep their `url` (players resolve segment addresses against it). Not reached: players that fetch the manifest in a worker (Twitch), and sites without manifests (YouTube).

What it can't do: get more than the site licenses. REBA has Widevine L3 without Google's VMP signature, and discovery+ (WBD) refuses the 720p and 1080p keys to it (HTTP 400 from its license proxy), so there the best is 1024x576; without the setting the player often sits at 768x432 for minutes. The REBA filters list removes the 720p/1080p groups on discovery+, which saves the four refused license requests (about 7 s) at every start.

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
