# Docker browser desktop and pre-photo folder browser

Tasks: TREE-START-01, DOCKER-BROWSER-01. Branch `agent/docker-browser`.

## Scope

- Folders button and Ctrl+B work before any photo is open; the tree starts at logical roots (Windows drives / Linux `/`) and loads lazily.
- Selecting a folder from any location opens its first supported image; the chosen album stays the traversal root and Parent is preserved.
- Linux image (Qt 6.8.3, amd64) streamed to a browser through TigerVNC, noVNC/websockify and Openbox, with photos mounted read-only.

## Container evidence (Docker 28.4.0 in WSL Ubuntu, `glancelot-browser:dev`)

| Check | Result |
| --- | --- |
| `docker build` runs all CTest targets, then runtime `ldd` check for viewer, decoder and `libqxcb.so` | passed; image built |
| Container health | `healthy` ~8 s after start; processes tini, start.py, Xtigervnc, openbox, glancelot, websockify run as UID `viewer` (1000) |
| noVNC page | `http://127.0.0.1:16080/vnc.html` returned 200 from both WSL and Windows |
| Authentication | WebSocket/RFB handshake offered only security type 2 (VNC password) |
| Read-only mount | `touch /photos/x` failed with `Read-only file system`; source PNG mtime unchanged after open |
| Decode via desktop session | A second window opened `/photos/album/sample.png`; window title became `sample.png — Glancelot` |
| Persistence | `settings.json` in the `/config` volume stored `folder`, `image` and `saved` for that window |
| Shutdown | `docker stop` took ~0.65 s, exit code 0; slot file updated |

An earlier image failed at launch with `libOpenGL.so.0` missing; the Dockerfile now installs `libopengl0` and fails the build if any runtime library is unresolved. That failure was reproduced and the rebuilt image verified as above.

## Independent review (first pass: CHANGES REQUESTED) and responses

- Unclean stop (`docker kill`, OOM, host crash) left `/tmp/.X0-lock` and the X socket, so the restarted container failed with "Server is already active for display 0". Fixed: `docker/start.py` removes both before starting Xtigervnc. Re-verified below.
- Parent widened the traversal root with no visible change. Fixed: the status line now reads "Navigation now covers <path>".
- Clicking a drive or `/` row made the whole volume the traversal root. Fixed: depth-0 rows expand/collapse (click, Enter, Space) instead of opening; the existing "first 500 folders" search is no longer run from a volume root.
- `compose.yaml` now sets `no-new-privileges` and drops all capabilities.
- Startup `browseComputer()` runs a bounded worker listing even if the panel is hidden: accepted, harmless, documented here.
- Re-verified after fixes (image `glancelot-browser:dev`, Compose options passed as `--cap-drop ALL --security-opt no-new-privileges:true`): first start healthy, noVNC 200; `docker kill` (exit 137) then `docker start` returned to `running healthy`, noVNC 200, `wmctrl` listed the Glancelot window, and the log had no "already active" or "could not connect" errors. `docker compose` itself was not run.
- Found while rebuilding: `WindowSlotTests::killedWriterNeverCorruptsSettings` failed once in the Linux image build (a lock-free reader saw `{}` while a writer replaced `settings.json`). It reproduced in 3-8 of 12 `window_slots` runs under CPU load (12 busy loops, 12 CPUs), with and without tmpfs `/tmp`, and also affects `main`. `readSettings` now re-reads when a settings directory with a lock file yields an empty ok result. After the fix: 0 failures in 12 runs on each of the two configurations; the idle 8-test Linux build passed before and after. The root cause of the momentary absence was not identified; the retry is a mitigation.
- Unchanged: the VNC brute-force blacklist is keyed on the websockify loopback source, so repeated bad attempts may lock out the real user; not verified.

## Not verified

- Pixel-level browser interaction (clicking Folders or tree rows inside noVNC, screenshots): no browser automation was available. The Windows QML test `foldersButtonWorksWithoutPhoto` covers only Folders-button and Ctrl+B toggling without a photo. No automated test clicks a tree row, uses Enter/Space on a row, calls `browseComputer()` during a load, selects an ancestor of the current root, or cancels a cross-root `openFolder`; the behavior in this document for those paths comes from code review only.
- Resume of slot 1 after container recreation, `docker compose up` through the Compose file, Unraid, ARM, GPU, ICC/HDR and high-refresh presentation.
- A viewer window launched with `docker exec` is not closed by the supervisor on stop; this is outside normal use, and its stale slot is reclaimed by the session registry.
- The VNC password is eight characters and traffic is unencrypted; documented in docs/DOCKER.md as trusted-LAN/VPN or HTTPS-proxy only.
