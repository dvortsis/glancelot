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
