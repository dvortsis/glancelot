# EN-02 Linux bootstrap: handoff

Written for: the lead and the EN-02 reviewer. Session S2 (environment), Phase 0 cycle 1, October 22-23, 2026.

## Status: implemented; Qt-dependent acceptance blocked by this session's network policy

The cloud environment's network policy denies `download.qt.io` and every Qt mirror aqt tries, so Qt 6.8.3 could not be installed in this session. The script's package, Docker-probe and failure paths are proven below with actual output. Configure, build, CTest and the "skip to pass" comparison did not run here and are **unverified**.

| Item | Value |
| --- | --- |
| Branch | `claude/en-02-linux-bootstrap` |
| Base SHA | `afeac1a5d5b1da088d7b35447a933a2f64400f49` (`origin/main`, "chore(lead): freeze Phase 0 cycle 1 contracts") |
| Implementation commit | `39788a6` |
| Tip SHA | the commit that adds this handoff (`git log -1 claude/en-02-linux-bootstrap`) |

## Files changed

- `scripts/linux-bootstrap.sh` (new, mode 100755).
- `Dockerfile`: `qt` stage aqt line adds `qtimageformats`; runtime stage adds one check that the WebP, TIFF and TGA plugins exist and every image-format plugin resolves its libraries (`ldd`).
- `BUILDING.md`: Linux section rewritten around the script; lists Python3 and Xvfb (required by `cmake/RecoveryTests.cmake`).
- `docs/handoff/EN-02-HANDOFF.md` (this file).

Not changed (lead-owned): `.github/workflows/*`.

## For the lead: aqt arguments

The script and the Dockerfile use exactly these (aqtinstall 3.3.0):

```sh
aqt install-qt linux desktop 6.8.3 linux_gcc_64 -O <prefix> -m qtshadertools qtimageformats --archives qtbase qtdeclarative qtshadertools qtimageformats
aqt install-qt linux desktop 6.8.3 linux_gcc_64 -O <prefix> --archives icu
```

For the hosted Windows job (`.github/workflows/windows-gate.yml`, lead-owned), line 51 becomes

```powershell
python -m aqt install-qt windows desktop 6.8.3 win64_msvc2022_64 -O C:\Qt -m qtshadertools qtimageformats --archives qtbase qtdeclarative qtshadertools qtimageformats
```

and the cache key on line 46 must change too (for example `qt-6.8.3-win64_msvc2022_64-qtbase-qtdeclarative-qtshadertools-qtimageformats-aqt3.3.0`), or a cache hit restores the old Qt without image formats. `build.yml` already installs `qtshadertools qtimageformats`.

**Cloud network policy.** Every cloud track session that follows the Linux instructions (this script or the snippet in PHASE0-CYCLE1) needs `download.qt.io` allowed. In this environment it is refused (`CONNECT tunnel failed, response 403`); so are aqt's fallback mirrors (`mirrors.dotsrc.org`, `ftp.yz.yamagata-u.ac.jp`). The user adds `download.qt.io` under the cloud environment's Network access, Allowed domains (keep "Allow package managers" ticked); see https://code.claude.com/docs/en/cloud-environments#network-access. Sessions started after the change can run the script.

## Script behaviour

- Ubuntu 24.04. Probes Docker first, then installs only the missing apt packages (sudo only when not root), then Qt, configure, build, offscreen CTest.
- Packages: the Dockerfile `qt` and `build` stage lists (`ca-certificates python3-venv build-essential cmake ninja-build libgl1-mesa-dev libxkbcommon-dev libfontconfig1 libdbus-1-3 libxcb-* libxkbcommon-x11-0 libegl1 libglib2.0-0t64 xvfb libgl1-mesa-dri`) plus `python3`.
- Qt goes to `--qt-prefix` / `$GLANCELOT_QT_PREFIX` (default `~/Qt`). Skipped when `Qt6Config`, `Qt6Quick`, `Qt6ShaderTools` configs, `libqwebp.so`, `libqtiff.so`, `libqtga.so` and ICU are all present. aqt runs in its own venv (`$GLANCELOT_AQT_VENV`, default `~/.cache/glancelot/aqt-3.3.0`). `--qt-base URL` passes a mirror to `aqt --base`.
- CTest: `-L fast` when `ctest --print-labels` lists `fast` (EN-03), otherwise the full suite; anything after `--` replaces the selection. Exit status is CTest's.
- On a failed Qt download it names the log and the network-policy fix and exits 1.

## Evidence (actual output, this session)

Container: Ubuntu 24.04.5 LTS, 4 vCPU, root, no Qt installed, Docker client 29.9.1 without a daemon.

First run, fresh container (`time scripts/linux-bootstrap.sh --build-dir build-claude-en-02 -- --output-junit ...`; this first version probed Docker at the end and so never reached the probe). Apt installed 11 missing packages (24 "Setting up" lines including dependencies), then aqt was refused:

```text
==> System packages (Ubuntu 24.04)
installing: python3-venv libgl1-mesa-dev libxkbcommon-dev libxcb-cursor0 libxcb-icccm4 libxcb-keysyms1 libxcb-xinerama0 libxcb-image0 libxcb-render-util0 libxkbcommon-x11-0 libegl1
Get:1 http://security.ubuntu.com/ubuntu noble-security InRelease [126 kB]
...
Processing triggers for libc-bin (2.39-0ubuntu8.9) ...

==> Qt 6.8.3 linux_gcc_64 -> /root/Qt/6.8.3/gcc_64
INFO    : aqtinstall(aqt) v3.3.0 on Python 3.13.16 [CPython GCC 13.3.0]
WARNING : Failed to download checksum for the file 'online/qtsdkrepository/linux_x64/desktop/qt6_683/qt6_683/Updates.xml'. This may happen on unofficial mirrors.
WARNING : Connection to 'https://download.qt.io' failed. Retrying with fallback 'https://mirrors.dotsrc.org/qtproject/'.
WARNING : Failed to download checksum for the file 'online/qtsdkrepository/linux_x64/desktop/qt6_683/qt6_683/Updates.xml'. This may happen on unofficial mirrors.
ERROR   : Failure to connect to https://mirrors.dotsrc.org/qtproject/online/qtsdkrepository/linux_x64/desktop/qt6_683/qt6_683/Updates.xml: ProxyError

real	1m17.214s
exit=1
```

Second run, final script (`time scripts/linux-bootstrap.sh --build-dir build-claude-en-02`): Docker probe, apt idempotence, clear failure message:

```text
==> Docker probe
docker: client present, daemon unreachable
gate path: offscreen CTest here only; the Docker build runs in lead-windows-gate (authoritative). See docs/CI-GATE-RUNNER.md.

==> System packages (Ubuntu 24.04)
all 22 packages already installed

==> Qt 6.8.3 linux_gcc_64 -> /root/Qt/6.8.3/gcc_64
INFO    : aqtinstall(aqt) v3.3.0 on Python 3.13.16 [CPython GCC 13.3.0]
WARNING : Failed to download checksum for the file 'online/qtsdkrepository/linux_x64/desktop/qt6_683/qt6_683/Updates.xml'. This may happen on unofficial mirrors.
WARNING : Connection to 'https://download.qt.io' failed. Retrying with fallback 'https://ftp.yz.yamagata-u.ac.jp/pub/qtproject/'.
WARNING : Failed to download checksum for the file 'online/qtsdkrepository/linux_x64/desktop/qt6_683/qt6_683/Updates.xml'. This may happen on unofficial mirrors.
ERROR   : Failure to connect to https://ftp.yz.yamagata-u.ac.jp/pub/qtproject/online/qtsdkrepository/linux_x64/desktop/qt6_683/qt6_683/Updates.xml: ProxyError
Qt download failed (log: /root/Qt/aqtinstall.log). aqt needs HTTPS access to download.qt.io or a mirror
given with --qt-base. A Claude Code cloud environment whose network policy denies download.qt.io needs it
under the environment's Allowed domains; a session started after that change can rerun this script.

real	0m57.673s
exit=1
```

Direct probe of the host:

```text
$ curl -sS -m 10 -o /dev/null -w "download.qt.io: %{http_code}\n" https://download.qt.io/
curl: (56) CONNECT tunnel failed, response 403
[agent-proxy] download.qt.io:443 — connect_rejected (... most often means that the host is not allowed for this session)
```

`bash -n scripts/linux-bootstrap.sh` passes; `--help` prints the option table. ShellCheck is not installed here.

## Skip to pass with qtimageformats (expected, unverified)

The only format-gated skip in the suite is `animation` `testMultipageTiffNavigation` (`tests/animation_tests.cpp:236`, skips when `QImageReader` lacks `tiff`). The format, thumbnail and grid tests contain no plugin-gated skips (`grep -rn QSKIP tests` and `grep supportedImageFormats tests`), so with `qtimageformats` installed that one case should change from SKIP to PASS and nothing else should change state. Confirm from the Docker build log in the gate (`animation` output: `PASS   : AnimationTests::testMultipageTiffNavigation()`).

## Unverified

- Qt install, configure, build and CTest through the script (blocked: network policy). Rerun `scripts/linux-bootstrap.sh` in a session that can reach `download.qt.io`; the reviewer or lead should record that output.
- The Dockerfile change, the new runtime plugin check and the Docker time delta: no Docker daemon here. The gate's `docker build` decides. If an official Qt image-format plugin needed a system library the runtime image lacks, the new check fails the build by naming it, which is intended.
- The skip-to-pass change above.
