# FS-WATCH-01 implementation evidence

Status: implementing, not independently approved or integrated. Frozen contract
c063236; assigned codex/fs-watch-01, .worktrees/codex-fs-watch-01,
build-codex-fs-watch-01. The all-Codex lane goal remains active.

## Implementation and bounds

Private fs_monitor.hpp owns one worker thread. All watch registration, source probes,
identity scans and rearming occur there. Logical registrations are current directory,
parent and current file (at most three). Scope/request state is one latest mailbox;
GUI publication is one coalesced latest value; controller navigation retains one
newest pending observation. Scope and monotonic observation revision reject old
results. Publication coalescing retains the earliest unprocessed baseline, so a busy
GUI cannot lose a rename by receiving only a later delta. Snapshot values have no
linked history and share immutable storage.

Native changes debounce 120ms with maximum event delay500ms (plus actual filesystem
probe time). Missed notifications use periodic recovery. Healthy fallback2s;
failed/partial registration or scans back off to30s; delay starts after completion
so an expensive scan cannot become a continuously busy retry loop. F5 bypasses
backoff. Qt cannot report every native overflow: deterministic lost-notification
and registration gates test the same recovery path; no native overflow guarantee
is claimed. Native gate/probe injection exists only in the private helper interface.

Each monitor snapshot stops at20,000 entries or a conservative16MiB path/key/node
budget. An incomplete snapshot proves no rename/deletion. The grid keeps its existing
100,000-entry listing budget; its extra native-identity map also stops at16MiB and
refuses identity matching if incomplete. File probes are independent of membership
scans, so current-file replacement and F5 remain useful beyond the monitor prefix.
Partial membership observations request the existing async grid refresh, retain
recoverable selection, and expose the existing error route. No recursive watches,
per-delegate handles, public header, catalog or original-media writes are added.

Native identity is volume/device plus file ID/inode and creation epoch. Linux uses
statx birth time when available; a filesystem without it refuses rename inference.
Hard-linked files, reparse points, duplicate identities, unknown probes and partial
snapshots refuse inference. Equal size/mtime is never rename evidence. Proven
renames preserve current asset and selected noncurrent paths, anchor and cursor;
a proven directory rename maps entries only after a worker identity check. Windows
case-only spelling updates preserve navigation. Actual deletion retains the existing
deleted-current anchor; local Trash plus its notification cannot advance twice.

An explicit decode normally begins after the monitor baseline/registration. A100ms
fallback still opens a new local file if an earlier watch probe is blocked; a late
baseline validates it with one fresh decode. Decode generations prevent stale pixels
from replacing a newer selection. New paths never queue unbounded watch commands.
Monitor close waits at most250ms; abandoned blocked work owns shared/by-value state,
never a controller pointer. Existing navigation shutdown policy remains unchanged.

## Checks so far (not candidate approval)

New native feature suite passed in6.11s initially,7.89s after baseline handling,
11.69s after injected stale/storm/blocked work,12.47s including empty-folder and
folder-rename recovery, and20.88s after established-watch loss/rearm and continuous
native storm tests. These runs precede the last case-only, backoff and byte-budget
changes; final-source checks are still required. Generated media only. Source hashes
are checked for replacement/rename/viewing. Queue/registered-path bounds and close
latency are asserted, not inferred from a screenshot.

Initial Docker19 suite failed folder_tree (background watch health erased an explicit
failed address error), folder_grid and directory_model (older rename-selection
expectations), plus a transient-failure row-policy regression. Error ownership was
fixed, the original inaccessible-row behavior retained, and only the two existing
rename-selection expectations updated to preserve the proven renamed item while
still dropping deleted items. New feature tests remain in fs_watch_tests.cpp and
fs_watch_ui_tests.cpp; no new test function was added to grid_ui_tests.cpp.
A later model check found the same-directory-refresh selected-count assertion also
needed the new expected list; its exact path assertions remain. Existing performance
assertions remain: a direct model run recorded worst GUI gaps32/48ms and a12,001-row
listing573ms with11ms worst GUI gap. Those observations are uncontrolled and are not
hardware qualification or final-source performance evidence.

A Windows compile caught Win32 small macro leakage from a native probe helper
included in an older UI test. The old tests now use QFileInfo birth-time capability;
the native helper remains private to implementation and the new feature tests.

## Routing contract and identity race checks

Lead froze the narrow routing correction and mandatory real-QML target on main
525336d, merged into this branch707331f. Product QML5812d75 adds only refresh to
gridAllowsCommand; no second dispatcher or precedence change. Windows build passed,
then ctest --test-dir build-codex-fs-watch-01 -R '^fs_watch(_ui)?$'
--output-on-failure passed2/2 in35.20s (backend31.79s, UI3.33s). The real-QML
harness pressed F5 in preview/grid/combined and checked text/settings suppression.
Logs: .cache/fs-watch-routing-{build,tests}.log. These checks precede the following
identity changes and do not constitute final-candidate gates.

A new generated-media regression reproduced a proven rename immediately followed
by old-path reuse: the viewer stayed on current.png instead of renamed.png,
2 passed/1 failed in37.736s, .cache/fs-watch-path-reuse-before.txt. Reconciliation
now follows the original identity even if another file/directory reuses its name.
The same test passed3/3 in0.865s after the fix, with original red pixels, selected
identity and source hash retained (.cache/fs-watch-path-reuse-after.txt).

Rename matching now runs on the monitor worker, outside the publication mutex.
Only the two needed destination paths are published. A controller acknowledges its
baseline after processing, preserving the original identity while navigation defers
multiple observations. Separate new tests cover this deferred acknowledgement and
a renamed directory whose old pathname is reused. Directory membership snapshots
and model identity scans compare directory revisions around enumeration; a change
makes identity inference incomplete. These new cases await the full gates below.

## Remaining gates

Final Windows build.ps1 -Test, full root Docker build, actual Windows/Linux events,
failure/cancellation outputs, independent Claude review and integrated-commit checks
remain. Native network shares, other filesystems, physical GPUs/DPI/ICC/HDR and
hardware performance stay unverified unless measured. Viewing never writes originals.

A final source-race inspection added last-good navigation retention when a scan
fails and preserves an intact widened root during the non-anchored recovery path.
A new generated-media test covers failed source plus F5 recovery under that root.
These changes are pending final-source checks alongside the byte/backoff/case-only
changes; no additional pass is claimed.

Required native command scripts/build.ps1 -BuildDir build-codex-fs-watch-01 -Test
on source b4c9b04 FAILED18/20,346.02s. fs_watch passed33.44s, including the
old-file/directory-path reuse and deferred-baseline cases; fs_watch_ui passed2.13s.
The unchanged folder_grid regression suite passed177.00s. directory_model failed
33.23s without Qt assertions in CTest output; platform_integration timed out30.06s.
Log: .cache/fs-watch-final-windows.log. No disabled test, relaxed threshold or
passing full native gate is claimed.

Bounded direct diagnostic reruns captured explicit Qt logs, with the same built
source and unchanged assertions. directory_model failed18 passed/2 failed23.437s:
interleaved refresh gap889ms and large-folder gap330ms against250ms, consistent
with the unresolved integrated PLATFORM gate but not proof of an identical cause.
platform_integration passed8/8 in22.761s after its earlier30s timeout; cause remains
unestablished. Logs .cache/fs-watch-{directory_model,platform_integration}-diagnostic.txt
and fs-watch-native-diagnostic-summary.txt. Other lanes' builds/native suites were
observed during this interval; do not call it an isolated performance run.

The required docker build -t glancelot-browser:main . passed20/20,139.50s on
source b4c9b04 through WSL Ubuntu; log .cache/fs-watch-final-docker.log. Backend
fs_watch passed29.97s and real-QML fs_watch_ui1.17s; directory_model3.07s and
platform_integration0.47s passed. Runtime image is
sha256:d47dc10b08af1739431e538d53ed22c0c90f4fa3a8b89a33d8166afc2e6a3f37.
A generated-file probe in that exact network-disabled image verified nonzero native
birth identity, unchanged identity/hash across rename and a distinct replacement
at the old pathname (.cache/fs-watch-linux-identity-capability.json). This checks
the container filesystem capability; it does not qualify other filesystems.

## Reviewed decoder prerequisite

Lead froze main5b91249 permitting exact Claude-authored a6bda32 after independent
Codex scoped approval db23447. Imported unchanged as615c0b5, with the original
author preserved; INPUT UI remains CHANGES REQUIRED. The long-held worker semaphore
was independently shown to hang parent detach after a witnessed partial-copy kill;
the fixed worker passed complete64MiB RGBA/ack and five qualifying kills in13-17ms.
The unchanged report is DECODER-CANCEL-INDEPENDENT-CODEX-REVIEW.md.

New whole-controller generated-media coverage cancels a confirmed live decoder for
a4096x4096 image at several delays, opens a blue final image, checks bounded cancel
return/stale-result rejection and unchanged source hashes. This controller test is
not claimed to pinpoint a mid-copy kill; the independent host supplies that proof.
The earlier full gates above precede this prerequisite and new regression. Full
Windows and Docker checks must run again on the resulting source; no new pass is
claimed yet.

## New-source native attempt stopped for serialization

Source b9fa03b (reviewed worker prerequisite plus whole-controller regression)
built successfully. Its required full Windows run passed tests1-9, then was
intentionally stopped during folder_grid after process inventory confirmed a
concurrent Claude F014 full CTest/grid suite. Both use the system clipboard; this
partial attempt cannot qualify a native gate. Only the owned FS-WATCH CTest tree
was terminated, and follow-up inventory found no owned test remaining. Other lanes
were not stopped or edited. Logs .cache/fs-watch-worker-windows.log and
fs-watch-worker-windows-cancel-reason.txt preserve the attempt.

The imported worker's actual SHA256 matches the independently compiled candidate
df2390c99477a005240cb14d3c1fa3f075e17df8b9e1e027ccf2f1bfa0c9c650.
Wait for a verified free native slot before a fresh full Windows gate; rerun Docker
on this new source too. These gates, whole-controller cancellation result and
independent Claude FS-WATCH review remain pending. The goal and packet stay open.

## Current-source Linux gate, October19

After importing615c0b5 and adding the whole-controller regression b9fa03b, required
docker build -t glancelot-browser:main . passed20/20 in144.99s; fs_watch31.43s
and fs_watch_ui1.14s. This includes the new live large-decode cancellation and
explicit-open/hash checks. Image733559bd1d8fc078d006b92c112680e6c2cc340da3c1390f238e1beff257134e;
log .cache/fs-watch-worker-docker.log. Actual source is unchanged by evidence-only
commits. A newly serialized full Windows gate is running with lead-owned native
slot markers; no result is claimed yet. Independent FS-WATCH review remains pending.

## Serialized Windows gate on current product source

The serialized full scripts/build.ps1 -Test run on product source b9fa03b
completed18/20 in506.81s. fs_watch passed37.85s and fs_watch_ui1.59s;
folder_grid passed273.25s within its unchanged300s timeout. directory_model
failed47.14s without Qt stdout and platform_integration timed out29.97s.
Log .cache/fs-watch-worker-windows-serial.log preserves actual command output.
No concurrent full native suite was observed during this run. These results
supersede the earlier running status; they do not qualify the full Windows gate.
The independently assessed Windows launcher fixture prerequisite is now being
implemented under the frozen PLATFORM contract. Production adapter bytes,
performance assertions and test timeouts remain unchanged.

## Windows launcher fixture and post-environment diagnostics

Test-only source038d1b5 keeps production platform adapter/header unchanged.
The helper PE subsystem is2 (GUI). Production adapter SHA256 is
AFF462DAD30C599138E54653FBD3FFEC97A9052ACF5AE5BD31B90819FA7861A9.
Private copied runner/stubs retain Unicode/non-BMP application directory, CWD,
media and argv; validated same-directory short PATH aliases accommodate Qt's
narrow default executable search. Process creation time distinguishes helper PID
reuse while its process object exists; exact capture module/hash/token are asserted. COM balance,
Qt-module failure diagnostics and bounded job accounting were strengthened after
independent Claude review56d27cb requested changes. Re-review is pending.

The deliberate invalid-PE failure initially hung under CTest until the22s owned
runner deadline. Private runner SetErrorMode suppresses loader error UI for this
intentional negative test. Default QtTest output uses a bounded relative file in
the Unicode CWD and is replayed to CTest; explicit log arguments remain available.
Required scripts/build.ps1 -BuildDir build-codex-fs-watch-01 build passed;
ctest --test-dir build-codex-fs-watch-01 -R '^platform_integration$'
--output-on-failure passed1/1,0.70s (total0.75s). Direct logged run passed9/9,
672ms. Logs .cache/native-fixture-{loader-build,loader-ctest,process-detail}.*.
No timeout or performance oracle changed. Parent termination can leave inert
private TEMP files; the owned kill-on-close job bounds live child lifetime.

User authorized closing the57 saved Explorer folder windows. At execution none
remained in Shell.Application.Windows, so none were closed and no processes were
killed. Explorer footprint had already fallen to17 processes/about1.7GB from the
earlier58/about8.4GB. Inventory and saved locations stay private and untracked.
A serialized direct directory_model run then passed20/20,26.040s with worst GUI
gaps31/33/11ms against250ms. Log .cache/directory-model-post-explorer.txt.
This changed environment is not a causal proof. A fresh required full Windows
suite is now running; no full gate or independent FS-WATCH approval is claimed.

The fresh full Windows attempt was stopped during folder_grid after confirming
another lane's native INPUT grid process30056. Only owned CTest47176 and its
child tree were stopped; no other lane job was stopped. Log
.cache/fs-watch-fixture-full-windows.log and its cancel note preserve the attempt.
This partial run does not qualify the gate. Independent Claude static re-review
8594e86 found no blocking defect, conditional on its independent runtime check.
Final diagnostic polish cd25573 fixes eager diagnostic construction, retains a
bounded prefix of oversized failure logs and reports ordinary child exits clearly.
The reviewer has an isolated PLATFORM-only candidate b4fa53e and permission to run
its own bounded target when the native slot is verified free. Full Windows and
Docker gates remain pending; no FS-WATCH approval or integrated pass is claimed.

Helper exit limitation: startDetached closes its handles. If the process object
has already disappeared, a committed capture plus empty owned-job accounting
shows completion; an exit code was not read in that case. Otherwise process
creation identity and zero exit are checked. The fixture requires a valid ASCII
8.3 alias of its exact private sandbox volume/directory; absence fails code110,
not a skip. These limitations are explicitly accepted and do not qualify visible
Shell presentation or the physical hardware matrix.

## Corrective candidate and local metadata cost, October 19

Actual Claude static reviews 0dafc6c and bb3d271 requested changes; neither
approved FS-WATCH. Candidate b4cfd20 preserves the first-scope marker through
worker/controller publication coalescing, retains complete monitor baselines
through partial observations, and distinguishes an alias entry from its resolved
target when deciding whether directory selection should follow a rename.
Targeted Windows regressions passed 12/12, zero skips, in 6.373s after an actual
red run (5 failures, 4 passes, 4.494s). These include first unacknowledged image
and directory renames, four partial-baseline cases, coalesced publication, native
junction watch coverage, steady polling, selected rename/hash/pixels, ordinary
alias-to-target navigation reset and dangling targets. Logs remain under ignored
.cache/fs-watch-corrective-{red,green}.txt. Full current-source checks and actual
Claude independent runtime approval remain pending.

A generated local folder of 20,000 one-byte files measured the actual unchanged
worker functions on b4cfd20: glancelot::listDirectory (limit 100,000), the model's
buildListing, and fs_watch::snapshot (limit 20,000). Three rounds alternated
forward/reverse/forward method order. Every result had exactly 20,000 rows and a
complete identity observation. Bare listing took 64.74-81.91ms, model listing
496.64-587.06ms, and watcher snapshots 379.53-442.54ms. File generation took
6,459ms and is excluded. Exact samples and conditions are in
[fs-watch-01/local-cost.json](fs-watch-01/local-cost.json).

The ignored diagnostic compiled the actual directory_entry_model.cpp and private
revision helper with Qt 6.8.3/MSVC Release and the branch's glancelot_core library;
no product seam or timing assertion changed. Elapsed time includes constructing
and destroying the worker results, but excludes GUI publication and decoding.
Cache state was uncontrolled after file generation; no cache flush was attempted.
No other native test or Linux build job was observed before starting the probe.
This is evidence of material per-entry metadata overhead, not a first-paint,
network-share, ReFS/OneDrive or production workload qualification. The existing
250ms GUI responsiveness gate still applies. Actual Claude R3 is independently
reviewing fixed candidate b4cfd20; its disposition is not claimed here.
