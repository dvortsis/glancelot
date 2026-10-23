# EN-02 Linux bootstrap and qtimageformats parity: review round 2

Reviewer: fresh-context Claude Code (Opus) re-reviewer on the lead's Windows PC; did not implement EN-02 and did not do round 1. October 22, 2026.

| Item | Value |
| --- | --- |
| Branch reviewed | `origin/claude/en-02-linux-bootstrap` at `dffc799` ("docs(EN-02): annotate round 1 check headers; CRLF note") |
| Base | `afeac1a` (contract freeze); branch merges `origin/main` at `5b21b29` |
| Diff reviewed | `git diff afeac1a origin/claude/en-02-linux-bootstrap` (packet files: `scripts/linux-bootstrap.sh`, `Dockerfile`, `BUILDING.md`, `docs/handoff/EN-02-HANDOFF.md`; the rest arrives with the merge of `main`). The two-dot diff against current `origin/main` also shows deletions of `docs/verification/gate-triage-2026-10-22/**` and a `WORK-QUEUE.yaml` hunk: those exist only because `main` moved on to `3893c77`; they are not branch changes. |
| Host | Windows 11, WSL Ubuntu 24.04.3, Docker Engine 28.4.0 (Docker Desktop), 12 cores |
| Gate lock | Every heavy step ran inside `scripts/gate-lock.ps1 -Branch review/en-02 -Run { ... }` |

## Verdict: APPROVED

All five round-1 findings and the QSKIP proof point are resolved and proven with real output below. `testMultipageTiffNavigation` now PASSES in both the script-built tree and the Docker gate (it SKIPs in an older pre-EN-02 build image). The Docker gate passes 43/43 with the new runtime plugin check. No blocking findings. Two lead actions remain outside this branch (L1, L2), and the cloud-session proof stays open as a Phase 0 exit item.

## Round 1 findings

| # | Finding | Status | Evidence |
| --- | --- | --- | --- |
| 1 | Relative `--qt-prefix` doubled the path | Resolved | Fresh container, `--qt-prefix qt-rel` from `/src`: step header `Qt 6.8.3 linux_gcc_64 -> /src/qt-rel/6.8.3/gcc_64`; `find /src/qt-rel -maxdepth 3 -type d` shows only `qt-rel/6.8.3/gcc_64/...`, no nested `qt-rel/src/qt-rel` (proof 1) |
| 2 | Missing option values | Resolved | Rerun here: `--qt-prefix`, `--qt-prefix --debug`, `--build-dir ''`, `--jobs` and `--qt-base --skip-apt` each print `<option> needs a value` and exit 2 (proof 2); valid values parsed correctly in every run here |
| 3 | `grep -q` under `pipefail` | Resolved | Both checks use here-strings. dpkg check: second run prints `all 22 packages already installed`. Label check: no `fast` label exists yet at this base, and the explicit `--` selection was honoured. `aqt_venv_ok`'s three conditions verified against the real venv (proof 2) |
| 4 | Unclear Docker-probe message | Resolved | Both branches seen: `docker: not installed` + offscreen-only gate path (container), and `docker: daemon reachable (28.4.0)` + advisory-only / gate-lock / authoritative-gate text (WSL host) |
| 5 | Dockerfile check did not name the failed plugin | Resolved | Messages name the missing path or the plugin with unresolved libraries (code read; `sh -n` passes). Only the passing path is exercised here: `#34 [runtime 11/12] ... DONE 0.3s` |
| 6 | "Skip to pass" cannot be shown by `ctest --output-on-failure` | Resolved | Shown from QtTest output, not CTest status: direct run `PASS   : AnimationTests::testMultipageTiffNavigation()`, results file from the scripted CTest run, and the Docker build stage's `/build/animation-results.txt` all show PASS with `0 skipped`; pre-EN-02 image shows `SKIP` (proofs 2 and 3) |

## New findings

None blocking.

- **L1 (lead, non-blocking for this branch): hosted Windows job.** The plan row (`LEAD-PLAN-2026-10-22.md` section 3) says "Add `qtimageformats` to ... the hosted job" and lists `windows-gate.yml`. The branch leaves `.github/workflows/windows-gate.yml` unchanged (`-m qtshadertools --archives qtbase qtdeclarative qtshadertools`, cache key without image formats). The queue row's outcome and acceptance and the S2 file grant in `PHASE0-CYCLE1-2026-10-22.md` do not include the workflow, and AGENTS.md reserves root configuration for the lead, so this is the lead's edit, not the packet's. The handoff gives the exact aqt line and a new cache key; both look right. Until the lead applies them, `hosted-windows` still lacks WebP/TIFF/TGA. Record the hosted-job change as a lead follow-up when integrating EN-02.
- **L2 (lead, Phase 0 exit): cloud-session proof.** The acceptance "Script proven in a fresh cloud session" is not met by this review; a throwaway `ubuntu:24.04` container on this PC is the closest equivalent and passed end to end. A cloud session still needs `download.qt.io` allowed (handoff, "Cloud network policy") and should rerun the script once.
- **N1 (minor, optional).** `--help` says the build-dir default is `./build`, but the code uses `$ROOT/build` (repository root, whatever the caller's directory), while a relative `--build-dir` resolves against the caller's directory. Both behaviours are reasonable; the help text should say `<repo>/build`.
- **N2 (minor, optional).** The first aqt call prints `qmake: error while loading shared libraries: libicui18n.so.73` because ICU is installed by the second call. It is harmless (the Dockerfile does the same) but looks like a failure in a fresh session; installing ICU first, or one line in BUILDING.md, would remove the scare.
- **N3 (observation).** A fresh run with the default `--jobs 4` took 22.9 min on 12 cores, almost all compile. Defaulting to `nproc` would roughly halve it; keeping 4 matches the Dockerfile and is defensible on small cloud VMs.
- **Library parity.** `readelf -d` on all eight image-format plugins lists only `libQt6Gui`, `libQt6Core`, `libGL.so.1`, `libxkbcommon.so.0`, `libz.so.1` and the C/C++ runtime. The runtime stage provides `libgl1` and `libxkbcommon-x11-0` (which pulls `libxkbcommon0`); `libz.so.1` is in the base image. The new `ldd` check confirms none is unresolved. No package change is needed.

## Proof 1: fresh-session equivalent (throwaway container, relative prefix)

WSL-native clone of the branch at `~/glancelot-en02` (`dffc799`, `scripts/linux-bootstrap.sh` mode 100755, LF). Named throwaway container so the rerun sees the same state; removed afterwards.

```sh
docker run -d --name en02-review -v "$HOME/glancelot-en02:/src" -w /src ubuntu:24.04 sleep infinity
docker exec -w /src en02-review bash -c "scripts/linux-bootstrap.sh --qt-prefix qt-rel --build-dir build-c -- -R '^(format|thumbnails|animation)\$'"
```

Container: `Ubuntu 24.04.5 LTS`, 12 CPUs, uid 0, no sudo, no docker, no Qt.

Run 1 (actual output, apt and compile lines elided):

```text
=== RUN 1 2026-10-22T20:22:10-06:00

==> Docker probe
docker: not installed
gate path: offscreen CTest here only; the Docker build runs in lead-windows-gate (authoritative). See docs/CI-GATE-RUNNER.md.

==> System packages (Ubuntu 24.04)
installing: ca-certificates python3 python3-venv build-essential cmake ninja-build libgl1-mesa-dev libgl1-mesa-dri libxkbcommon-dev libfontconfig1 libdbus-1-3 libxcb-cursor0 libxcb-icccm4 libxcb-keysyms1 libxcb-shape0 libxcb-xinerama0 libxcb-image0 libxcb-render-util0 libxkbcommon-x11-0 libegl1 libglib2.0-0t64 xvfb
Get:1 http://archive.ubuntu.com/ubuntu noble InRelease [256 kB]
...
1 upgraded, 196 newly installed, 0 to remove and 3 not upgraded.
...
Fetched 170 MB in 11s (15.5 MB/s)
...

==> Qt 6.8.3 linux_gcc_64 -> /src/qt-rel/6.8.3/gcc_64
INFO    : aqtinstall(aqt) v3.3.0 on Python 3.12.3 [CPython GCC 13.3.0]
INFO    : Found extension qtwebengine
INFO    : Found extension qtpdf
INFO    : Downloading qtshadertools...
INFO    : Downloading qtimageformats...
INFO    : Downloading qtbase...
INFO    : Downloading qtdeclarative...
INFO    : Redirected: qt.mirror.constant.com
...
INFO    : Finished installation of qtimageformats-Linux-RHEL_8_10-GCC-Linux-RHEL_8_10-X86_64.7z in 1.71943709
INFO    : Finished installation of qtshadertools-Linux-RHEL_8_10-GCC-Linux-RHEL_8_10-X86_64.7z in 1.98987545
INFO    : Finished installation of qtbase-Linux-RHEL_8_10-GCC-Linux-RHEL_8_10-X86_64.7z in 9.76409818
INFO    : Finished installation of qtdeclarative-Linux-RHEL_8_10-GCC-Linux-RHEL_8_10-X86_64.7z in 26.47220578
/src/qt-rel/6.8.3/gcc_64/bin/qmake: error while loading shared libraries: libicui18n.so.73: cannot open shared object file: No such file or directory
INFO    : Patching /src/qt-rel/6.8.3/gcc_64/lib/libQt6QmlTypeRegistrar.prl
...
INFO    : Finished installation
INFO    : Time elapsed: 30.67876971 second
INFO    : aqtinstall(aqt) v3.3.0 on Python 3.12.3 [CPython GCC 13.3.0]
INFO    : Found extension qtwebengine
INFO    : Found extension qtpdf
INFO    : Downloading icu...
INFO    : Redirected: qt.mirror.constant.com
INFO    : Finished installation of icu-linux-Rhel8.6-x86_64.7z in 2.52098594
INFO    : Finished installation
INFO    : Time elapsed: 6.47527989 second

==> Configure /src/build-c (Release)
-- The CXX compiler identification is GNU 13.3.0
...
-- Build files have been written to: /src/build-c

==> Build (4 jobs)
...
[541/541] Linking CXX executable monitor_customization_ui_tests

==> CTest (offscreen): -R ^(format|thumbnails|animation)$
Internal ctest changing into directory: /src/build-c
Test project /src/build-c
    Start  8: thumbnails
1/3 Test  #8: thumbnails .......................   Passed    3.65 sec
    Start 33: animation
2/3 Test #33: animation ........................   Passed    0.37 sec
    Start 36: format
3/3 Test #36: format ...........................   Passed    0.02 sec

100% tests passed, 0 tests failed out of 3

Total Test time (real) =   4.04 sec
run1 exit=0 elapsed=1374s
```

Finding 1 directly:

```text
$ find /src/qt-rel -maxdepth 3 -type d | sort
/src/qt-rel
/src/qt-rel/6.8.3
/src/qt-rel/6.8.3/gcc_64
/src/qt-rel/6.8.3/gcc_64/bin
/src/qt-rel/6.8.3/gcc_64/doc
/src/qt-rel/6.8.3/gcc_64/include
/src/qt-rel/6.8.3/gcc_64/lib
/src/qt-rel/6.8.3/gcc_64/libexec
/src/qt-rel/6.8.3/gcc_64/metatypes
/src/qt-rel/6.8.3/gcc_64/mkspecs
/src/qt-rel/6.8.3/gcc_64/modules
/src/qt-rel/6.8.3/gcc_64/plugins
/src/qt-rel/6.8.3/gcc_64/qml
/src/qt-rel/6.8.3/gcc_64/sbom
$ ls /src/qt-rel
6.8.3
aqtinstall.log
no nested /src/qt-rel/src
```

Run 2, same container (skip paths; configure re-ran in 2.7 s and Ninja only re-checked AUTOMOC, no compiles):

```text
=== RUN 2 2026-10-22T20:45:04-06:00

==> Docker probe
docker: not installed
gate path: offscreen CTest here only; the Docker build runs in lead-windows-gate (authoritative). See docs/CI-GATE-RUNNER.md.

==> System packages (Ubuntu 24.04)
all 22 packages already installed

==> Qt 6.8.3 linux_gcc_64 -> /src/qt-rel/6.8.3/gcc_64
Qt 6.8.3 with ShaderTools, image formats and ICU already in /src/qt-rel/6.8.3/gcc_64

==> Configure /src/build-c (Release)
...
-- Configuring done (2.6s)
-- Generating done (0.1s)
-- Build files have been written to: /src/build-c

==> Build (4 jobs)
[1/182] Automatic MOC and UIC for target glancelot_recovery_common
...
[44/46] Automatic MOC and UIC for target fs_watch_tests

==> CTest (offscreen): -R ^(format|thumbnails|animation)$
...
100% tests passed, 0 tests failed out of 3

Total Test time (real) =   4.05 sec
run2 exit=0 elapsed=8s
```

Docker probe, daemon-reachable branch (WSL host, Qt already complete so nothing downloads):

```text
$ scripts/linux-bootstrap.sh --skip-apt --qt-only --qt-prefix ~/glancelot-en02/qt-rel

==> Docker probe
docker: daemon reachable (28.4.0)
gate path: 'docker build .' works here as an advisory check only. On the lead's Windows PC a local docker build
must run inside scripts/gate-lock.ps1 -Run (machine gate lock). The authoritative gate (lead-windows-gate or the
lead's local tester) runs the Docker build and decides done. See docs/CI-GATE-RUNNER.md.

==> Qt 6.8.3 linux_gcc_64 -> /home/dvort/glancelot-en02/qt-rel/6.8.3/gcc_64
Qt 6.8.3 with ShaderTools, image formats and ICU already in /home/dvort/glancelot-en02/qt-rel/6.8.3/gcc_64
exit=0
```

## Proof 2: TIFF passes, plugin libraries, aqt venv

Inside the container (targeted run, exempt from the lock):

```text
$ QT_QPA_PLATFORM=offscreen build-c/animation_tests testMultipageTiffNavigation -v1
********* Start testing of AnimationTests *********
Config: Using QtTest library 6.8.3, Qt 6.8.3 (x86_64-little_endian-lp64 shared (dynamic) release build; by GCC 10.3.1 20210422 (Red Hat 10.3.1-1)), ubuntu 24.04
INFO   : AnimationTests::initTestCase() entering
PASS   : AnimationTests::initTestCase()
INFO   : AnimationTests::testMultipageTiffNavigation() entering
PASS   : AnimationTests::testMultipageTiffNavigation()
INFO   : AnimationTests::cleanupTestCase() entering
PASS   : AnimationTests::cleanupTestCase()
Totals: 3 passed, 0 failed, 0 skipped, 0 blacklisted, 62ms
********* Finished testing of AnimationTests *********
exit=0

$ grep -E "Tiff|Totals" build-c/animation-results.txt   (written by the scripted ctest run)
PASS   : AnimationTests::testMultipageTiffNavigation()
Totals: 11 passed, 0 failed, 0 skipped, 0 blacklisted, 356ms

$ grep -HE "SKIP|Totals" build-c/format-results.txt build-c/thumbnails-results.txt
build-c/format-results.txt:Totals: 10 passed, 0 failed, 0 skipped, 0 blacklisted, 3ms
build-c/thumbnails-results.txt:Totals: 36 passed, 0 failed, 0 skipped, 0 blacklisted, 3654ms
```

(The "Detected locale C" notice Qt prints in a bare container is omitted.)

Before EN-02, for contrast: `glancelot-audit-build:latest` (a build-stage image created 2026-10-20, commit not recorded, Qt without `qtimageformats`; its imageformats directory holds only `libqgif`, `libqico`, `libqjpeg`, ...):

```text
SKIP   : AnimationTests::testMultipageTiffNavigation() TIFF format not supported by current Qt image formats plugins on this platform
Totals: 2 passed, 0 failed, 1 skipped, 0 blacklisted, 5ms
```

Plugin dependencies (`readelf -d qt-rel/6.8.3/gcc_64/plugins/imageformats/*.so | grep NEEDED`), condensed per plugin; every plugin also needs `libstdc++.so.6 libm.so.6 libgcc_s.so.1 libpthread.so.0 libc.so.6`:

```text
libqgif.so   libQt6Gui.so.6 libGL.so.1 libQt6Core.so.6 libxkbcommon.so.0
libqicns.so  libQt6Gui.so.6 libGL.so.1 libQt6Core.so.6
libqico.so   libQt6Gui.so.6 libGL.so.1 libQt6Core.so.6
libqjpeg.so  libQt6Gui.so.6 libGL.so.1 libQt6Core.so.6 libxkbcommon.so.0
libqtga.so   libQt6Gui.so.6 libGL.so.1 libQt6Core.so.6
libqtiff.so  libQt6Gui.so.6 libGL.so.1 libQt6Core.so.6 libz.so.1
libqwbmp.so  libQt6Gui.so.6 libGL.so.1 libQt6Core.so.6
libqwebp.so  libQt6Gui.so.6 libGL.so.1 libQt6Core.so.6
```

libtiff and libwebp are built into the official plugins; the only non-Qt libraries are `libGL.so.1` (runtime `libgl1`), `libxkbcommon.so.0` (pulled by runtime `libxkbcommon-x11-0`) and `libz.so.1` (base image).

Option values (WSL host; first output line and the script's own exit status):

```text
$ scripts/linux-bootstrap.sh --qt-prefix
--qt-prefix needs a value
exit=2
$ scripts/linux-bootstrap.sh --qt-prefix --debug
--qt-prefix needs a value
exit=2
$ scripts/linux-bootstrap.sh --build-dir ''
--build-dir needs a value
exit=2
$ scripts/linux-bootstrap.sh --jobs
--jobs needs a value
exit=2
$ scripts/linux-bootstrap.sh --qt-base --skip-apt
--qt-base needs a value
exit=2
```

`aqt_venv_ok` conditions against the venv the script created:

```text
-rwxr-xr-x 1 root root 239 Oct 23 02:23 /root/.cache/glancelot/aqt-3.3.0/bin/aqt
lrwxrwxrwx 1 root root   7 Oct 23 02:23 /root/.cache/glancelot/aqt-3.3.0/bin/python -> python3
aqt version exit=0
3.3.0
```

## Proof 3: Docker gate

Clean WSL clone at `dffc799` (`~/glancelot-en02-docker`, no build or Qt files), WSL `docker` (the Windows client returned an API 500 at the time).

```sh
docker build --progress=plain -t glancelot-browser:review-en02 .
```

Cached layers: base image and the `qt` stage apt layer. Rebuilt from the changed aqt line onward: aqt, ICU, glib, the `build` stage (apt, compile, CTest); the runtime stage apt layer also ran uncached.

```text
#10 [qt 3/5] RUN python3 -m venv /opt/aqt && /opt/aqt/bin/pip install --no-cache-dir aqtinstall==3.3.0 && /opt/aqt/bin/aqt install-qt linux desktop 6.8.3 linux_gcc_64 -O /opt/Qt -m qtshadertools qtimageformats --archives qtbase qtdeclarative qtshadertools qtimageformats
#10 DONE 39.2s
#11 [qt 4/5] RUN /opt/aqt/bin/aqt install-qt linux desktop 6.8.3 linux_gcc_64 -O /opt/Qt --archives icu
#11 DONE 7.3s
#9 DONE 60.9s            (runtime 2/12 apt)
#26 DONE 619.7s          (build 12/13: configure + compile, --parallel 4)
#27 484.4 100% tests passed, 0 tests failed out of 43
#27 484.4 Total Test time (real) = 443.11 sec
#27 DONE 484.4s
#33 [runtime 10/12] RUN ! LD_LIBRARY_PATH=/opt/Qt/lib ldd /opt/glancelot/glancelot | grep -q "not found" && ...
#33 DONE 0.4s
#34 [runtime 11/12] RUN for format in qwebp qtiff qtga; do test -f /opt/Qt/plugins/imageformats/lib$format.so || { echo "image-format plugin missing: /opt/Qt/plugins/imageformats/lib$format.so" >&2; exit 1; }; done && for plugin in /opt/Qt/plugins/imageformats/*.so; do if LD_LIBRARY_PATH=/opt/Qt/lib ldd "$plugin" | grep "not found"; then echo "image-format plugin has unresolved libraries (listed above): $plugin" >&2; exit 1; fi; done
#34 DONE 0.3s
#35 [runtime 12/12] WORKDIR /photos
#35 DONE 0.1s
#36 DONE 15.5s
docker build exit=0 total=1210.316196496s
review image bytes: 2195923230
```

Selected CTest lines from `#27`: `#8 thumbnails Passed 3.72 sec`, `#12 folder_grid Passed 175.97 sec`, `#33 animation Passed 0.37 sec`, `#36 format Passed 0.03 sec`.

TIFF in the gate (the `build` stage re-tagged from cache, `docker build -q --target build`, then reading the results file CTest wrote):

```text
$ docker run --rm en02-buildstage bash -c "grep -E 'Tiff|Totals' /build/animation-results.txt"
PASS   : AnimationTests::testMultipageTiffNavigation()
Totals: 11 passed, 0 failed, 0 skipped, 0 blacklisted, 358ms
```

Runtime image plugins (`/opt/Qt/plugins/imageformats`): `libqgif.so libqicns.so libqico.so libqjpeg.so libqtga.so (35 KB) libqtiff.so (515 KB) libqwbmp.so libqwebp.so (511 KB)`.

### Time and size

| Measure | Value |
| --- | --- |
| This gate, wall | 1210 s (compile 620 s, CTest step 484 s / 443 s test time, aqt 39 s, ICU 7 s) |
| Last recorded `main` gate (`ec05e42`, LEAD-PLAN section 1) | 1096 s wall, 620 s compile, 411 s tests |
| Changed step, uncached, `afeac1a` vs branch | STEP4_PLACEHOLDER |
| Image size | `glancelot-browser:review-en02` 2,195,923,230 B vs `glancelot-browser:main` 2,191,555,697 B (image `ec829ac89cbe`, built 2026-10-19, commit not recorded): +4.37 MB |

The compile step is unchanged; the wall-clock difference against the recorded gate is test-time variance (`folder_grid` alone is 176 s) and the uncached runtime apt layer, not EN-02. The EN-02 cost is the aqt step difference above plus 0.3 s for the runtime check.

## Cleanup

Removed: container `en02-review`, images `glancelot-browser:review-en02`, `en02-buildstage`, `en02-qt-main`, `en02-qt-branch`, and the WSL clones `~/glancelot-en02` and `~/glancelot-en02-docker`. Raw logs stay in WSL `~/en02-review/` (`step1.log` to `step4.log`, `baseline.log`).
