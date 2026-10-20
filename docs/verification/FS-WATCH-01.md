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

## R3 corrective implementation on synchronized main

Actual Claude R3 report ecb9d54 is preserved unchanged here. It requested changes
without a new blocker. Lead contract f4c7cbe freezes incomplete-scope relisting,
alias rearming/selection policy and deterministic backend-test-only hooks. Merge
b0593b8 synchronizes main's Favorites, UI settings and approved PLATFORM source;
additive command/test-target conflicts retained both sides. This is a feature
branch synchronization, not FS-WATCH integration or approval.

Corrections prefer complete baselines during controller pending coalescing,
invalidate permanently incomplete listings after the first observation with the
existing backoff, rebind native alias/image watches when their target or link
entry changes, and reset selection on explicit ancestor-alias navigation while
preserving proven real directory renames. Private scan and controller hooks are
compiled only with GLANCELOT_FS_WATCH_TESTING in fs_watch_tests. Product viewer,
decoder and real-QML binaries do not contain these hooks.

Required build.ps1 build succeeded after synchronization. New deterministic
Windows regressions passed10/10,0 failed,0 skipped in15.682s: controller pending
partial-to-complete coalescing, ancestor-alias navigation, permanent incomplete
scope automatic invalidation without F5, A-B and A-B-A scan-boundary retarget,
native retarget delivery within1.5s (below2s polling), and actual late/coalesced
baseline fresh pixels with exactly one re-decode plus newer-open precedence.
Logs .cache/fs-watch-r4-{build,incremental-build}.log and r4-targeted.txt preserve
commands/output. No competing native test was observed during this targeted run.
Full Windows/Linux gates and independent corrective review remain pending.
Coarse-timestamp identity collisions and physical/network/cloud variants remain
unverified; this run does not establish their behavior.

## R4 independent findings and R5 corrections

Actual Claude R4 a19a431 found no blocking source defect and confirmed N8-N11
fixed. Its preserved report still withholds approval pending runtime/full gates.
On fixed9177b15 the complete Docker build passed21/21 in163.94s (fs_watch50.66s,
fs_watch_ui1.09s), imagea83da558b936d0a96ae4d236e0249646ad6927ceccaf5650b76cbdcf2f21bd49.
Log .cache/fs-watch-r4-full-docker.log; terminal exit0. The native wait coordinator
ended9 after five minutes with the slot occupied; no full Windows gate started.
The Docker result predates the following corrections and cannot qualify them.

N12 now resolves the former directory spelling before declaring that it no
longer denotes the renamed object. Grid selection tests cover a missing old
name, a different object reusing it, and an old-name alias pointing to the moved
object. N14 separates subsequent-scan evidence from the OR-merged first-baseline
marker, so an initial and later incomplete scan coalesced in the controller still
invalidate listings. New native rearm rows cover image and parent watches as well
as the directory itself. Full build passed. Focused Windows tests passed9/9,
zero skips,6.151s; exact output is fs-watch-01/windows-r5-targeted.txt.

The new actual controller/model100,000-entry test passed3/3 (including init and
cleanup), zero skips,68.017s. It generated files in40.901s before starting its
GUI timer, then recorded12ms worst gap during initial publication and140ms during
271 atomic replacements over8s plus final listing recovery,11 invalidations.
It checked an actual metadata publication,100,000 retained rows, current image
pixels/path and unchanged original PNG hash. Both gaps satisfy the existing
strictly-less-than250ms oracle. Writes use a separate worker thread and real
QSaveFile atomic replacements in generated TEMP media, without injected events.
Exact output is fs-watch-01/windows-r5-100k-stress.txt. Concurrent native jobs from
other lanes were observed; these are filtered filesystem backend results, not an
isolated benchmark, full Windows gate or physical hardware qualification.

Current-source metadata measurement also covered20,000 entries in the scope and
20,000 in its parent (39,999 one-byte files plus the scope directory). Three
forward/reverse/forward rounds measured bare scope131.98-172.69ms, bare scope and
parent217.68-260.11ms, actual model651.52-803.44ms, watch scope559.62-685.49ms and
watch scope plus parent1126.01-1612.92ms. Generation16.347s is excluded; each sample
had its exact expected row count and complete identities. Exact data and limits
are in fs-watch-01/local-cost-scope-and-parent.json. The ignored diagnostic
compiles actual directory_entry_model.cpp and fs_revision.hpp unchanged, plus the
branch core library; worker construction/destruction only, no GUI/decoder, warm
uncontrolled file cache, one local volume, no randomization or cache flush. Other
native jobs were active. This confirms material background I/O cost; it is not
proof of isolated latency, acceptable power use or network performance. Poll delay
is measured from scan completion, so a slow scan does not create a continuous loop.
Independent runtime repetition and current-source full gates remain required.

Residual R4 N13 registration ABA/coarse identity cases can degrade native latency
while bounded polling still recovers membership; physical/coarse-timestamp,
network/ReFS/cloud behavior remains unverified. No native handle binding or
hardware qualification is inferred from QFileSystemWatcher's path strings. N16's
1.5s event bound remains intentionally below2s polling; a loaded-host failure is
a real failed test result, never silently converted to a pass or skipped. The
first-baseline timer/cancellation cases retain their exact decode-count checks.

## Current-source full Linux gate failure, October 19 R5

The required full docker build on fixed9bc891f finished20/21 tests passed,
236.08s; terminal exit1 (CTest8). The only failed target was folder_grid:
GridUiTests::reviewCompactCellsSnapToWholeRows expected live<=columns*8 but saw
9 delegates for1 column at300x440 (tests/grid_ui_tests.cpp:280). Its QtTest
subtotal was73 passed,1 failed,4 skipped. fs_watch passed75.34s and fs_watch_ui
passed1.45s; these narrow target passes do not qualify the complete Docker gate.
Actual output is .cache/fs-watch-r5-full-docker.log. No image/gate success is
claimed for this attempt. The earlier9177b15 image remains separately scoped.

An ignored diagnostic Dockerfile is preparing a build-stage image from the same
compiled9bc891f sources, omitting automatic CTest only so the individual failing
assertion can be reproduced. It is not a replacement gate or a product Dockerfile
change. No product UI or test bound has been changed. Independent actual ClaudeR5
backend runtime is still active in its own worktree; no report/approval yet.
The native full-suite wait coordinator ended without starting while another
lane's tests occupied the slot. Main's integration lock is currently held by
Gemini agent/filter, so no main synchronization or integration was attempted.

The unchanged diagnostic image f41fd9cb4950c98c9dbedb828a5bdf5adbef5911b43ab64670abe5826e85e488
re-ran the exact failing compact-grid case:3/3 passed, zero skips,4.827s. The
complete grid executable in isolation then passed74 cases with4 existing skips,
zero failures,105.617s. Logs .cache/fs-watch-r5-grid-{repro,full-repro}.txt. These
are diagnostic passes; the original full-gate failure is retained. A fresh full
Docker attempt on unchanged source is running, output in
.cache/fs-watch-r5-full-docker-retry.log; no result is claimed yet.

Actual Claude's independent backend run produced48 passed,1 failed,zero skips,
230.979s: the100k sustained-writer GUI gap was385ms against the unchanged250ms
oracle (initial179ms, setup124.786s,256 writes,4 invalidations). Its first isolated
rerun passed3/3,90.237s: initial12ms, sustained129ms,274 writes,13 invalidations,
setup58.204s. These independently observed mixed results preserve the performance
failure and require a clean full native gate; neither repeated passing nor a
loaded host explanation waives it. Reviewer-owned second stress rerun is active;
its final disposition is pending. Other lane native jobs remain active.

## R5 final review and measured early identity admission

Actual Claude R5 report1398061 is preserved unchanged. It withheld approval on
N17 after independent100k runs passed only1/3:385ms GUI gap,129ms isolated pass,
then final-listing recovery beyond30s. No threshold or deadline is weakened.
A small worker optimization now refuses native identity probing up front when
entryCount times the minimum per-entry storage cost already exceeds16MiB.
Every visible/listing row still publishes, identity completeness stays false,
and the last complete baseline remains protected. Native probes of an unusable
large prefix and its retained allocation are avoided; no new identity evidence
is claimed. Existing per-entry checks remain for listings that might fit.

The ignored actual-model before/after diagnostic generated34,380 local one-byte
files. Both builds published exactly34,380 rows and an incomplete identity map.
Before:32,102 retained identities,1,604ms worker time,diagnostic exit2.
After:zero retained identities,940ms worker time,exit0. Outputs are under
fs-watch-01/identity-budget-{before,after}.txt. The before diagnostic compiles the
previous committed model bytes; the after diagnostic includes actual current
source. Only the impossible-budget optimization differs; fresh generated media
and uncontrolled warm caches/concurrent compiler load mean timings are descriptive,
not an isolated speedup claim. Counts prove unchanged rows and avoided storage.

Test-only instrumentation now times the actual model publication callback,
including apply, cleanup and pump, and reports its maximum in the100k case even
on failure. It adds a strict<250ms publication assertion alongside the unchanged
heartbeat<250ms and final-listing30s checks. The hook is copied at model creation
and exists only under the backend test macro, not the product binary. Required
build.ps1 build passed. Instrumented stress is running; no result claimed yet.

Separately, the unchanged9bc891f full Docker retry passed21/21 in204.40s,
fs_watch73.86s/fs_watch_ui1.35s,terminal0, image
366bca519f794349a82349844cc256dfdf44009239435d2e26bdd3e417e5271b.
Its first failing grid run and diagnostic passes remain recorded above. The new
budget optimization and publication timing postdate that gate and need new gates.

The early-admission-only instrumented100k run on5e4fe9e passed3/3,zero skips,
102.771s: setup67.339s,initial GUI45ms,sustained226ms,267 writes,1 invalidation;
actual GUI publication callbacks2,maximum208ms,final loadingfalse. Exact output
fs-watch-01/windows-r6-100k-stress.txt. This is one filtered run, not a full gate.

Installed Qt6.8.3 qfuture.h result() returns the result reference by value,
copying Listing's std::vector of rows on the GUI thread. Its qfutureinterface.h
takeResult() instead uses move_if_noexcept and clears retained result storage.
The model has exactly one consumer, called only from QFutureWatcher::finished;
it now uses watcher->future().takeResult() and a compile-time nothrow-move assertion
for Listing. This removes the deep row copy and its duplicate deferred destruction,
without blocking on an unfinished job or changing generation/cancellation logic.
Required build.ps1 build passed, including the assertion. Full backend regression
is running; no result claimed. New source needs independent review/full gates.

Current2bd898d full backend direct run completed48 passed,1 failed,zero skips,
175.165s. The100k case passed: initial13ms,sustained184ms,263 writes,2 invalidations;
actual model publication3 callbacks,max183ms,final loadingfalse. The sole failure
was replacementAndUnrelatedChanges:generated png(image,color) returnedfalse at
line516. This is a failed complete backend run, not a pass. Exact output is
fs-watch-01/windows-r7-backend-full.txt. Test-only PNG diagnostics now identify
open/encoding/atomic-commit failure and retain immediatefailure/no retry behavior.
The same unchanged replacement operation passed in a filtered rerun3/3,zero
skips,2.940s, output fs-watch-01/windows-r7-replacement-repro.txt. No cause is yet
proven and no complete native pass is claimed. Productdecoder already uses
read/write/delete sharing and closes source before publication; no product write
or arbitrary retry was added to mask this generated-media failure.
