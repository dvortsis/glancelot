# EN-02 cloud proof (session S12): blocked, `download.qt.io` still denied

Written for: the lead. Proof session S12, October 23, 2026, fresh Claude Code cloud session.

## Result: FAIL (environment), not a script defect

The packet's acceptance path (Qt download, configure, build, offscreen CTest, multipage TIFF PASS) **did not run**. This session's egress proxy still refuses `download.qt.io` and every aqt fallback mirror with `403` to `CONNECT`, exactly as in the session that wrote EN-02. Qt 6.8.3 could not be installed, so the configure, build and CTest steps, step 4 (TIFF test, plugin listing, `readelf`) and the test counts are **unverified**. Nothing here stands in for them.

The parts that did run, did so as specified: the Docker probe, the apt install and its skip on rerun, the aqt venv creation and its reuse on rerun, the **relative `--qt-prefix` fix** (round 1 fix 1), and the clear failure message with exit 1.

| Item | Value |
| --- | --- |
| Checked out | `origin/claude/en-02-linux-bootstrap` at `dffc799` ("docs(EN-02): annotate round 1 check headers; CRLF note") as local `en02-proof` |
| Container | Ubuntu 24.04.5 LTS, 4 vCPU, 15 GiB RAM, root; no `~/Qt`, no `qt-rel`, no `~/.cache/glancelot` at start |
| Session | created 2026-10-23T03:00:02Z, environment `env_01CEGtogVxi1HghgjNoESVyM` ("Default", described as "trusted network access"; the only environment on the account) |
| Script | unchanged (no edits to `scripts/linux-bootstrap.sh`, product code or the Dockerfile) |
| Logs | `docs/verification/en-02-cloud-proof/`: `bootstrap-run1.log`, `bootstrap-run2.log`, `aqtinstall.log` (both runs), `network-probe.txt` |

## Likely cause and fix (for the lead and the user)

The `download.qt.io` allowance has not reached this environment's policy. The proxy reports `selective: false` and refuses the host at the gateway (`connect_rejected`, "gateway answered 403 to CONNECT (policy denial or upstream failure)"). The account has one environment, `Default`, and this session runs in it, so the change was not saved to a different environment. Most likely it was either not saved, or the domain was entered under an access level that is not the selected one: Allowed domains takes effect only under the **Limited** level (in older apps, **Custom** with "include default package managers" ticked).

To fix it: open the cloud environment menu in the session title bar, choose Edit, then Network access. Select Limited (or Custom), add `download.qt.io` under Allowed domains, leave "Allow package managers" ticked and save. Docs: https://code.claude.com/docs/en/cloud-environments#network-access. Then start a **new** session to rerun this proof; a running session keeps its policy.

aqt picks a random fallback mirror when `download.qt.io` fails (here `qt-mirror.dannhauer.de` and `ftp.jaist.ac.jp`; in EN-02, `mirrors.dotsrc.org` and `ftp.yz.yamagata-u.ac.jp`). Each fallback needs its own allowance, so `download.qt.io` alone is the domain to add: once it is reachable, aqt does not fall back.

## Network probe (actual output)

Before running anything, then again after both runs (`network-probe.txt`):

```text
$ curl -sS -m 10 -o /dev/null -w "download.qt.io: %{http_code}\n" https://download.qt.io/
curl: (56) CONNECT tunnel failed, response 403
download.qt.io: 000
master.qt.io: 000          (same 403)
mirrors.dotsrc.org: 000    (same 403)
qt.mirror.constant.com: 000, mirror.accum.se: 000   (first probe only, same 403)

$ curl -sS "$HTTPS_PROXY/__agentproxy/status"
  "selective": false,
  "recentRelayFailures": [ { "kind": "connect_rejected",
     "detail": "gateway answered 403 to CONNECT (policy denial or upstream failure)",
     "host": "download.qt.io:443" }, ... ftp.jaist.ac.jp:443, master.qt.io:443, mirrors.dotsrc.org:443 ]
```

## Step 2: first run, relative Qt prefix (`bootstrap-run1.log`)

```text
$ time scripts/linux-bootstrap.sh --qt-prefix qt-rel --build-dir build-proof
==> Docker probe
docker: client present, daemon unreachable
gate path: offscreen CTest here only; the Docker build runs in lead-windows-gate (authoritative). See docs/CI-GATE-RUNNER.md.

==> System packages (Ubuntu 24.04)
installing: python3-venv libgl1-mesa-dev libxkbcommon-dev libxcb-cursor0 libxcb-icccm4 libxcb-keysyms1 libxcb-xinerama0 libxcb-image0 libxcb-render-util0 libxkbcommon-x11-0 libegl1
Fetched 33.8 MB in 3s (10.7 MB/s)
...
0 upgraded, 24 newly installed, 0 to remove and 39 not upgraded.
...                                   (24 "Setting up" lines)

==> Qt 6.8.3 linux_gcc_64 -> /home/user/glancelot/qt-rel/6.8.3/gcc_64
INFO    : aqtinstall(aqt) v3.3.0 on Python 3.13.16 [CPython GCC 13.3.0]
WARNING : Failed to download checksum for the file 'online/qtsdkrepository/linux_x64/desktop/qt6_683/qt6_683/Updates.xml'. This may happen on unofficial mirrors.
WARNING : Connection to 'https://download.qt.io' failed. Retrying with fallback 'https://qt-mirror.dannhauer.de/'.
WARNING : Failed to download checksum for the file 'online/qtsdkrepository/linux_x64/desktop/qt6_683/qt6_683/Updates.xml'. This may happen on unofficial mirrors.
ERROR   : Failure to connect to https://qt-mirror.dannhauer.de/online/qtsdkrepository/linux_x64/desktop/qt6_683/qt6_683/Updates.xml: ProxyError
Qt download failed (log: /home/user/glancelot/qt-rel/aqtinstall.log). aqt needs HTTPS access to download.qt.io or a mirror
given with --qt-base. A Claude Code cloud environment whose network policy denies download.qt.io needs it
under the environment's Allowed domains; a session started after that change can rerun this script.

real	1m17.935s
user	0m8.535s
sys	0m2.292s
exit=1
```

- **apt:** 11 missing packages from the script's list were installed (24 packages with dependencies), with no prompts, as root without sudo.
- **aqt:** venv created at `~/.cache/glancelot/aqt-3.3.0`, `aqtinstall` 3.3.0 installed from PyPI (PyPI is reachable). The first `install-qt` call (qtbase, qtdeclarative, qtshadertools, qtimageformats) failed at the repository's `Updates.xml`, so the ICU call never ran. No archive was downloaded.
- **Relative prefix fix (round 1 fix 1): works.** `qt-rel` resolved to `/home/user/glancelot/qt-rel`, and after aqt ran from inside the prefix the tree is `qt-rel/aqtinstall.log` only. There is no nested `qt-rel/qt-rel`.
- **configure / build / ctest:** not reached; `build-proof` was never created.

## Step 3: rerun, skip paths (`bootstrap-run2.log`)

```text
$ time scripts/linux-bootstrap.sh --qt-prefix qt-rel --build-dir build-proof -- -R '^(format|thumbnails|animation)$'
==> Docker probe
docker: client present, daemon unreachable
gate path: offscreen CTest here only; the Docker build runs in lead-windows-gate (authoritative). See docs/CI-GATE-RUNNER.md.

==> System packages (Ubuntu 24.04)
all 22 packages already installed

==> Qt 6.8.3 linux_gcc_64 -> /home/user/glancelot/qt-rel/6.8.3/gcc_64
INFO    : aqtinstall(aqt) v3.3.0 on Python 3.13.16 [CPython GCC 13.3.0]
WARNING : Failed to download checksum for the file 'online/qtsdkrepository/linux_x64/desktop/qt6_683/qt6_683/Updates.xml'. This may happen on unofficial mirrors.
WARNING : Connection to 'https://download.qt.io' failed. Retrying with fallback 'https://ftp.jaist.ac.jp/pub/qtproject/'.
WARNING : Failed to download checksum for the file 'online/qtsdkrepository/linux_x64/desktop/qt6_683/qt6_683/Updates.xml'. This may happen on unofficial mirrors.
ERROR   : Failure to connect to https://ftp.jaist.ac.jp/pub/qtproject/online/qtsdkrepository/linux_x64/desktop/qt6_683/qt6_683/Updates.xml: ProxyError
Qt download failed (log: /home/user/glancelot/qt-rel/aqtinstall.log). aqt needs HTTPS access to download.qt.io or a mirror
given with --qt-base. A Claude Code cloud environment whose network policy denies download.qt.io needs it
under the environment's Allowed domains; a session started after that change can rerun this script.

real	0m54.314s
user	0m0.783s
sys	0m0.187s
exit=1
```

- **apt skip: proven.** All 22 packages were detected and no `apt-get` ran.
- **aqt venv reuse: proven.** `aqt_venv_ok` accepted the existing venv, so pip did not run: user time fell from 8.5 s to 0.8 s, and the venv still reports `3.3.0` (`importlib.metadata.version("aqtinstall")`).
- **Qt skip (`qt_complete`): not exercised**, because no Qt is installed. The 54 s are aqt's own retries (5 checksum attempts about 4.5 s apart per host, `aqtinstall.log`).

## Step 4: multipage TIFF, plugins, `readelf`: not run

There is no Qt, so there is no `build-proof/animation_tests` and no `qt-rel/6.8.3/gcc_64/plugins/imageformats/`. **TIFF PASS: unverified.**

## Commands for the rerun (unchanged from the S12 brief)

In a new session, after the network change:

```sh
git fetch origin && git checkout -B en02-proof origin/claude/en-02-linux-bootstrap
curl -sS -m 10 -o /dev/null -w "%{http_code}\n" https://download.qt.io/     # expect 200 or 301, not 000/403
time scripts/linux-bootstrap.sh --qt-prefix qt-rel --build-dir build-proof 2>&1 | tee bootstrap-run1.log
time scripts/linux-bootstrap.sh --qt-prefix qt-rel --build-dir build-proof -- -R '^(format|thumbnails|animation)$' 2>&1 | tee bootstrap-run2.log
build-proof/animation_tests -functions | grep -i tiff
QT_QPA_PLATFORM=offscreen build-proof/animation_tests testMultipageTiffNavigation -v1
ls qt-rel/6.8.3/gcc_64/plugins/imageformats/
for p in qtiff qwebp qtga; do readelf -d qt-rel/6.8.3/gcc_64/plugins/imageformats/lib$p.so | grep NEEDED; done
```

## Open questions for the lead

1. Ask the user to confirm the cloud environment's Network access level and its Allowed domains list (see "Likely cause and fix"), then start a new proof session. Nothing in EN-02 needs to change for this failure.
