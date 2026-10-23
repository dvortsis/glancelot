# F074 feasibility: remote desktop resize-to-browser with fixed-size fallback

EN-09 (track session S4, Phase 0 cycle 1). Base `afeac1a`. Written 2026-10-22. A proposal for the lead: nothing here is a support claim or delivery evidence.

**Recommendation: GO for a fixed-size hardening slice now (F074a); DEFER resize-to-browser (F074b) until a server-side RFB policy proxy exists.** The packaged stack can resize, but nothing in it can bound, debounce or arbitrate client resize requests, and today any authenticated client can already request up to 16384x16384. Size: F074a S, F074b M on top of the shared proxy (L). Depends on: Track F (Dockerfile, `docker/`), the RFB policy proxy shared with F076/F078, F2 PERF-REMOTE pixel-cost measurements.

## 1. What the feature needs in this codebase

Inventory row (docs/FEATURE-OPPORTUNITIES.md section 11): "F074 | Remote desktop resize-to-browser with a fixed-size fallback | C / P2 / M | Server/client capability probe; debounce resize, bound total pixels and preserve pane minimums; simultaneous clients cannot endlessly resize each other." No REQUIREMENTS.md row covers Docker; UX-14 (pane minimum sizes, compact layouts) and UX-15 ("fit/physical zoom on resize") constrain the app side.

Current state (verified at `afeac1a`):

- `docker/start.py` validates `DESKTOP_SIZE` to 560x400..3840x2160 and starts `Xtigervnc :0 -geometry WxH ... -AlwaysShared`. It does **not** pass `-AcceptSetDesktopSize`, so the Xvnc default (on) applies [8].
- The app starts with `--window-size max(540,W-4)xmax(360,H-40)` (`app/main.cpp:301`); `ui/Main.qml` sets `minimumWidth: 540`, `minimumHeight: 360`. The window is not maximized, so a root-window resize does not resize it.
- `index.html` is a copy of noVNC `vnc.html`; noVNC's default `resize` setting is `off` [12], but any user can switch the settings panel to "Remote resizing", which sends SetDesktopSize [12][13].
- docs/DOCKER.md states "resizing the browser does not change the server desktop". That is true for the default setting only.

Finding: the `DESKTOP_SIZE` bound is only a startup bound. Xvnc 1.13.1 accepts any client-requested size up to 16384x16384 (`VNCServerST::setDesktopSize` rejects only larger sizes [9]; RandR size range is set to 32..32768 [10]). There is no Xvnc option for a maximum client-requested size in 1.13.1 or in current upstream (option lists diffed between v1.13.1 and master [8][7]). A 16384x16384x32-bit framebuffer is 1 GiB before Qt, Mesa or encoder buffers, and browser zoom-out alone (for example 33 % zoom on a 4K monitor gives about 11520x6480 CSS pixels) can produce very large requests, because noVNC sends the viewport size in CSS pixels [13].

What F074 would touch: `docker/start.py` (Xvnc flags, policy env vars), a new Openbox config under `docker/` (maximize rule), a new static landing page under `docker/` replacing the `index.html` copy (Dockerfile line), docs/DOCKER.md, and the shared RFB policy proxy (new `docker/rfb_gate.py`, see section 4). No change to `include/glancelot/*`, `ui/Main.qml` or `viewer_controller` is needed for the minimum slice; see section 3 for the app-side check. Owner: Track F (Dockerfile, compose.yaml, `docker/`, `tools/platform/*`).

## 2. Candidate dependencies

Versions in the image come from Ubuntu 24.04 (noble) packages; nothing is vendored today.

| Candidate | Licence | GPL-3.0-or-later compatible? | Maintenance | Windows bundling | Docker bundling |
| --- | --- | --- | --- | --- | --- |
| TigerVNC Xvnc (`tigervnc-standalone-server`) | GPL-2.0-or-later [18] | Yes (separate program; "or later") | noble ships 1.13.1+dfsg-2build2, release pocket, 2024-04 [1][2]; upstream latest 1.16.2 (26 Mar, year inferred 2026 from release order) [5] | Not applicable | Already installed |
| noVNC (`novnc`) | MPL-2.0 core JS; BSD-2 HTML/CSS [19] | Yes (separate program served to the browser) | noble ships 1:1.3.0-2 (2023-10) [3]; upstream latest 1.7.0 (28 Apr, year inferred 2026) [6] | Not applicable | Already installed |
| websockify (`websockify`) | LGPL-3.0 [20] | Yes | noble ships 0.10.0+dfsg1-5build2 [4]; upstream latest v0.13.0 (12 Feb, year inferred 2025) [21] | Not applicable | Already installed |
| Openbox (`openbox`) | GPL-2.0-or-later (Debian copyright, unconfirmed in this session: check `/usr/share/doc/openbox/copyright` in the image) | Yes | noble ships 3.6.1-12build5 (2024-04) [22]; upstream inactive since 3.6.1 (unconfirmed release date) | Not applicable | Already installed |
| Write our own: small RFB policy proxy in Python 3 stdlib (`asyncio`) | Project GPL-3.0-or-later | Yes | Our code | Not applicable | `python3` already in runtime image |
| Vendored newer noVNC (1.6+/1.7) | MPL-2.0 | Yes | Faster resize rate-limit (one pending request, 100 ms) [14] | Not applicable | Would replace the Ubuntu package; adds a provenance/notice item for F6 |

No new third-party package is needed for F074a or F074b. A vendored noVNC is not required: noVNC 1.3.0 already debounces browser resizes by 500 ms [13].

## 3. Platform capability

**Windows / native Linux.** Not applicable: native windows already follow the OS window manager; no change.

**Docker/noVNC (mandatory).** Per stage:

| Stage | Packaged capability | Evidence |
| --- | --- | --- |
| Browser -> noVNC 1.3.0 | `resize=remote` (URL query param or settings panel) sets `RFB.resizeSession`; noVNC waits 500 ms after the last window `resize` event, then sends SetDesktopSize with `getBoundingClientRect()` size (CSS pixels, not device pixels). It sends only after the server has sent an ExtendedDesktopSize rect, so the capability probe is built in. The URL query takes precedence over localStorage, so a landing page can redirect to `vnc.html?resize=remote` or `?resize=scale`. No `defaults.json` support before 1.6.0 [12][13][6][15]. | noVNC 1.3.0 `app/ui.js` `initSetting`, `core/rfb.js` `_handleResize`/`_requestRemoteResize` |
| noVNC -> websockify 0.10.0 | Transparent byte relay; no message inspection | [20] source |
| Xvnc 1.13.1 | `-AcceptSetDesktopSize` default on; `-AcceptSetDesktopSize=0` makes Xvnc answer "prohibited" [8][9]. Accepted sizes are applied through RandR (`RRScreenSizeSet`, physical size derived from the configured DPI, default 96) [10][11]. Other clients receive an ExtendedDesktopSize with reason "other client" [9]. | `VNCSConnectionST::setDesktopSize`, `VNCServerST::setDesktopSize`, `RandrGlue.c` |
| Openbox 3.6 | Re-fits maximized windows to the new work area on a root-size change (expected from Openbox behaviour, unconfirmed: verify with `xdotool getwindowgeometry` or Qt evidence after `xrandr --fb`) | Probe needed |
| Qt 6.8.3 xcb | Updates `QScreen` geometry on RandR CRTC/output change notifications and calls `handleScreenGeometryChange`, so `QScreen::geometryChanged`/`availableGeometryChanged` fire [16]. The app window itself only changes when the WM resizes it. | `qxcbscreen.cpp` |

Multiple clients: with `-AlwaysShared` every client shares one framebuffer. Stock noVNC (1.3.0 and current master) re-requests a size only after its own window resize or on its first ExtendedDesktopSize after connect, not when another client changed the size [13][14]. Endless ping-pong therefore does not happen with stock noVNC, but "last mover wins": each new client that connects with remote resizing on resizes everyone, and a phone and a desktop browser with remote resizing on will alternate whenever either window changes. Other clients (for example a modified browser client speaking RFB over the same WebSocket) may re-request on every change and loop; only a server-side policy prevents that.

App side: the app is launched with a fixed `--window-size`. For resize-to-browser the window must follow the desktop. The cheapest route is an Openbox `rc.xml` application rule (maximized, optionally undecorated) shipped in `docker/` and passed with `openbox --config-file`. `recoverStartupPlacement` (`app/main.cpp`) already preserves a maximized state. To check during implementation: per-slot geometry persistence (`/config`) must not store a transient tiny or huge size; UX-14 pane minimums must keep the layout usable at the 560x400 floor; the window minimum 540x360 plus decorations must fit the floor (it does with the current 4/40 px margins). If Openbox does not refit, an app change would be needed (follow `QScreen::availableGeometryChanged` in Docker mode only), which touches the hot files and needs a lead grant.

Pixel cost: PERFORMANCE-STRATEGY section 11 notes 3840x2160 is 6.4 times 1440x900 and that browser scaling does not reduce server work. Any resize policy needs a pixel ceiling chosen from F2 PERF-REMOTE measurements, not from the browser size.

## 4. Security model

- **Who can resize.** Any client that passed VncAuth. With a single shared password, all clients are equally trusted, but oversize requests are a robustness problem (accidental browser zoom) as much as a hostile one. View-only connections (F078) cannot resize: Xvnc checks `AccessSetDesktopSize`, which a view-only password does not grant [9][17].
- **Bounding.** Xvnc cannot bound requested sizes. Options:
  1. `-AcceptSetDesktopSize=0` (fixed size, current documented behaviour, server-enforced). Recommended now.
  2. RFB policy proxy ("rfb-gate"): a Python stdlib process between websockify and Xvnc (websockify targets `127.0.0.1:5901`, the proxy forwards to Xvnc on `127.0.0.1:5900` or a Unix socket via `-rfbunixpath` [8]). After the handshake it frames client-to-server messages (all are length-determinable for the types TigerVNC accepts; unknown type closes the connection) and rewrites or rejects SetDesktopSize (type 251) outside policy: clamp to `[560x400, MAX_PIXELS]`, one change per N ms server-wide, optional "only the controller may resize" (F078). Stays unencrypted-RFB-aware only because VncAuth does not encrypt; it would break if TLS security types were adopted.
  3. An X-side watchdog that undoes oversize RandR changes after the fact. Rejected: the allocation has already happened and it fights the client.
- **Allocation.** The policy must bound `width*height` (not each axis alone) to keep Xvnc framebuffer, Qt backing store and Mesa llvmpipe buffers inside the container memory profile (F1).
- **Not a boundary.** Client-side settings (`resize=off` in the landing page) are convenience only; any client can change them.

## 5. Test strategy

- **No browser needed (CI, Linux build stage or a test stage of the image):** a headless RFB client in Python stdlib (about 200 lines: version, VncAuth DES challenge, ClientInit, SetEncodings with ExtendedDesktopSize -308, SetDesktopSize) against the real `Xtigervnc` inside the image. Cases: request inside bounds succeeds and the second client receives reason 2; `-AcceptSetDesktopSize=0` returns status 1 (prohibited); with the proxy, 16384x16384, 100x100 and 0x0 are clamped or rejected; 50 requests in one second are coalesced; client disconnect mid-request; Xvnc killed mid-resize (supervisor exits cleanly); view-only password cannot resize. DES for VncAuth needs a tiny pure-Python DES or `openssl` (unconfirmed whether `openssl des-ecb` works under OpenSSL 3 without the legacy provider; check in the image).
- **App reaction:** after a resize, read the window geometry with `xwininfo`/`wmctrl -lG` (wmctrl is installed) and the app's existing `--report`/screenshot evidence; check the 560x400 floor with UX-14 panes visible.
- **Browser automation (optional, advisory):** Playwright + Chromium driving `vnc.html?resize=remote` while changing the viewport; asserts on the server size via the Python client. Chromium may not be present in the gate image; treat as advisory.
- **RUNBOOK (user):** real browsers on desktop and phone, two simultaneous clients, browser zoom, HiDPI laptop, reverse proxy path.
- Generated media only; no photos are needed for resize tests beyond a generated PNG to show something on the canvas.

## 6. Effort estimate

| Packet | Size | Track | Content | Prerequisites |
| --- | --- | --- | --- | --- |
| F074a fixed-size hardening | S | F | `-AcceptSetDesktopSize=0` explicitly in `start.py`; narrow `-AllowOverride` so X clients cannot re-enable it; DOCKER.md note; Python RFB client test asserting "prohibited" | none (can join F1) |
| RFB-GATE policy proxy core | L | F | Shared with F076b and F078b; handshake tracking, message framing, bounded buffers, fuzz target (F4) | F074a; security review |
| F074b resize-to-browser | M | F | `GLANCELOT_REMOTE_RESIZE=off|browser`, pixel ceiling, server debounce, controller-only option, Openbox maximize rule, landing page with `resize=remote`, app geometry checks | RFB-GATE, F2 pixel budgets |

## 7. Recommendation and what would change it

GO for F074a now: it closes an unbounded allocation path and matches what DOCKER.md already promises. DEFER F074b. Resize-to-browser cannot meet "bound total pixels" and "simultaneous clients cannot endlessly resize each other" with the packaged components alone, because enforcement must be server-side and Xvnc has no bound.

What would change it:
- The lead approves the RFB-GATE proxy (driven mainly by F078). F074b then becomes a small policy addition and can be GO.
- A future TigerVNC adds a maximum-size or resize-policy option (not present in upstream master man page as of this check [7]); then F074b needs only Xvnc flags plus the Openbox rule.
- If the user accepts "trusted users only, no hard bound", a client-only slice (landing page offering `resize=remote`, Openbox maximize) is possible but would not meet the inventory acceptance text and should not be called F074.

## Sources

Accessed 2026-10-22.

1. Launchpad, tigervnc in Ubuntu Noble: https://launchpad.net/ubuntu/noble/+source/tigervnc
2. Launchpad, tigervnc publishing history: https://launchpad.net/ubuntu/+source/tigervnc/+publishinghistory
3. Launchpad, novnc in Ubuntu Noble and publishing history: https://launchpad.net/ubuntu/noble/+source/novnc , https://launchpad.net/ubuntu/+source/novnc/+publishinghistory
4. Launchpad, websockify in Ubuntu Noble: https://launchpad.net/ubuntu/noble/+source/websockify
5. TigerVNC releases: https://github.com/TigerVNC/tigervnc/releases
6. noVNC releases: https://github.com/novnc/noVNC/releases
7. TigerVNC Xvnc man page, master: https://raw.githubusercontent.com/TigerVNC/tigervnc/master/unix/xserver/hw/vnc/Xvnc.man
8. TigerVNC Xvnc man page, v1.13.1: https://raw.githubusercontent.com/TigerVNC/tigervnc/v1.13.1/unix/xserver/hw/vnc/Xvnc.man
9. TigerVNC v1.13.1 `common/rfb/VNCServerST.cxx` and `VNCSConnectionST.cxx`: https://github.com/TigerVNC/tigervnc/tree/v1.13.1/common/rfb
10. TigerVNC v1.13.1 `unix/xserver/hw/vnc/xvnc.c`: https://raw.githubusercontent.com/TigerVNC/tigervnc/v1.13.1/unix/xserver/hw/vnc/xvnc.c
11. TigerVNC v1.13.1 `unix/xserver/hw/vnc/RandrGlue.c`: https://raw.githubusercontent.com/TigerVNC/tigervnc/v1.13.1/unix/xserver/hw/vnc/RandrGlue.c
12. noVNC v1.3.0 `docs/EMBEDDING.md` and `app/ui.js`: https://raw.githubusercontent.com/novnc/noVNC/v1.3.0/docs/EMBEDDING.md , https://raw.githubusercontent.com/novnc/noVNC/v1.3.0/app/ui.js
13. noVNC v1.3.0 `core/rfb.js`: https://raw.githubusercontent.com/novnc/noVNC/v1.3.0/core/rfb.js
14. noVNC master `core/rfb.js`: https://raw.githubusercontent.com/novnc/noVNC/master/core/rfb.js
15. noVNC v1.3.0 `docs/API.md`: https://raw.githubusercontent.com/novnc/noVNC/v1.3.0/docs/API.md
16. Qt 6.8.3 `src/plugins/platforms/xcb/qxcbscreen.cpp`: https://raw.githubusercontent.com/qt/qtbase/v6.8.3/src/plugins/platforms/xcb/qxcbscreen.cpp
17. TigerVNC v1.13.1 `common/rfb/SSecurityVncAuth.cxx`, `SConnection.h`: https://github.com/TigerVNC/tigervnc/tree/v1.13.1/common/rfb
18. TigerVNC v1.13.1 `LICENCE.TXT` and source headers ("version 2 ... or any later version"): https://raw.githubusercontent.com/TigerVNC/tigervnc/v1.13.1/LICENCE.TXT
19. noVNC v1.3.0 `LICENSE.txt`: https://raw.githubusercontent.com/novnc/noVNC/v1.3.0/LICENSE.txt
20. websockify v0.10.0 `COPYING` and `websockify/websockifyserver.py`: https://github.com/novnc/websockify/tree/v0.10.0
21. websockify releases: https://github.com/novnc/websockify/releases
22. Launchpad, openbox in Ubuntu Noble: https://launchpad.net/ubuntu/noble/+source/openbox
