# FS-WATCH-01 implementation evidence

Correction4a86528: actual Claude R14 review9851f03 accepts the narrow same-key/
order metadata refresh optimization, subject fresh full/integrated gates. Exact
claude-r14-review.md is retained. Windows build succeeded. Selected metadata
regression3/3 zero skips131ms and cache10/10 zero skips4418ms passed. The unchanged
100k budget passed3/3 zero skips159381ms: setup97386ms, maximum publication152ms,
initial GUI gap60ms, sustained-writer gap154ms, active-filter GUI gap140ms and
publication139ms. This observed run meets250ms/180s; it is not a performance
guarantee under arbitrary load or a full-suite qualification.

The auxiliary direct grid diagnostic omitted CTest's QT_QUICK_BACKEND=software;
it timed out300s and recorded the directory-link navigation assertion. Preserve
windows-fast-path-grid.txt and outcomes.json as failures under that different
configuration. The selected assertion under the actual software renderer passed
3/3 zero skips4452ms (windows-fast-path-grid-software-target.txt); that does not
prove the entire grid suite or explain the earlier full-suite timeout. No
controller/QML change is made on that evidence. New full Windows/Linux and final
integrated gates remain required. No assertions, bounds or timeouts were relaxed.

Latest status: merged as2c38082 by another session, but acceptance remains open.
Main-synced2414cd6 Windows rerun failed29/32 in776.35s: grid300s timeout,
dense180s timeout and cache failure. Exact windows-main-synced-first-*.txt logs
retain the failures. Dense setup117637ms completed; measured publication maximum
358ms and GUI gap359ms exceeded the unchanged250ms limit before cleanup timeout.
Backend81/81 and UI3/3 passed; these do not make the full suite green. Another
session ran Windows and Docker verification concurrently during this run; no
host-isolation claim is made. The diagnostic refused to overlap that suite.
A narrow same-key/order refresh optimization and metadata/persistent-index
regression are now being prepared in the assigned Codex tree; neither has been
reviewed or qualified yet. Older green measurements below remain historical.

Historical status before merge: reviewed source preparing integration. Measured
source72f0afa includes the R12 L1 bookkeeping fix116dac3 and main's qualified
replacement fixture334065e. Actual Claude R13 e93ad90 accepts that narrow fix,
building on R11/R12 source review and the retained Linux startup-leak red/green
probe. Frozen contract c063236 plus committed amendments; assigned
codex/fs-watch-01, .worktrees/codex-fs-watch-01, build-codex-fs-watch-01.
The all-Codex lane goal remains active.

Latest Windows scripts/build.ps1 -Test passed32/32 in592.95s. Exact logs are
fs-watch-01/windows-qualified-platform-*.txt: backend81/81 zero skips85.505s,
100k budget3/3 zero skips166.724s, QML3/3 zero skips3.074s, replacement platform
26/26 zero skips811ms. The 100k test observed maximum GUI gap234ms and publication
callback233ms against the unchanged250ms limit. Another lane's native tests
overlapped, so no host isolation or hardware qualification is claimed.
Full Linux Docker passed32/32 in240.15s and built the runtime image. Exact
linux-qualified-platform-*.txt logs retain backend82/82 zero skips69.769s,
100k budget3/3 zero skips22.094s, and QML3/3 zero skips1.364s. The failed-start
lease case actually observed QProcess::Starting on Linux. The 100k case measured
83ms sustained-writer/recovery GUI gap and73ms maximum publication callback.
The build-stage extraction reused the successful CTest layer; it did not rerun
tests. Checks on the eventual merged commit remain pending.
Historical failed/older-source runs below do not transfer to this candidate.

The old Defender-triggering launcher fixture is permanently retired; this source
uses the separately reviewed replacement. Post-run Defender records show the
earlier retired FS and monitor helper detections, inactive/nonexecuted, with
successful actions; no replacement detection appears in the inspected records.
No exclusions/restores or false-positive/clean-machine conclusions are used.
See windows-qualified-platform-defender.txt and main's fixture security report.

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

## Filter/History/Help synchronization checks

Merge65eab86 brought committed main through a657a30 into the assigned FS branch,
retaining physical complete identity reconciliation before Filter projection,
raw-index selection pruning, temporary-filter crossing semantics and all task
commands. No frozen header was edited by this merge resolution. Main.qml differs
from that base by exactly the permitted refresh allow-list token. The full native
build passed140/140 build steps. New targeted backend compatibility cases passed
6/6,zero skips,4.039s: hidden selected rename restored after filter clear,
watched current rename retaining temporary and persistent filters, plus repeated
atomic replacement. Original Filter backend tests passed13/13,zero skips,2.433s.
Exact outputs are fs-watch-01/windows-filter-{compat,original-tests}.txt. These
are scoped backend checks, not a full native or merged-source Docker gate.

While these checks ran, main advanced to CACHE-01 header freeze f708068. The
FS branch intentionally still has the earlier header contracts, so it is not
header-identical to that newer main and these checks cannot qualify CACHE APIs.
Keep the owner implementation/final synchronization separate; do not copy new
unimplemented declarations into this tested candidate or implement another
lane's cache packet. Re-review and full gates remain required after final sync.

On fixed performance candidate2bd898d (before Filter synchronization), the full
Docker gate passed21/21,214.61s,fs_watch76.59s/fs_watch_ui2.21s,terminal0,image
9ab1f30f5ee44a90113274d90a45711ba44fb8d151d1b7631509ae874fd0ef26.
Actual Claude R6 independently built and ran the whole backend:49/49,zero skips,
137.622s,100k GUI gap180ms,publicationmax179ms. Its first isolated repeat passed
3/3,97.937s,gap181ms/publication170ms. Further repeats and its final disposition
are still pending; neither these nor old Docker results approve the new merge.

## Current contract snapshot and native overlap

Mergef384955 carried main's frozen CACHE-01 interfaces and default scaffolding
without taking its owner implementation. Shared headers are identical to that
main contract; QML delta remains only refresh. Required build passed119/119
steps. Root synchronization locks are released; no FS-WATCH integration occurred.

Actual independent Claude R6 reporte2e10cf is preserved unchanged: NOT APPROVED,
noting correct early budget admission and no-copy result ownership,49/49 whole
backend pass and3/3 passing isolated100k runs (gaps181/129/232ms; publication
170/128/232ms). It still requests per-phase GUI cost evidence/headroom, a committed
impossible-budget regression and full current-source/realF5 gates. These requests
remain open; four passing runs are not treated as independent packet approval.

The required Windows full gate on exactf384955 was started with no other full
or native-input suite observed. The old Filter20064 process was verified QtCore-
only (QTEST_GUILESS_MAIN), uses exclusively generated QTemporaryDir media and has
no live parent, so it was left untouched and did not count as an input/clipboard
conflict. While owned CTest51668/48672 and grid50896 ran, Cache started full
CTest46160/32388. Only verified owned wrapper51668 and descendants were stopped.
First9 targets had passed; stopped duringfolder_grid. Other owner Cache and the
QtCore-only orphan were not stopped. The partial attempt is not a full native
pass; logs .cache/fs-watch-current-full-windows.log and cancel-reason preserve
actual output. The user was asked to coordinate one uninterrupted5-8minute native
slot because reservations have repeatedly been overlapped. Independent regression
work can continue while that scheduling answer is pending.

## R6 review follow-up: phase timing and committed budget regression

The backend-only publication hook now reports total, identity reconciliation,
merge total (including result disposal), merge scan, reset, raw projection/mirror
and selection-pruning times. Scan/reset are subphases of merge, not additive
independent totals; allocator cleanup inside merge is included in its total.
All hooks remain under GLANCELOT_FS_WATCH_TESTING; no public header, product
setting, user flow or runtime timing behavior is added. The original250ms
heartbeat/publication limits and30s recovery deadline remain unchanged.

The existing100k stress case is now also a committed behavioral budget regression:
first establish a real complete one-entry model baseline and selection, then
generate99,999 unsupported files. Assert the full100,000 rows publish with an
incomplete identity observation, zero retained prefix identities and the previous
one-entry complete baseline still accepted. After the writer stops, remove the
generated extras, rename the selected original, and require selection/cursor and
source hash to follow that identity on the next complete listing. This uses the
public model API and real native identities, without a private/public macro,
synthetic listings or the ignored diagnostic harness. A filesystem with no native
identity explicitly skips the final rename qualification after budget assertions;
that is never counted as a pass.

Required full build passed64/64 steps. The expanded Windows case passed3/3,
zero skips,71.387s. Setup49.322s precedes GUI measurement. Initial GUI gap20ms;
writer/recovery130ms,274 atomic writes,8 invalidations. Thirteen publication
callbacks had maximum129ms. Nine100k callbacks showed merge total15-112ms,
merge scan4-72ms, projection/raw mirror4-17ms, identity/selection0ms at integer
resolution, no reset on those samples, and observed/accepted identity counts0/1.
The recovery and selected-original rename assertions passed. Exact output is
fs-watch-01/windows-r8-phases-budget.txt. Concurrent lane native jobs were present;
this is measured loaded-host evidence, not an isolated hardware benchmark or
independent review. Full current gates and independent re-review remain pending.

The committed regression was challenged by temporarily removing only the early
identity-admission block from the assigned model source, keeping all per-entry
limits, row publication and baseline-retention logic. The actual backend test
failed exactly at admittedWithoutPrefix:2 passed,1 failed,zero skips,132.510s;
observed identity prefix35,544 versus accepted complete baseline1. Heartbeat191ms
and callback179ms stayed below250ms, so this red distinguishes admission rather
than failing an unrelated timing assertion. Exact output is
fs-watch-01/windows-r8-budget-red.txt.

A PowerShell finally restored the exact green source bytes and successfully
rebuilt the backend target. A subsequent wrapper evidence check had a positional
Select-String argument error after restoration; corrected read-only checks found
the intended assertion, executable exit1 and matching source/backup SHA256
EF98B855BAC510F45CB02862D017FCD0060723F05BEF45AEDF3F740593A0FAEA.
Git confirmed no source delta from the committed green variant. The current
full backend run uses that restored green target; no result claimed yet. No
product/test deadline or limit was weakened and no temporary red source remains.

After exact green restoration, the full current backend executable passed52/52,
zero failures or skips,155.267s (direct fs_watch_tests,QT_QPA_PLATFORM=offscreen).
This includes expanded admission/baseline recovery, Filter compatibility, source
hashes, cancellation and repeated replacement. Actual output is
fs-watch-01/windows-r8-backend-full.txt. It is not the full25-target Windows suite.
The current25-target Docker build is now running; no result is claimed yet.
Actual independent Claude R7 has fixed5e2b0dc in its own new worktree, reviewing
merged contracts and both R6 follow-ups, with backend-only runtime authorized.
Native input/QML/full Windows runtime remains subject to an exclusive slot.

## Current merged-source full Linux gate

Required docker build on source candidate5e2b0dc (product/tests2b2579d) passed
25/25 targets,206.78s,terminal0. fs_watch76.11s and fs_watch_ui2.63s passed;
resulting runtime image
c1457164a141dfa5a885ccd97c3f626e4e81169fd1016dd40b043ad2888abb43.
Actual excerpts are fs-watch-01/linux-r8-gate-summary.txt. Documentation-only
commits afterward do not change any Docker input/product/test source; checked
with git diff against5e2b0dc across all copied source/build inputs.

The saved CTest LastTest.log from that exact successful build layer was inspected
through a cached build-stage image b691c71bd1d967f45af727e9fa20d860d9fa578f6408fb1b98e7491ba6ded7c3.
The layer was CACHED: no second test execution is claimed. Exact extracted
backend/F5 evidence is fs-watch-01/linux-r8-fs-details.json. Backend53 passed,
zero failures/skips,76.083s; F5 real-QML suite3 passed,zero failures/skips,2.558s.
This includes POSIX directory aliases, alias-to-regular-file rejection, dangling
targets, A-B/A-B-A scan rejection, the committed100k admission/baseline recovery,
and preview/grid/combined F5 plus text/modal precedence. The100k case recorded
initial GUI33ms,writer79ms,maxpublication68ms,observed/accepted identities0/1.
These are Linux Qt offscreen software-scenegraph tests, not physical GPU/DPI,
network or other filesystem qualification. Independent review and full current
Windows/integrated gates remain required.

## R7 independent review and active-filter coverage

Actual Claude report3e98077 is preserved verbatim in
fs-watch-01/claude-r7-review.md. It found no blocking source issue on5e2b0dc,
independently ran the Windows backend52/52 and two isolated100k repetitions,
and compiled/executed Linux real-QML F5 routes3/3 with zero skips. Final packet
approval remains withheld pending full Windows and integrated checks. Its Linux
F5 run is software-scenegraph evidence, not native Windows/GPU qualification.

The moderate G1 coverage gap now has a committed regression: reuse the generated
100k fixture with an active persistent filter matching every row, perform real
atomic replacements on the writer thread, await the final metadata size, and
measure actual applied filtered publications plus the GUI heartbeat. Keep all
250ms limits and30s recovery unchanged. Test-only statistics distinguish applied
results from discarded callbacks. Then clear the filter and retain the original
complete-baseline/selected-rename assertions. No product filtering behavior changes.

Windows targeted execution passed3/3, zero failures/skips,72.495s. Filter change
29ms, filtered publication62ms, filtered GUI gap63ms; unfiltered writer GUI136ms,
all callbacks at most135ms. Exact output: fs-watch-01/windows-r9-active-filter.txt.
The generated setup49.199s is excluded from the GUI measurement. Another lane's
F051 Windows suite was live during this backend-only run, so this is not an
exclusive-host performance qualification or the full25-target gate. The user's
next-slot reservation is recorded in the root native-suite marker; the already
running suite is allowed to finish. No other lane process is terminated.

## Reserved full Windows gate

The user-reserved next Windows slot completed successfully on f20711e source:
scripts/build.ps1 -BuildDir build-codex-fs-watch-01 -Test,25/25 targets,
403.85s,terminal0. No other Windows UI suite was active during this gate; the
pre-existing exact QtCore-only filter/file_operation_service orphan was left
untouched. This is shared-host evidence, not isolated CPU/IO benchmarking.
folder_grid151.63s,fs_watch149.52s,fs_watch_ui1.83s all passed. Subsequent commits
026bafb contain documentation only, with an empty diff across product/build/test
inputs. Output is fs-watch-01/windows-r9-full-gate.txt and the exact FS CTest
sections are windows-r9-fs-details.txt. These Windows Qt CTest sections contain
no per-case stdout, so no per-case/skip count is inferred from them.

A separate direct Windows fs_watch_ui_tests execution with a file logger passed
3/3,zero failures/skips,1.472s: fs-watch-01/windows-r9-ui.txt. The real-QML key
test covers preview/grid/combined F5 and text-input/settings-modal suppression.
It uses Qt offscreen/software scene graph, not physical keyboard/GPU/DPI
qualification. Existing ShortcutCheatSheet keys and PhotoGrid undefined-value
warnings also reproduce on Windows; no new UI fix is claimed by FS-WATCH.

Final integration remains open: main now contains Cache engine8b28060 and its
owner still holds the serial lock. The combined source, revision-validation
compatibility, Docker and integrated-commit gates have not been approved or run.

## R8 minor regression-oracle corrections

After the immutable candidate's full gate finished, strengthen the filtered
writer's final-size oracle: every committed replacement now has a distinct
128+sequence byte length, capped at4096 writes. The final expected size cannot
match an earlier replacement. Move the test-only applied/filtered markers inside
the real model publication guard, so a retained truncated listing is not counted
as a projection. Production behavior remains unchanged; these markers compile
only into fs_watch_tests. This addresses the concrete G4 and G5 naming findings,
without claiming QML delegate/layout timing from a model-only regression.

Build passed and the strengthened targeted Windows regression passed3/3,zero
failures/skips,84.285s. Active filter change33ms,publication85ms,GUI gap86ms;
unfiltered writer GUI161ms,maximum callback160ms. Original complete-baseline,
selected-rename and source-hash assertions also passed. Exact output is
fs-watch-01/windows-r10-unique-final-size.txt. Another lane had resumed Windows
tests after the reserved full gate ended, so this backend-only result is not an
isolated-host benchmark. The saved25-target pass proves f20711e source; it is not
relabeled as a full gate of this later test revision. Current-source Linux and
combined Cache/integrated gates and review follow-up remain open.

## Current-source Linux and independent source disposition

Full docker build -t glancelot-browser:main . on825f7f2 product/test inputs
passed25/25,210.08s,terminal0. Configure/compile279.0s and CTest actually ran;
they were not cached in this gate. Runtime image is
ec829ac89cbeab34b0eaac8756be8a450bbac2c0064753b0797bb3c6b5b95c64.
Summary output: fs-watch-01/linux-r10-gate-summary.txt. Later commits contain
documentation only, confirmed by empty source/build/test input diff.

Extracted saved CTest output from the exact successful layer through cached
build-stage image8ba4115c953e7c8dd36561ed892b57338292ec8dc4a32b504406378519da4f58;
this extraction did not run tests again. fs-watch-01/linux-r10-fs-details.json
records53 passed,0 failed,0 skipped,77.112s for backend and3/0/0,1.344s for QML.
The53rd case is POSIX-specific. The strengthened100k final-size/rename regression
passed with filtered change16ms,publication42ms,GUI gap52ms; unfiltered writer
gap71ms and maximum callback60ms. No physical GPU/network filesystem claim.

Actual Claude independent R9 f0bf272 accepted825f7f2 source for integration with
no blocker,major or new finding. Verbatim report: fs-watch-01/claude-r9-review.md.
It resolved G4 and G5 naming, retained the documented timing/view limitations,
and accepted Windows QML F5 evidence as covering the required real-key route.
This review was static only; its own runtime remains the earlier R7 runs. Final
packet approval is withheld for combined Cache compatibility, serial integration
and integrated-commit Windows/Docker checks. A full Windows gate of825f7f2 is
running separately; the earlier f20711e pass is not relabeled.

Independent Codex review of the Gemini Cache engine now reproduces stale pixels
on main8b28060, review16ef41f (CHANGES REQUESTED). See the Cache integration
handoff note for exact generated same-size/restored-mtime navigation evidence.
The Cache owner's correction and combined independent review remain necessary;
neither this Linux pass nor FS source acceptance waives that demonstrated defect.

## Exact current-source Windows gate before Cache sync

scripts/build.ps1 -BuildDir build-codex-fs-watch-01 -Test on825f7f2 source
passed25/25,409.58s,terminal0. Later commits changed documentation only.
fs_watch177.25s,fs_watch_ui1.81s,folder_grid126.89s passed. The backend is close
to its unchanged180s CTest timeout; this pass does not prove timing headroom on
slower or loaded hosts. No timeout/oracle was raised. Output is
fs-watch-01/windows-r10-full-gate.txt. Qt CTest stdout again contains no per-case
counts, so zero skips is established only by the separately saved targeted
Windows/current full Linux executions. No competing Windows UI suite was live.

Both candidate platform gates now pass. Under lead lock9463943/main0212daa,
the next step imports main and its frozen private Cache integration amendment.
The combined source has not yet been reviewed or qualified; these passes remain
evidence for the pre-Cache candidate, not for the upcoming correction.

## Combined branch baseline and navigation intent correction

Merge68c3731 imports Cache and the frozen private integration amendment. Two
private controller conflicts retain FS baseline/monitor lifetime and cancel(),
which now also cancels imported Cache lookahead. Public headers are unchanged
from that main snapshot. The combined source is not qualified by earlier gates.

A new generated FS regression places the noncurrent image outside all three
watches (root/a.png vs root/sub/leaf/b.png), first requiring a real unchanged
navigation hit, then replacing the original at equal4096-byte size/restoredmtime.
The initial merged target compiled, but failed its unchanged-hit assertion:
2 passed,1 failed,0 skipped,635ms. This exposes the synchronous navigationOpen
marker expiring before the new registration-delayed decode begins. Actual output
is fs-watch-01/windows-cache-combined-intent-red.txt.

Persist that consumed one-shot intent through deferred decode; nested explicit
opens do not inherit it, and the late-baseline fresh decode bypasses reuse.
After rebuilding, the unchanged hit succeeds and the regression reaches the
intended source-revision failure: source blue, display red, cache-hit true;
2 passed,1 failed,0 skipped,1.089s. Source and unrelated-image hashes remain
unchanged. Output is fs-watch-01/windows-cache-combined-revision-red.txt.
This intermediate branch intentionally remains red until the source validation
correction is implemented and independently reviewed; it is not a done candidate.

The root lock release and candidate-evidence update were included in another
session's93dea6b IO contract commit while the lead's release was being committed.
Verified the intended queue delta and clear lock; no IO files were reverted or
staged by this lane. That newer IO contract still needs branch synchronization
before combined final qualification. Preserve its owner implementation work.

## Frozen protocol and worker-side source validation

Lead protocol freeze is maina82c5ac; shared IO property definitions are kept
linkable by lead060e83b defaults, imported in branch1dd803a. The defaults expose
no IO implementation and do no filesystem work. Root lock is released8fe8e0f;
the IO owner's concurrent source edits remain untouched. Newer main CMake work
still needs synchronization before combined final gates.

Worker now implements the frozen --source-revision absolute-path mode and r1
metadata token encoding in private fs_source_token.hpp. Missing/directory or
ambiguous/weak revision evidence returns an empty token. Cache eligibility
requires a regular file, unambiguous identity and nonzero native change revision;
stream/special entries cannot gain a cache token. A private regular classification
is captured with the same native handle/stat, not a separate GUI filesystem query.
No token claims a media-content hash or exclusion of later external writes.

Normal decoding captures native revision before opening and rechecks after
conversion/copy, before decoded publication. A known changed/lost revision returns
an error without decoded; an unchanged eligible source carries its verified token.
Existing allocation checks, thumbnail framing and lock-free worker transfer/ack
are preserved. The controller has not yet adopted this token: its hit/admission
correction, prefetch deadline and bounded discovery remain open. R1 is not closed.

Windows target build and focused file-logged run passed9/9,zero failures/skips,
5.691s: fs-watch-01/windows-revision-worker-focused.txt. Tests qualify missing and
directory probes, malformed/relative/empty mode arguments (including a generated
PNG named --source-revision), stable tokens and equal-size/restored-mtime atomic
replacement; they also qualify a same-size/restored-mtime in-place source edit
after the real decoder probe but before shared-memory admission. That worker
returns a revision error/exit1 without publishing decoded pixels, while generated
source hashes remain correct. Original newer-open, live large cancellation and
replacement regressions pass in the same targeted run. No full suite/combined
review is claimed. The POSIX FIFO assertion is present but not executed on Windows.

The first admission-boundary fixture attempted QSaveFile replacement while the
worker held its source and failed setup with Windows Access denied,8 pass/1 fail
in5.220s; it did not test the revision assertion. Preserve that output in
fs-watch-01/windows-open-source-fixture-failure.txt. The fixture now performs an
allowed same-length shared-write edit for that boundary; the separate noncurrent
Cache regression and metadata-token comparison retain atomic replacement.

## Controller cache validation and IO ownership integration

Source2b97ad0 replaces GUI path/size/mtime cache lookup with a pure text path key
and cached pixels plus verified sourceToken. Navigation returns probe through the
isolated worker before reuse. Unknown/mismatched revisions reject the candidate
and decode freshly; malformed/missing token replies cannot admit pixels. Explicit
opens still decode. Ordinary and prefetch admission require verified worker tokens;
prefetch has its15s deadline and discovery has one running/one latest pending task.
Cache notifications coalesce safely on the GUI event loop. Cache-hit trace flags
are recorded only after validation, with the probe measured as CacheLookup.

The real noncurrent equal-size/restored-mtime regression passed3/3,zero skips,
1.319s (windows-cache-r1-first-green.txt): unchanged source first hits, then new
blue pixels display with cache-hit false. IO merge663fb19 retains the delivered
storage profile/admission interface and original Cache behavior. Original Cache
tests10/10 passed3.640s before the following ownership corrections; output is
windows-cache-original-io-merged.txt. These are scoped passes, not final approval.

The imported IO ledger accepted the same ID twice and charged3072 bytes to one
entry; it also accepted a negative estimate. Regression4c11c50 recorded2 passed,
2 failed,0 skipped,5ms in windows-io-admission-red.txt. Lead froze correction
contract3aef7c9 and linkable opaque allocator b2cbe0b; root lock release54eae8f
keeps IO in review. Existing IO evidence remains preserved. This integration does
not redesign storage classification, profile/capacity policy or physical limits.

Private io_job_lease.hpp now binds successful admissions to work completion,
with process-retirement release independent of a destroyed UI owner. Selection,
lookahead and thumbnail wire counters are separate from opaque process-unique
IDs. The allocator uses a high ID namespace to avoid legacy ordinals during
migration, never resets/reuses IDs, and fails closed on exhaustion. Zero-byte
revision probes have foreground ownership; fresh image admission replaces that
lease with its pixel reservation. Cancelled/stale thumbnails keep their lease
until completion/exit. Duplicate/zero IDs, negative estimates and overflow fail
without altering the ledger. Availability notification and consumer retry are
queued/coalesced, outside the scheduler mutex. Deadlines arm before start so a
synchronous startup failure cannot restart an obsolete timer. Admission callbacks
recheck process/generation before altering state.

Latest focused Windows run passed9/9,zero failures/skips,4.532s in
windows-io-ownership-green.txt. It covers the red ledger cases,32 coalesced
availability releases with getters called from the notification, and a real
controller cold/open/navigation/cache-hit/teardown sequence preserving unrelated
reservations whose IDs match the old counters. Newer-open/live cancellation and
the stale-pixel regression also pass. Latest original suites passed Cache10/10
4.122s,Thumbnail36/36 6.779s,IoScheduler12/12 0.068s,all zero skips; outputs are
windows-io-final-cache.txt,windows-io-final-thumbnail.txt and
windows-io-final-scheduler.txt. No full combined gate or independent review has
yet run. Controlled malformed/unknown-reply, mid-probe/prefetch cancellation,
deadline, bounded-discovery and actual thumbnail wakeup fixtures remain required.

Newer main includes META-HUD contracts while its owner's controller wiring is
still being prepared. Preserve that work and synchronize before final review and
integrated gates; these branch-local results are not main's qualification.

## Full combined backend checkpoint

Direct fs_watch_tests file-logged execution on73737a2 passed59/59,zero
failures/skips,142.879s: fs-watch-01/windows-cache-io-full-backend.txt. This
includes Cache revision validation, admission ownership and existing FS cases.
The100k case kept all250ms/30s oracles: initial GUI33ms,writer/recovery153ms,
filtered change31ms,filtered publication72ms,filtered GUI84ms,maximum callback
152ms. Generated file setup54.680s is excluded from GUI measurement. Native
source hashes, baseline/rename recovery and failure/cancellation assertions pass.

This is a full backend executable run, not scripts/build.ps1 -Test or a Linux/
integrated gate. Source still needs controlled IPC fixtures, independent combined
review and synchronization with newer main. No source was changed during this run.

## Current-main IPC and lifetime checkpoint

Controlled fixture6af2858 passed18/18,zero skips13.492s. Current-main merge6d033c0
preserves metadata/animation/rename/copy-move/RAW. Private adapters bound metadata
to one active/one latest value-owned parse and route rename reconciliation through
async navigation. Adapter/IPC20/20 passed15.810s before final reentrancy guards;
final native RAW unchanged/replaced and metadata/mutation6/6 passed2.567s.
See windows-ipc-focused.txt and windows-current-main-{adapters,native-adapters}.txt.

Actual Claude R11 report10f49fb reviewed0cab319 statically: source acceptable
subject to uncompleted gates, with a medium failed-start retirement followup.
Reports R10ec649b4 (rejected older68c3731) and R11 are preserved verbatim.
R11's suspected Linux edge was reproduced using the actual helper: Starting
retirement left one job/3072 bytes reserved. c48b77c makes retirement idempotent
across stopped-state/error/finished signals while preserving owner-independent
release and guarded notifications. The same Linux probe now returns process
deleted/lease expired/callback1/jobs0/bytes0. Windows5/5,zero skips291ms covers
failed-start/running/closed-owner cases; missing startup is synchronous there.
The lease-probe directory records the exact reproducible generated-only probe.

The immutable0cab319 Windows scripts/build.ps1 -Test checkpoint passed30/31
targets in524.14s, with fs_watch timing out at179.99s; this is a failure, not a
qualified gate. The later unchanged single100k case on c48b77c passed3/3,zero
skips79.396s: generated setup54.626s, initial GUI12ms, writer GUI125ms, filter
change25ms, filtered GUI74ms, maximum publication124ms. The method is moved
unchanged to required fs_watch_budget_tests/fs_watch_budget under lead b6d2b73;
its SHA256 move proof is retained. Both backend targets keep180s timeouts and all
250ms GUI/30s recovery oracles. Required full gates now include both and FS QML.

The16MiB identity limit is per snapshot, not a whole-window or process RSS cap.
Current/parent baselines, bounded publications and model history can retain
several such budgets, often shared through pointers/COW. Aggregate shared resource
accounting belongs to the later Codex PERF-RESOURCE packet. Non-birth-time and
ambiguous-link filesystems conservatively decode uncached and cannot prove rename
identity. Poll metadata cost, physical GPU, HDD/network/ReFS and power behavior
remain unqualified. These limitations are not changed to obtain passing tests.

New complete Windows/Docker gates, independent followup review and serial
integration with checks on the integrated commit remain pending.
