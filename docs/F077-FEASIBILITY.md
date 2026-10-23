# F077 feasibility: opt-in browser upload/download into an explicit staging area

EN-09 (track session S4, Phase 0 cycle 1). Base `afeac1a`. Written 2026-10-22. A proposal for the lead: nothing here is a support claim or delivery evidence.

**Recommendation: NO-GO for v1; DEFER as a separately scoped project.** The current stack has no upload path and no authentication strong enough to protect one; building it means a new network service, a new credential system and a new writable data path. The host's existing file sharing into a read-only or explicit writable mount gives users the same outcome with no new attack surface. Size if ever done: XL. Depends on: an authenticated HTTPS front end being mandatory, per-user credentials, F078 access modes, F075 server-path indicators, F079 self-check.

## 1. What the feature needs in this codebase

Inventory row: "F077 | Opt-in browser upload/download into an explicit staging area | C / P3 / XL | New authenticated transfer design; size/quota, filename and content validation, cancellation, no source overwrite, separate permissions and no mandatory server catalog." Related rows: SYS-02 (parser boundaries/sandbox), SYS-06 (source safety), the product rule "viewing never writes original media". docs/DOCKER.md: "Browser file upload/drop is not implemented."

Current state:

- websockify 0.10.0 serves `/usr/share/novnc` with Python `SimpleHTTPRequestHandler`; only GET and HEAD are handled (plus the WebSocket upgrade), so there is no upload endpoint [5]. `--web-auth` plus an `--auth-plugin` (for example `BasicHTTPAuth`) exists but would send credentials in clear over the documented plain-HTTP deployment [5][6].
- The only credential is the 8-character VncAuth password, used in a DES challenge-response inside RFB [7]. It is not an HTTP credential and should not become one.
- `/photos` is a read-only bind mount by design (`compose.yaml`); `/config` is the only writable volume.
- Inside the app, `include/glancelot/selection_export.hpp` (F070 engine) writes one new file through a temp sibling plus rename without overwriting a selected item, and `copy_move_service` provides queued copy with collision policies. Either could stage a "download" copy; neither is network-facing.

What F077 would need: (1) a new transfer service process in the container (upload: streaming PUT into `/staging`; download: serve files the controller explicitly staged); (2) a new named volume `/staging`, writable, separate from `/photos` and `/config`; (3) a new authentication scheme with sessions, CSRF protection and Host/Origin checks; (4) quotas and disk-full handling; (5) an app surface to "stage for download" and to open `/staging`; (6) a page UI in the browser outside noVNC. Owner: Track F for (1)-(4) and (6); (5) would need hot-file grants. Paths: `docker/`, `compose.yaml`, `Dockerfile`, new `src/app/` component.

## 2. Candidate dependencies

| Candidate | Licence | GPL-3.0-or-later compatible? | Maintenance | Windows bundling | Docker bundling |
| --- | --- | --- | --- | --- | --- |
| Python 3 stdlib `http.server` | PSF-2.0 | Yes | Python docs: "not recommended for production. It only implements basic security checks"; `SimpleHTTPRequestHandler` follows symlinks [8] | Not applicable | `python3` already installed |
| Python stdlib `cgi` multipart parsing | PSF-2.0 | Yes | Deprecated in 3.11, removed in 3.13 (PEP 594) [9]; avoid | n/a | n/a |
| websockify `--web-auth` + `BasicHTTPAuth` plugin | LGPL-3.0 [6] | Yes | Ubuntu 0.10.0 [3]; upstream v0.13.0 [4] | n/a | Installed; protects only GET of static files, no upload |
| Reverse proxy with auth and TLS (Caddy, nginx, Traefik) supplied by the user outside the container | Various (Apache-2.0, BSD-2, MIT) | n/a (not shipped) | User-managed | n/a | Not bundled; DOCKER.md already recommends one |
| Host file sharing (SMB/NFS/Unraid shares) into a mounted folder | n/a | n/a | User-managed | n/a | Zero new code: add a bind mount |
| Write our own transfer service (Python stdlib, streaming PUT, no multipart) | Project | Yes | Our code; security-critical | n/a | New process under `start.py` |

## 3. Platform capability

**Windows / native Linux.** Not applicable: the native app reads local and mapped/UNC paths directly; native drag-and-drop is a separate feature. Unaffected.

**Docker/noVNC (mandatory).**

- noVNC 1.3.0 has no file transfer; RFB has no standard file-transfer message that TigerVNC Xvnc implements (Extended Clipboard defines a "files" format bit, but noVNC 1.3.0 only handles text and Xvnc 1.13 converts only text targets) [10][11]. A transfer therefore needs a separate HTTP endpoint.
- websockify listens on one port and routes everything that is not a WebSocket upgrade to static files [5]. A new service needs either a second published port or path routing in front (a reverse proxy, which is outside the image today).
- Browser side: file upload from a page (`<input type=file>`, drag and drop, `fetch` with a streamed body) works over plain HTTP; downloads work via normal links. Nothing in the browser prevents the feature; the blockers are server-side authentication and data safety.
- Disk: Docker named volumes on the default `local` driver have no size quota (unconfirmed per host filesystem; check `docker volume inspect` and whether the host supports `xfs` project quotas). The service must enforce its own quota by counting bytes and reserving before writing.

## 4. Security model

This section is the reason for the NO-GO.

- **Authentication.** VncAuth is an 8-byte DES challenge-response with one shared password and no user identity [7]. HTTP Basic over plain HTTP exposes the password on every request. A safe design needs TLS and per-user credentials; the container does not terminate TLS today and the docs only *recommend* an HTTPS reverse proxy. Making that proxy mandatory, or terminating TLS in the image with a certificate story, is new product scope.
- **Browser attacks on a LAN service.** Cookie or Basic auth on a LAN origin is exposed to CSRF from any site the user visits and to DNS rebinding. Required controls: CSRF tokens, `Origin` and `Host` allowlists, `SameSite=Strict` session cookies, no CORS. websockify's `ExpectOrigin` plugin shows the Origin-check pattern for WebSockets [6]; it is not configured today.
- **Path and name handling.** Upload names must be treated as display labels only: store under generated names (for example a random ID plus an allowlisted extension), reject path separators, NUL, control characters, Windows-reserved names and over-long names, NFC-normalise the display name, refuse symlinks in `/staging` (`O_NOFOLLOW`, `O_EXCL`), never write outside `/staging`. Archive extraction is out of scope, so zip-slip does not arise unless archive upload is later added (then it does).
- **Content.** Uploaded files then flow into the existing decoder worker (`glancelot-decoder`, separate process, content-sniffed allowlist, dimension and 256 MiB caps). Upload turns the decoder from "parses files the owner mounted" into "parses files any password holder sends", so fuzzing (F4) and the SYS-02 sandbox story become prerequisites, not hardening.
- **Download is exfiltration.** Any client with the shared password could pull any staged file; with a broad "download selected" it is a bulk export of the photo library to anyone who learns the password. Download must require an explicit per-item stage action by a controller and should be off by default.
- **Resource exhaustion.** Upload size limit, total quota, concurrent transfer limit, per-request timeouts, streaming writes with fsync then rename, cleanup of partial files on cancel or disconnect, behaviour on `ENOSPC`, and a limit on the number of files in `/staging` (directory listing cost for the folder browser).
- **Source safety.** `/photos` stays read-only; never remount it writable for F077; never let an upload overwrite or replace an existing item (no overwrite flag at all).
- **No catalog.** Staging is a plain folder the user can delete; no database of transfers beyond an in-memory list.

## 5. Test strategy

If the feature is ever promoted: Python stdlib HTTP client tests against the service in the image (no browser): auth required on every route; CSRF token and Origin/Host rejection; traversal names (`../x`, absolute paths, `%2e%2e`, NUL, Unicode look-alikes, 255+ byte names); symlink planted in `/staging`; size limit exceeded mid-stream; quota exceeded; client disconnect mid-upload leaves no partial file; disk-full on a small `tmpfs` volume; concurrent uploads at the limit; upload of generated malformed images (truncated JPEG/PNG, decompression bombs) reaching the decoder caps; download of an unstaged path refused. Playwright + Chromium for the page flow (advisory). RUNBOOK: real browsers, reverse proxy with TLS, phone uploads, large files on the user's NAS. Generated media only.

## 6. Effort estimate

| Packet | Size | Track | Content | Prerequisites |
| --- | --- | --- | --- | --- |
| F077-0 documentation alternative | S | F | DOCKER.md section "Getting files in and out": add a host share as a bind mount (read-only by default; an explicit separate writable mount for a staging folder if the user wants one), with the F075 read-only indicator | none |
| F077-DESIGN auth and transfer design | M | F + lead | Threat model, mandatory TLS front end, per-user credentials, CSRF/Origin/Host policy, quota model; independent security review | F078, F075 |
| F077-UP upload service | L | F | Streaming PUT service, quota, cancel, cleanup, tests | F077-DESIGN, F4 decoder fuzzing |
| F077-DOWN staged download | M | F + app grant | Explicit stage action in the app, read-only serving of staged copies | F077-UP |

Total for the full feature: XL, with a security review gate before any code.

## 7. Recommendation and what would change it

NO-GO for v1, DEFER the feature. Ship F077-0 (documentation of host sharing into mounts) as the supported way to move files, because it uses the host's existing authentication and adds nothing to the container's attack surface.

What would change it:
- The project makes an authenticated HTTPS front end mandatory for any browser deployment (bundled or required by the docs and checked by F079), with per-user credentials.
- F078 provides a server-enforced controller role, so upload/download can be limited to the controller.
- F4 fuzzing and the SYS-02 decoder sandbox are complete, so untrusted uploaded content is an accepted input class.
- A user need that host sharing cannot meet (for example a phone with no SMB client) is recorded with the user.

## Sources

Accessed 2026-10-22.

1. Launchpad, novnc in Ubuntu Noble: https://launchpad.net/ubuntu/noble/+source/novnc
2. Launchpad, tigervnc in Ubuntu Noble: https://launchpad.net/ubuntu/noble/+source/tigervnc
3. Launchpad, websockify in Ubuntu Noble: https://launchpad.net/ubuntu/noble/+source/websockify
4. websockify releases: https://github.com/novnc/websockify/releases
5. websockify v0.10.0 `websockify/websockifyserver.py`, `websockify/websocketproxy.py`: https://github.com/novnc/websockify/tree/v0.10.0/websockify
6. websockify v0.10.0 `websockify/auth_plugins.py` and `COPYING` (LGPL-3.0): https://raw.githubusercontent.com/novnc/websockify/v0.10.0/websockify/auth_plugins.py , https://raw.githubusercontent.com/novnc/websockify/v0.10.0/COPYING
7. TigerVNC v1.13.1 `common/rfb/SSecurityVncAuth.cxx` and `unix/vncpasswd/vncpasswd.man`: https://github.com/TigerVNC/tigervnc/tree/v1.13.1
8. Python 3.12 `Doc/library/http.server.rst` (security notes): https://raw.githubusercontent.com/python/cpython/3.12/Doc/library/http.server.rst
9. PEP 594, Removing dead batteries from the standard library: https://raw.githubusercontent.com/python/peps/main/peps/pep-0594.rst
10. noVNC v1.3.0 `core/rfb.js`: https://raw.githubusercontent.com/novnc/noVNC/v1.3.0/core/rfb.js
11. TigerVNC v1.13.1 `unix/xserver/hw/vnc/vncSelection.c`: https://raw.githubusercontent.com/TigerVNC/tigervnc/v1.13.1/unix/xserver/hw/vnc/vncSelection.c
