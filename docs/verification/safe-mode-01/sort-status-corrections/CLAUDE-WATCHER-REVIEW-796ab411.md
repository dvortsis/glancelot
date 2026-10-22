# Watcher follow-up: review of the six-line cache-fixture change at `796ab411`

**Disposition:** the fixture change is acceptable as a test-only correction. It is not a full approval, and it is not a diagnosis of the earlier failures. One documentation error should be fixed, and two coverage gaps remain. I executed nothing. I relied on the saved 2026-10-21 approval entry for this payload, not on a statement from you in this conversation.

## Ranked findings

1. **The budget timeout is more likely a time-limit margin problem than a hang, and the README misstates the limit.**
   - `cmake/FSWatchTests.cmake:25-26` sets `TIMEOUT 180` for both `fs_watch` and `fs_watch_budget`.
   - `sort-status-corrections/README.md:97` says the isolated pass kept "the 300s outer timeout". That is wrong for the file I read. The retry log `windows-full-retry.txt:103` shows `***Timeout 179.99 sec`.
   - Budget runtimes across the logs: 108.4s, 122.8s, 145.5s, 164.7s (`full-fs-watch-budget-results.txt:16`), then a timeout at 180s. The retry recorded generated-corpus setup at 135.9s, which is most of the limit.
   - Trigger: the 100k-file generated setup varies a lot under Windows load.
   - Impact: the unchanged 180s limit has a margin of about 15s in the worst passing run, so the next full gate can time out again.
   - Disposition: this stays unresolved. The 3/3 isolated pass and the 108s pass at `796ab411` do not remove it. Correct the README. Any decision to change the timeout or the setup cost belongs to the lead, since it touches configuration. Do not describe the timeout as fixed.

2. **The late-baseline cache-miss path is proven by source. It has not been shown to be the cause of the full-run failures.**
   - `viewer_controller.cpp:314` and `:318-321`: when the 100ms timer fires first, the controller sets `decodeStartedWithoutBaseline` and starts a decode.
   - `:333-334`: when the baseline arrives after that, the controller bumps `generation`, calls `cancel()`, sets `cacheReuseAllowed=false`, and decodes again.
   - `:827`: with `cacheReuseAllowed` false, `revisionOnly` is false, so the decode is fresh and `isLastDecodeCacheHit()` cannot be true.
   - The probe confirms this. With prompt baseline it recorded `revisionStarts 1`, `decodeStarts 2`, `unchangedHit true`. With a 70ms-per-directory delay it recorded `decodeStarts 5` and `unchangedHit false` (`local-baseline-probe-results.txt:10,21`).
   - What is not shown: no failing full-run log records worker purposes or baseline timing. The two failures were `fs_watch_tests.cpp:684` and `:404`, both `isLastDecodeCacheHit()` returning false. They are consistent with the late-baseline path, but other causes are not excluded. Examples are a revision-token mismatch on Windows or cache eviction, where `QCache` cost is 128 MiB. I did not check either.
   - Evidence after the fix is thin: one 82/82 pass, one 37/37 pass, and 3/3 focused passes. The earlier failures were 2 of 2 full runs, so a single pass is weak evidence. Keep the claim as "makes the tests exercise the asserted path".

3. **The fixture change preserves the two tests' criteria and leaves the other items intact.**
   - Hooks: `ControllerTestHooks` defaults are `registerNative=true`, the default snapshot reader, and an empty `decoderProgram` (`fs_monitor.hpp:27-42`). `viewer_controller.cpp:656` and `:953` keep the real `glancelot-decoder` when `decoderProgram` is empty.
   - Hook scope: the new `HookScope` is scoped to each test function and restores the previous pointer on exit (`fs_watch_tests.cpp:55-58`).
   - Assertions: the hit assertion at `:407` and the foreground/admitted-bytes assertions at `:402-412` are unchanged. So are the hit at `:690` and the miss at `:702` with its hash and pixel checks.
   - Other items I read as unchanged: the production 100ms default, the `controllerLateCoalescedBaselineDecodesOnce` rows, and the CTest timeouts. The late-coalesced test (`:743-775`) builds its own hooks with `registerNative=false` and `baselineDeadlineMs=0`, so it still uses 100ms. Both rows passed in `windows-full-fs-watch-results.txt:57-58`.
   - Timing: each open can now wait up to 5000ms for the baseline. That fits the 10000ms `QTRY` bounds and the 180s CTest limit (`fs_watch` ran about 82s).
   - Disposition: acceptable.

4. **A regression in the late-baseline cache behavior is no longer covered by any committed test.**
   - Before this change the two cache tests ran on the 100ms fallback, so they sometimes exercised the late-baseline path. Now no committed test asserts it.
   - The late-coalesced rows check fresh pixels and decode counts, but not that the cache-hit flag stays false after a late baseline. The probe that demonstrated this was temporary and is not committed.
   - Minimal coverage: a new test in a new file, not `fs_watch_tests.cpp`. It would use a gated `options.read` as at `:757-761` to deliver the baseline after the 100ms fallback, with real native registration if practical. It would assert pixels of the changed source and `!isLastDecodeCacheHit()` when the source is unchanged. It would also assert `workerStarted` purposes. That makes the fallback a deliberate, tested behavior instead of a source of flakes.

5. **The 5000ms wait is a wall-clock bound, not a deterministic guarantee.**
   - Trigger: if native registration or the baseline read takes longer than 5s, the fallback fires, the late restart occurs, and the original flake returns, still with an assertion failure and no explanatory output.
   - Impact: low. It fails closed, as an assertion failure and not a false pass.
   - Minimal addition: a `workerStarted` hook counting `decode` versus `revision` purposes in the two tests, reported through the existing `QVERIFY`-failure output. That would make a recurrence diagnosable. It is additive, so it keeps every existing assertion. It is optional and not blocking.

## Not found

I found no source problem in the scheduler, lease or source-token helpers for these tests. Line 872 compares `cached->sourceToken` to the reply token, and an empty token never counts as a hit (`:873`). I did not read `io_scheduler`, `io_job_lease.hpp`, `fs_revision.hpp` or `fs_source_token.hpp` in depth.

## Inspection limits

- I read `viewer_controller.cpp` lines 120-460 and 730-920, plus 1395-1416.
- I read the cache-test region of `fs_watch_tests.cpp` and the `Fixture` and `HookScope` definitions.
- I read the payload document, both READMEs, the proposed patch, the probe and original-case results, and the 82/82 summary lines.
- I did not read the full test logs, the Docker logs, the `windows-process` artifacts or `fs_watch_budget_tests.cpp`.
- Gate results are quoted from the READMEs only. I did not verify the pinned SHA or the byte-identity claims. The physical GPU/DPI, sanitizer and power-loss gaps stay unverified.
- This is not a full gate, an approval of integration, or a task-done claim.
