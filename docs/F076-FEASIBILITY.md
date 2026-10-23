# F076 feasibility: clipboard bridge with explicit paste and shortcut forwarding

EN-09 (track session S4, Phase 0 cycle 1). Base `afeac1a`. Written 2026-10-22. A proposal for the lead: nothing here is a support claim or delivery evidence.

**Recommendation: GO for a policy-and-documentation slice (F076a): explicit, text-only clipboard through the noVNC panel with Xvnc limits set, PRIMARY selection off and an in-app path for every browser-reserved shortcut. DEFER per-client clipboard routing and any browser system-clipboard integration (F076b).** Size: F076a M, F076b M on the shared RFB policy proxy. Depends on: Track F (`docker/start.py`, `docker/`), a hot-file grant only if an in-app shortcut affordance needs `ui/Main.qml`, F078 view-only mode for receive-only clients.

## 1. What the feature needs in this codebase

Inventory row: "F076 | Clipboard bridge with explicit paste and shortcut forwarding | C / P1 / M | noVNC/browser constraints; text size limits, permission failures, focus release and no automatic bidirectional clipboard harvesting." No REQUIREMENTS.md row covers the remote clipboard.

What the app puts on the X clipboard today (all through `QGuiApplication::clipboard()`, X11 CLIPBOARD selection under xcb):

| Source | Content | Crosses VNC? |
| --- | --- | --- |
| `ViewerController::copyDirectory` (`src/app/viewer_controller.cpp`) | directory path text | Yes, as text |
| copy paths (same file, `setText(joinedText)`) | newline-joined paths of the selection | Yes, as text; size grows with selection count |
| copy files (`QMimeData` with `setUrls` + `setText`) | `text/uri-list` plus text | Only the text part |
| copy image (`clipboard->setImage(d->image)`) | bitmap | **No.** Xvnc converts only `STRING`/`TEXT`/`UTF8_STRING` targets [9] |
| `MetadataModel::copySummary/copyGps/copyTag` (`src/app/metadata_model.cpp`) | metadata text | Yes |
| `copy_move_service` internal cut/copy/paste | in-process file list | No (not the system clipboard) |

Paths are server paths (`/photos/...`); F075 owns telling the user that. The image copy must be presented as unavailable in the browser desktop (status text), not as silently failing.

Touch points: `docker/start.py` (Xvnc flags), docs/DOCKER.md, a landing page under `docker/` that explains the clipboard panel, and possibly one status string in the app when `QT_QPA_PLATFORM=xcb` and a Docker marker env var are set (a library class under `src/app/` with a thin call site; lead resolves the include). Owner: Track F.

## 2. Candidate dependencies

| Candidate | Licence | GPL-3.0-or-later compatible? | Maintenance | Windows bundling | Docker bundling |
| --- | --- | --- | --- | --- | --- |
| Xvnc built-in clipboard (`tigervnc-standalone-server` 1.13.1+dfsg-2build2) | GPL-2.0-or-later [16] | Yes (separate program) | noble release pocket 2024-04 [1]; upstream 1.16.2 [3] | Not applicable | Already installed |
| noVNC 1:1.3.0-2 clipboard panel | MPL-2.0 / BSD-2 [17] | Yes | noble 2023-10 [2]; upstream 1.7.0 [4] | Not applicable | Already installed |
| Newer noVNC with Async Clipboard (master after 1.7.0 only) | MPL-2.0 | Yes | `core/clipboard.js` exists on master, absent at v1.6.0 and v1.7.0 tags (checked) [8] | Not applicable | Would need vendoring; **conflicts with "no automatic harvesting"**, see section 4 |
| `autocutsel` / `vncconfig -nowin` style selection bridges | Various | n/a | Not needed: Xvnc 1.13 already owns CLIPBOARD and PRIMARY [5] | Not applicable | Not needed |
| Qt only (app-side status messages) | LGPL-3 (dynamic) | Yes | Qt 6.8.3 | n/a | n/a |

No new dependency is required for F076a.

## 3. Platform capability

**Windows / native Linux.** Not applicable: the native app uses the OS clipboard directly; image copy works there. Unaffected.

**Docker/noVNC (mandatory).**

Protocol and server (TigerVNC 1.13.1 in noble):

- Classic RFB ServerCutText/ClientCutText carries Latin-1. The Extended Clipboard pseudo-encoding carries UTF-8 with explicit notify/request/provide actions. TigerVNC added full Unicode clipboard in 1.10.0 [10]; noVNC 1.3.0 implements Extended Clipboard (`extendedClipboardFormatText`, caps/request/peek/notify/provide in `core/rfb.js`) and falls back to Latin-1 ClientCutText with a "FIXME: text can have values outside of Latin1" when the server lacks it [7]. With the packaged pair, UTF-8 text should round-trip (unverified until the test in section 5 runs).
- Xvnc parameters in 1.13.1 [5][6]: `-AcceptCutText` (default on), `-SendCutText` (default on), `-MaxCutText bytes` (default 262144; applies to **incoming** client updates; larger ones are dropped with a log line, not truncated [9]), `-SendPrimary` (default on: the X PRIMARY selection and cut buffer are sent too), `SetPrimary` (parameter exists in source, default on; documented in the man page only from later releases [5]). `-AllowOverride` default is `desktop,AcceptPointerEvents,SendCutText,AcceptCutText,SendPrimary,SetPrimary`, so any X client on `:0` (for example `vncconfig -set`) can change these at runtime [5].
- When the X clipboard changes, Xvnc announces it to **every** connected client that has cut-text rights; noVNC 1.3.0 immediately requests the data and places it in its panel [7][9]. With one shared session, a "Copy path" by one user lands in every user's panel.

Client (noVNC 1.3.0 UI):

- Clipboard is a side panel with a `<textarea>`. Received server text is written into the textarea only; noVNC 1.3.0 never calls `navigator.clipboard` (checked by source search) [7]. Text typed or pasted into the textarea is sent on its `change` event via `RFB.clipboardPasteFrom` [7]. That is already "explicit paste" in both directions with no automatic harvesting.
- `view_only` clients drop received clipboard client-side, and Xvnc also withholds cut text from connections without `AccessCutText` (view-only password) [7][15].

Browser:

- The Async Clipboard API is available only in secure contexts (HTTPS or localhost) [11]. On the documented plain-HTTP LAN deployment `navigator.clipboard` is unavailable, so no page script can read the client clipboard; behind an HTTPS reverse proxy it becomes possible. Chromium prompts for `clipboard-read` and persists the grant; Firefox (readText from 125) and Safari (13.1) show a per-read "Paste" context-menu prompt and do not support the `clipboard-read` permission [11][12]. Permission failures surface as `NotAllowedError` [12].
- noVNC master (unreleased after 1.7.0) adds `AsyncClipboard`: on canvas focus it calls `navigator.clipboard.readText()` and sends the result, and it writes server clipboard to the system clipboard when permitted [8]. That is automatic bidirectional sync and must not be adopted as-is.

Shortcuts:

- Chromium reserves some accelerators (for example Ctrl+N, Ctrl+T, Ctrl+W) so a page cannot cancel them; only fullscreen with keyboard lock lets a page capture them [13][18]. Keyboard Lock (`navigator.keyboard.lock()`) needs a secure context and transient activation, works only while the document is fullscreen, and is supported by Chromium 68+ only (not Firefox or Safari per browser-compat-data) [13][14]. Holding Escape for 2 s always exits lock and fullscreen [14]. noVNC 1.3.0 and master do not call `keyboard.lock()` (checked by source search) [7][8].
- noVNC 1.3.0 provides an extra-keys drawer: Ctrl and Alt toggles, Windows key toggle, Tab, Esc and Ctrl+Alt+Del [19].
- Glancelot shortcuts likely to collide in a browser tab (from `src/app/command_registry.cpp`): Ctrl+Shift+N (new folder; Chromium incognito window), F11 (fullscreen; browser fullscreen), Ctrl+= / Ctrl+- / Ctrl+0 (thumbnail zoom; browser zoom), Ctrl+Shift+P (palette; Firefox private window), Ctrl+Shift+Delete (Chromium clear-data dialog), Alt+Left (back). Which of these a page can cancel differs per browser and is unconfirmed: measure with the Playwright probe below. Commands in `command_registry.cpp` are reachable from the command palette (coverage to verify; Ctrl+Shift+P itself may collide), so the fix is an always-visible way to open the palette plus documentation, not key remapping.
- Focus release: keyboard focus stays in the noVNC canvas until the user clicks outside it or the noVNC control bar; with keyboard lock, the 2 s Escape hold. No server change can trap browser focus.

## 4. Security model

- **No automatic harvesting.** Keep noVNC at a release without `AsyncClipboard` (noble 1.3.0, or a pinned vendored 1.7.0) or, if a newer noVNC is ever vendored, disable that path (patch out or keep the page in an insecure context, which is not a control to rely on). Record this in F6 notices/DEP-DECISIONS as a pin reason.
- **Shared-session leakage.** Server-to-client clipboard is a broadcast to every full-rights client. Options in `start.py`: `-SendPrimary=0` and `-SetPrimary=0` (only explicit Copy commands cross, not every text selection; recommended default); `GLANCELOT_CLIPBOARD=both|to-browser|from-browser|off` mapping to `-SendCutText`/`-AcceptCutText`; `-MaxCutText=65536` (proposed; app-side copy sizes should have a matching cap with a status message). Narrow `-AllowOverride` (for example `desktop`) so X clients, including a compromised decoder process that inherits `DISPLAY`, cannot re-enable clipboard flags.
- **Per-client routing** (send clipboard only to the client that triggered Copy) is impossible with stock Xvnc, because the app does not know which client pressed the key. It needs the RFB policy proxy plus a "controller" concept (F078); deferred.
- **Untrusted input.** Incoming clipboard text reaches the app only when it pastes (QML text fields, path entry). Existing input validation applies; paste of a 256 KiB string into a path field must not block the UI thread (test).
- View-only clients get no clipboard at all (server-enforced) [15].

## 5. Test strategy

- **Headless RFB client (Python stdlib, shared with F074/F078 tests) against real Xtigervnc in the image:** set the X clipboard with a small X client inside the container (Qt test helper or `xclip`, which is not installed; a tiny Qt `QClipboard` helper built from the test tree is preferable) and assert: ServerCutText/Extended Clipboard delivery to two clients; UTF-8 text (generated, including non-Latin-1 code points) round-trips; `-SendCutText=0` delivers nothing; `-AcceptCutText=0` ignores client text; a client update of MaxCutText+1 bytes is dropped and logged; PRIMARY-only selection is not sent with `-SendPrimary=0`; view-only password receives nothing; image-only clipboard sends no text.
- **App:** existing clipboard UI tests stay offscreen; add a check that copy-image shows the "not available in the browser desktop" status under the Docker marker (feature test file, not `grid_ui_tests.cpp`).
- **Playwright + Chromium (advisory, if present in the CI image):** type into the noVNC clipboard textarea, confirm the app receives it; probe which Glancelot shortcuts reach the server in a normal tab and in fullscreen; confirm `navigator.clipboard` is undefined on `http://` origin.
- **RUNBOOK (user):** Firefox, Chrome and Safari over plain HTTP and behind an HTTPS reverse proxy; non-Latin-1 text; phone keyboards; shortcut table per browser.

## 6. Effort estimate

| Packet | Size | Track | Content | Prerequisites |
| --- | --- | --- | --- | --- |
| F076a clipboard policy + shortcut guidance | M | F | Xvnc flags and env var, `AllowOverride` narrowing, landing-page help, DOCKER.md section, app status for unavailable image copy, Python RFB clipboard tests, Playwright shortcut probe (advisory) | none; app status string needs a small lead grant |
| F076b per-client routing / browser clipboard | M | F | Proxy-side routing to the controller only; optional explicit "Copy to my clipboard" button on an HTTPS deployment using `writeText` on a click (user activation) | RFB-GATE proxy (F074/F078), F078 controller |

## 7. Recommendation and what would change it

GO for F076a. The packaged noVNC 1.3.0 already gives an explicit, text-only, panel-based bridge with no harvesting; what is missing is server policy (PRIMARY off, size limits, direction toggles, override lock), honest messaging about image copy, and shortcut guidance. DEFER F076b.

What would change it:
- If the user wants one-click copy into the browser's system clipboard, that requires an HTTPS deployment and a page-level button using `navigator.clipboard.writeText` on user activation; becomes GO when HTTPS (reverse proxy) is a documented supported profile.
- If the image ever ships a noVNC with `AsyncClipboard`, F076 must be re-reviewed before the upgrade.
- If F078's proxy is approved, per-client routing becomes feasible and F076b can be promoted.

## Sources

Accessed 2026-10-22.

1. Launchpad, tigervnc in Ubuntu Noble and publishing history: https://launchpad.net/ubuntu/noble/+source/tigervnc , https://launchpad.net/ubuntu/+source/tigervnc/+publishinghistory
2. Launchpad, novnc in Ubuntu Noble: https://launchpad.net/ubuntu/noble/+source/novnc
3. TigerVNC releases: https://github.com/TigerVNC/tigervnc/releases
4. noVNC releases: https://github.com/novnc/noVNC/releases
5. TigerVNC Xvnc man page v1.13.1 and master: https://raw.githubusercontent.com/TigerVNC/tigervnc/v1.13.1/unix/xserver/hw/vnc/Xvnc.man , https://raw.githubusercontent.com/TigerVNC/tigervnc/master/unix/xserver/hw/vnc/Xvnc.man
6. TigerVNC v1.13.1 `common/rfb/ServerCore.cxx`, `unix/xserver/hw/vnc/vncExtInit.cc`: https://github.com/TigerVNC/tigervnc/tree/v1.13.1
7. noVNC v1.3.0 `core/rfb.js`, `app/ui.js`: https://raw.githubusercontent.com/novnc/noVNC/v1.3.0/core/rfb.js , https://raw.githubusercontent.com/novnc/noVNC/v1.3.0/app/ui.js
8. noVNC master `core/clipboard.js`, `core/util/browser.js`, `core/rfb.js`: https://raw.githubusercontent.com/novnc/noVNC/master/core/clipboard.js , https://raw.githubusercontent.com/novnc/noVNC/master/core/util/browser.js
9. TigerVNC v1.13.1 `common/rfb/SMsgReader.cxx`, `VNCServerST.cxx`, `VNCSConnectionST.cxx`, `unix/xserver/hw/vnc/vncSelection.c`: https://github.com/TigerVNC/tigervnc/tree/v1.13.1
10. TigerVNC 1.10.0 changelog (secondary sources, seen through search results): https://www.freshports.org/net/tigervnc-server/ and https://mac.softpedia.com/progChangelog/TigerVNC-Changelog-103974.html
11. MDN, Clipboard API (source): https://raw.githubusercontent.com/mdn/content/main/files/en-us/web/api/clipboard_api/index.md
12. MDN, Clipboard.readText (source) and browser-compat-data `api/Clipboard.json`: https://raw.githubusercontent.com/mdn/content/main/files/en-us/web/api/clipboard/readtext/index.md , https://raw.githubusercontent.com/mdn/browser-compat-data/main/api/Clipboard.json
13. MDN, Keyboard.lock (source) and browser-compat-data `api/Keyboard.json`: https://raw.githubusercontent.com/mdn/content/main/files/en-us/web/api/keyboard/lock/index.md , https://raw.githubusercontent.com/mdn/browser-compat-data/main/api/Keyboard.json
14. WICG Keyboard Lock explainer and spec: https://raw.githubusercontent.com/WICG/keyboard-lock/gh-pages/explainer.md , https://raw.githubusercontent.com/WICG/keyboard-lock/gh-pages/index.bs
15. TigerVNC v1.13.1 `common/rfb/SSecurityVncAuth.cxx`, `SConnection.h`: https://github.com/TigerVNC/tigervnc/tree/v1.13.1/common/rfb
16. TigerVNC v1.13.1 `LICENCE.TXT`: https://raw.githubusercontent.com/TigerVNC/tigervnc/v1.13.1/LICENCE.TXT
17. noVNC v1.3.0 `LICENSE.txt`: https://raw.githubusercontent.com/novnc/noVNC/v1.3.0/LICENSE.txt
18. (Seen through search results, not opened) Chromium code review "allow websites to capture reserved keys in fullscreen": https://codereview.chromium.org/2636013002 and `browser_command_controller.cc` (`IsReservedCommandOrKey`): https://chromium.googlesource.com/chromium/src/+/57f212f/chrome/browser/ui/browser_command_controller.cc
19. noVNC v1.3.0 `vnc.html`: https://raw.githubusercontent.com/novnc/noVNC/v1.3.0/vnc.html
