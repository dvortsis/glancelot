# F078 feasibility: single-controller / view-only access modes and visible shared-session state

EN-09 (track session S4, Phase 0 cycle 1). Base `afeac1a`. Written 2026-10-22. A proposal for the lead: nothing here is a support claim or delivery evidence.

**Recommendation: GO for a server-enforced view-only password (F078a, "controllers + watchers"); DEFER the visible session-state indicator and true single-controller floor control (F078b/F078c) until the lead decides on an RFB policy proxy.** TigerVNC already enforces a second, view-only password server-side. VNC has no per-connection control lock, and the app has no channel that tells it who is connected. Size: F078a S-M, F078b M, F078c L (includes the proxy core shared with F074b/F076b). Depends on: Track F (`docker/start.py`, `compose.yaml`, `docker/`), F075 (read-only mount indicators), F079 (self-check reports the mode).

## 1. What the feature needs in this codebase

Inventory row: "F078 | Single-controller/view-only access modes and visible shared-session state | C / P2 / L | Remote session controller; no assumption that a view-only client is an authorization boundary; server enforces mutation/input rights." Section 11 of FEATURE-OPPORTUNITIES: "The current container is one shared desktop/cursor/session. Remote proposals must preserve that model unless a separately scoped isolation design is approved."

Current state (`afeac1a`): `docker/start.py` reads one 8-character ASCII password from the `vnc_password` secret, pipes it to `tigervncpasswd -f`, and starts `Xtigervnc ... -SecurityTypes VncAuth -PasswordFile ... -AlwaysShared`. Every client that knows the password has full keyboard, pointer, clipboard and resize rights on the one shared desktop. `/photos` is mounted read-only (`compose.yaml`), so file mutation already fails at the kernel regardless of who clicks.

Touch points: `docker/start.py` (second secret, validation, password file, Xvnc flags), `compose.yaml` (optional second secret), a landing page under `docker/` ("Connect to control" / "Connect to watch"), docs/DOCKER.md. F078b adds a state file written by the supervisor and a small read-only status component in the app (new component file under `ui/` or a library class in `src/app/`, thin call site in a hot file under a lead grant). Owner: Track F.

## 2. Candidate dependencies

| Candidate | Licence | GPL-3.0-or-later compatible? | Maintenance | Windows bundling | Docker bundling |
| --- | --- | --- | --- | --- | --- |
| TigerVNC Xvnc + `tigervncpasswd` (`tigervnc-standalone-server`, `tigervnc-tools` 1.13.1+dfsg-2build2) | GPL-2.0-or-later [14] | Yes (separate programs) | noble release pocket 2024-04 [1]; upstream 1.16.2 [3] | Not applicable | Already installed |
| noVNC 1:1.3.0-2 `view_only` setting | MPL-2.0 / BSD-2 | Yes | noble 2023-10 [2]; upstream 1.7.0 [4] | Not applicable | Installed; client-side only |
| websockify 0.10.0+dfsg1-5build2 logs / `ExpectOrigin` plugin | LGPL-3.0 | Yes | noble [2]; upstream v0.13.0 [5] | Not applicable | Installed |
| `vncconfig` (QueryConnect dialog) | GPL-2.0-or-later | Yes | Part of TigerVNC; package location in noble unconfirmed (check `dpkg -S vncconfig` in the image) | n/a | Possibly installed; not recommended (section 3) |
| Write our own RFB policy proxy (Python 3 stdlib) | Project | Yes | Our code | n/a | `python3` installed |

## 3. Platform capability

**Windows / native Linux.** Not applicable: a native window has one local user; unaffected.

**Docker/noVNC (mandatory).**

Server-enforced options in Xvnc 1.13.1:

| Mechanism | What it does | Fit |
| --- | --- | --- |
| View-only password | `tigervncpasswd -f` reads up to two newline-separated passwords and writes both obfuscated entries; the second is the view-only password [7][8]. Xvnc's VncAuth checks the full password first (grants `AccessDefault`), then the view-only one (grants `AccessView` only) [9]. `AccessView` excludes key events, pointer events, cut text in both directions, SetDesktopSize and non-shared access; Xvnc checks each right per message [10][11]. | **Yes.** Cheap, server-enforced. Gives "controllers + watchers". |
| `-AcceptKeyEvents`, `-AcceptPointerEvents` | Global on/off for **all** clients (default on) [6] | Only a "presentation lock" for everyone; no per-client control |
| `-NeverShared` + `-DisconnectClients=0` | Only one client at a time; new ones are refused while one is active [6] | Single user, no watchers; conflicts with the shared model |
| `-AlwaysShared` (current) | Client "shared" flag ignored, so no client can kick others [6]. View-only clients are forced shared regardless [11] | Keep |
| `-QueryConnect` | Shows an accept/reject dialog on the desktop; needs `vncconfig` running [6] | Poor: the dialog appears on the shared screen, any controller can click it, and every connection arrives from websockify as `127.0.0.1` so it cannot identify the newcomer |
| `-MaxConnectionTime`, `-MaxIdleTime`, `-MaxDisconnectionTime` | Terminate **the whole Xvnc server** after the interval, not one client [6] | Not usable as per-client limits |
| `-UseBlacklist` / `-BlacklistThreshold 5` / `-BlacklistTimeout 10` | Blocks a host after repeated failed auth, doubling the timeout each time [6][12] | All clients share the source address 127.0.0.1 through websockify, so five wrong passwords from anyone lock out everyone for 10 s, then 20 s, and so on until a success. Document; keep on (it throttles guessing) |

Client side: noVNC's `view_only` setting (URL `?view_only=1` or settings panel) only stops that browser from sending events; the user can switch it off [13][15]. It is a convenience for watchers, never a boundary. With a view-only password the server drops input even if the client sends it.

Learning who is connected:

- There is no RFB-to-app channel. The app sees one X display and one stream of input with no client identity.
- Xvnc logs `Connections: accepted: <peer>` when a socket is accepted, which is **before** authentication, and `Connections: closed: <peer>` on close [11]. The peer is always websockify on `127.0.0.1`. Successful authentication and the granted role are not logged at the default level (unconfirmed for `-Log *:stderr:100`; check in the image).
- websockify logs each WebSocket connection with the real client address and logs closes [16]. Counting open WebSockets gives "browser connections", including ones still at the password prompt.
- A supervisor (`start.py`) could capture both stderr streams (forwarding them to the container log unchanged), keep a bounded count, and write `/tmp/glancelot-runtime/session.json` (directory already 0700, same UID). The app would read it with a file watcher, parse a few bounded fields and show "Shared session: N browser connections". Attack surface is small (local file, same UID, bounded JSON), but the count is approximate and log formats are version-specific (pinned Ubuntu packages limit drift).
- Exact roles need an RFB-aware proxy between websockify and Xvnc. Because VncAuth is a plain DES challenge-response, a proxy that knows both passwords can tell which one a client answered with (compute both expected responses for the observed challenge), so it knows each connection's role without changing Xvnc. The same proxy can implement **single-controller floor control**: forward input only from the connection holding the floor; release the floor after an idle interval or an explicit release; refuse SetDesktopSize and ClientCutText from others (shared with F074b and F076b). It is new security-critical protocol code (framing of all client-to-server messages, bounded buffers), and it would stop working if a TLS security type were adopted between websockify and Xvnc.

What the UI can show: the desktop is identical for every viewer, so the app can only show **global** state ("3 connected, 1 in control", "control free"). It cannot tell an individual viewer "you are view-only". Per-client role display belongs on the landing page (the "Connect to watch" link also sets `view_only=1` so the noVNC toolbar matches).

## 4. Security model

- **Authorization boundaries, in order of strength:** (1) mount permissions (`/photos` read-only; future writable mounts are explicit and separate); (2) Xvnc per-connection access rights from the view-only password; (3) optional proxy floor control. App UI states (disabled buttons, banners) are never a boundary.
- **Password handling.** Two separate secrets (`vnc_password`, optional `vnc_view_password`) rather than a two-line file, so the watcher password can be shared without exposing the control password. `start.py` must reject a view-only password equal to the control password (Xvnc would grant full rights because it checks the full password first [9]), apply the same 8-ASCII-character rule, and never log either. The password file is re-read on each connection, so rotation works without restart [6].
- **Plain HTTP limits.** VncAuth over plain HTTP exposes the challenge and response to a LAN observer (offline guessing of an 8-character password) and the RFB stream is unencrypted after authentication, so an active attacker can hijack a session. View-only mode protects against honest watchers, not against the network; the docs must keep the LAN/VPN or authenticated HTTPS reverse-proxy requirement.
- **Cross-site WebSocket hijacking.** websockify does not check `Origin` today; a hostile page in a user's browser could open the WebSocket but still needs the password. Adding the `ExpectOrigin` plugin [16] is cheap hardening with a reverse-proxy caveat (the allowed origin list must include the proxy origin); consider in F078a.
- **Local injection.** Any X client on `:0` can synthesize input (XTEST) and, by default, change `AcceptPointerEvents` and clipboard flags through `-AllowOverride` [6]. Only Openbox, the app and its decoder run there; narrow `-AllowOverride` to `desktop` (shared with F074a/F076a).
- **Mutation.** A controller can do whatever the app allows on writable mounts; F075 makes the read-only state visible. Watchers cannot mutate because no input reaches the server.

## 5. Test strategy

- **Headless RFB client in Python stdlib (shared with F074/F076):** connect with the control password and with the view-only password against real `Xtigervnc` in the image; with the view-only connection send KeyEvent, PointerEvent, ClientCutText and SetDesktopSize and assert no effect (X-side probe: a small Qt test helper window that records the input and clipboard it receives); assert the view-only connection receives no ServerCutText; assert it still receives framebuffer updates. Failure cases: wrong password, equal passwords rejected by `start.py`, missing view secret (mode off), malformed secret (spaces, 7 or 9 characters, non-ASCII), password file rotated while connected, blacklist after five failures (expect lockout of a second, correct client for 10 s; documents the shared-address effect), client disconnect during auth.
- **Supervisor state file (F078b):** generated log lines replayed into the parser; bounded counts under 1000 rapid connects/disconnects; malformed lines; state file write failure; app parses truncated/oversized JSON safely (feature UI test file).
- **Proxy (F078c):** framing fuzz target in F4; floor acquisition, idle release, controller disconnect, two simultaneous first inputs.
- **Playwright + Chromium (advisory):** landing-page links, `view_only=1` toolbar state.
- **RUNBOOK (user):** two real browsers on two machines, one control and one watch; reverse proxy with Origin check.

## 6. Effort estimate

| Packet | Size | Track | Content | Prerequisites |
| --- | --- | --- | --- | --- |
| F078a view-only password | S-M | F | Optional `vnc_view_password` secret, validation, two-entry password file, landing page with control/watch links, `-AllowOverride` narrowing, optional `ExpectOrigin`, DOCKER.md, RFB client tests | none |
| F078b approximate session indicator | M | F + lead grant | `start.py` log capture and bounded state file; app status component (read-only); docs stating it counts browser connections, not people | F078a; lead grant for the app call site |
| F078c RFB policy proxy with roles and floor control | L | F | Proxy core (shared with F074b/F076b), role detection, floor control, exact status, fuzzing, security review | F078a, F4 fuzz harness |

## 7. Recommendation and what would change it

GO for F078a: it delivers a server-enforced watcher mode with existing packages and a few lines in `start.py`, and it matches the inventory's "server enforces mutation/input rights". DEFER F078b and F078c. The indicator is only approximate without the proxy, and single-controller floor control does not exist in VNC.

What would change it:
- The lead approves the RFB policy proxy as a Track F component with a security review. F078c then also unblocks F074b and F076b; promote all three together.
- The user accepts an approximate "N browser connections" indicator. F078b can then be GO on its own.
- A move to TLS between websockify and Xvnc, or a different streaming stack, invalidates the proxy approach; re-evaluate then.

## Sources

Accessed 2026-10-22.

1. Launchpad, tigervnc in Ubuntu Noble and publishing history: https://launchpad.net/ubuntu/noble/+source/tigervnc , https://launchpad.net/ubuntu/+source/tigervnc/+publishinghistory
2. Launchpad, novnc and websockify in Ubuntu Noble: https://launchpad.net/ubuntu/noble/+source/novnc , https://launchpad.net/ubuntu/noble/+source/websockify
3. TigerVNC releases: https://github.com/TigerVNC/tigervnc/releases
4. noVNC releases: https://github.com/novnc/noVNC/releases
5. websockify releases: https://github.com/novnc/websockify/releases
6. TigerVNC Xvnc man page v1.13.1: https://raw.githubusercontent.com/TigerVNC/tigervnc/v1.13.1/unix/xserver/hw/vnc/Xvnc.man
7. TigerVNC v1.13.1 `unix/vncpasswd/vncpasswd.man`: https://raw.githubusercontent.com/TigerVNC/tigervnc/v1.13.1/unix/vncpasswd/vncpasswd.man
8. TigerVNC v1.13.1 `unix/vncpasswd/vncpasswd.cxx` (`encrypt_pipe`): https://raw.githubusercontent.com/TigerVNC/tigervnc/v1.13.1/unix/vncpasswd/vncpasswd.cxx
9. TigerVNC v1.13.1 `common/rfb/SSecurityVncAuth.cxx`: https://raw.githubusercontent.com/TigerVNC/tigervnc/v1.13.1/common/rfb/SSecurityVncAuth.cxx
10. TigerVNC v1.13.1 `common/rfb/SConnection.h`: https://raw.githubusercontent.com/TigerVNC/tigervnc/v1.13.1/common/rfb/SConnection.h
11. TigerVNC v1.13.1 `common/rfb/VNCSConnectionST.cxx`, `VNCServerST.cxx`: https://github.com/TigerVNC/tigervnc/tree/v1.13.1/common/rfb
12. TigerVNC v1.13.1 `common/rfb/Blacklist.cxx`: https://raw.githubusercontent.com/TigerVNC/tigervnc/v1.13.1/common/rfb/Blacklist.cxx
13. noVNC v1.3.0 `docs/API.md` and `docs/EMBEDDING.md`: https://raw.githubusercontent.com/novnc/noVNC/v1.3.0/docs/API.md , https://raw.githubusercontent.com/novnc/noVNC/v1.3.0/docs/EMBEDDING.md
14. TigerVNC v1.13.1 `LICENCE.TXT`: https://raw.githubusercontent.com/TigerVNC/tigervnc/v1.13.1/LICENCE.TXT
15. noVNC v1.3.0 `app/ui.js`, `core/rfb.js`: https://raw.githubusercontent.com/novnc/noVNC/v1.3.0/app/ui.js , https://raw.githubusercontent.com/novnc/noVNC/v1.3.0/core/rfb.js
16. websockify v0.10.0 `websockify/websockifyserver.py`, `websockify/websocketproxy.py`, `websockify/auth_plugins.py`: https://github.com/novnc/websockify/tree/v0.10.0/websockify
