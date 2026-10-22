# FS-WATCH acceptance audit for corrected merge1bd4461

Completed software-packet audit. Source is the R14-reviewed4a86528
product/test tree with documentation-only main synchronization. Current Windows
full32 pass is625.10s; Linux full32 passed233.88s and built the runtime image.
Linux backend83/83, budget3/3 and UI3/3 have zero skips; the failed-start lease
case actually observed QProcess::Starting. Actual merged1bd4461 Windows32/32
passed580.54s and fresh Linux32/32 passed323.97s, with backend82/83, budget3 and
UI3 zero skips. See INTEGRATED.md and integrated-* outputs. Retain earlier failures.

Original frozen behavior maps to these current feature cases and implementations:

- Scope/worker/queue bounds: fs_monitor.hpp has one worker, one latest request and
  coalesced publication, at most current directory/parent/image registrations,
  120ms debounce/500ms maximum event delay, 2s healthy fallback/backoff to30s,
  and250ms close wait. delayedOldScopeAndStormAreBounded,
  continuousNativeChangesCannotStarvePublication, blockedProbeDoesNotBlockClose,
  firstBaselineMarkerSurvivesPublicationCoalescing and related baseline cases
  assert the actual lifetime/generation/queue behavior, not screenshot inference.
- Watch loss/alias rearm: unavailableNativeRegistrationFallsBack,
  establishedWatchLossAndRearm, retargetRearmsNativeAliasWatch and linked-scope
  cases exercise fallback and re-registration. Retarget A-B/A-B-A during listing
  refuses completeness. They do not prove detection of every OS overflow or
  physical/coarse/network/cloud filesystem variant.
- Identity/current/selection: selectedNoncurrentRenameAndCurrentRename,
  renamedCurrentSurvivesOldPathReuse, renamedDirectorySurvivesOldPathReuse,
  hiddenSelectedRenameUsesUnfilteredIdentityBaseline, case-only rename and
  watchedCurrentRenameRetainsFilter preserve proven identities/selection/filter.
  Hard-link/ambiguous/truncated/dangling/non-directory-alias cases refuse inference;
  incomplete observations retain the last complete baseline and still invalidate.
- Replace/delete/navigation: replacementAndUnrelatedChanges, atomic replacement,
  same-size/restored-mtime revision, empty-folder/directory recovery,
  localDeleteDoesNotAdvanceTwiceAndRefreshCommand, failedSourceAndRefreshPreserveWidenedRoot,
  rapidExplicitNavigationCannotRestoreOldPixels and source-hash assertions cover
  fresh pixels, unchanged unrelated assets, root preservation and no media writes.
- Cache/RAW/IPC integration: noncurrentSameTupleReplacementInvalidatesCache,
  decodeRejectsSameTupleEditAfterAdmissionProbe, rawWorkerRevisionProtocol,
  malformed/empty tokens, superseded/deadline workers and discovery-latest tests
  cover revision-before/after admission/publication and bounded async adaptation.
  Original Cache/IO targets remain required; real hits are preserved, not disabled.
- Admission ownership: distinct thumbnail/controller admissions, invalid estimates,
  idempotent retired-worker release, availability coalescing and memory-release
  wakeups exercise unique opaque IDs and lease lifetime. The failed-start Linux
  Starting edge is observed in linux-fast-path-full-backend.txt; the Windows
  NotRunning row alone would not establish that edge.
- Imported adapters: metadataAdapterKeepsLatestSelectionAndClosesSafely and
  mutationAdapterRearmsMonitorAndPreservesNewestOpen cover bounded parsing,
  receiver lifetime and newer explicit-open priority; normal/RAW protocol tests
  retain the independently reviewed shared-memory prerequisite.
- Dense GUI publication: required separate100k case retains all250ms GUI and
  publication,30s recovery,180s backend/budget and90s QML limits. R7 closed the R6
  missing-budget-test finding. R14 accepts the new same-key/order shortcut by
  equivalence to the prior merge. Candidate Windows full case passed3/3 zero skips
  129.271s/max115ms; selected diagnostic passed159.381s/max152ms. Integrated
  Windows passed136.737s/max88ms and fresh Linux31.070s/max198ms, all zero skips.
  These are measured runs, not an arbitrary-host timing guarantee or PERF-ENUM.
- Command route: fs_watch_ui_tests sends actual F5 in preview/grid/combined and
  checks text/settings-modal precedence through the existing dispatcher. R7 own
  Linux runtime and R9 assessed native real-QML evidence exist; integrated UI is
  3/3 zero skips3.606s Windows/3.244s Linux. No physical keyboard/GPU/DPI qualification follows.

Independent review chain: R7 closes R6-1/R6-2 in source/tests; R9 accepts the FS
implementation for integration subject gates; R11 reviews combined cache/IO and
imported adapters; R12 closes the reproduced Linux Starting lease leak and accepts
the unchanged dense-test split; R13 closes QPointer retirement bookkeeping; R14
accepts the metadata fast path subject measured full/integrated gates. The retired
platform fixture is replaced by separately reviewed/qualified334065e. Reviewers'
actual methods and unexecuted checks remain explicit in their reports.

Open acceptance outside this packet must remain open: CACHE-01 eviction-pressure
and aggregate current/transfer/staging/cache headroom, IO-01 planned Linux
rotational detection/adaptive/physical-media evidence and hard-ceiling policy,
and physical watcher/render/driver/DPI/power-loss variants. The FS job-ID, lease
and source-revision fixes do not establish those broader Gemini-owned requirements.

Candidate and actual integrated checks, including fresh Linux Starting execution,
are now retained and satisfy the independent reviews' remaining software gates.
Close FS-WATCH-01 and release its lock after committing this evidence. Broader
Cache/IO/hardware requirements and all remaining Codex packets stay open.
