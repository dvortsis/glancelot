# Independent decoder cancellation review

Candidate source: `a6bda32feac0120332badd7e86ba713e31095472`, Claude-authored change confined to `src/worker/main.cpp`. Baseline: parent `d0c65954713dbfe6939c432eaa771380ce475da7`. Reviewer: Codex lane in `.worktrees/codex-review-input-02-final`, branch `codex/review-input-02-final`.

**Disposition: APPROVED for the isolated worker pixel-copy semaphore removal, within the Windows protocol/cancellation scope measured below.** This is a separate prerequisite assessment, not INPUT-02 UI approval. The five UI blockers in `INPUT-02-FINAL-CODEX-REVIEW.md` remain CHANGES REQUIRED. Full INPUT Windows/Linux gates and integrated-commit checks remain outstanding. The lead owns selection/integration of this standalone change.

## Finding and protocol assessment

The baseline worker locks the QSharedMemory system semaphore throughout copying decoded pixels. Killing it during that copy leaves the parent stuck when detaching its mapping. This review independently reproduced that exact sequence. The candidate removes only the worker's long-held lock/unlock; it writes pixels, emits decoded only after the copy and waits for parent acknowledgement before exit. The parent validates geometry and copies the completed transfer before acknowledging. Generation/process guards reject stale callbacks. The candidate passed both an exhaustive complete-payload oracle and five witnessed incomplete-copy cancellations.

No blocking defect found in the isolated change. This does not establish that every possible Qt shared-memory attach/destructor interleaving or platform is deadlock-free. In particular Linux cancellation and whole-controller rapid-open/teardown behavior were not executed here.

## Independent method

The lead released a native slot only for this small isolated project. No full INPUT suite, product UI process or Docker build ran. Ignored sources/evidence reside under this reviewer worktree's `.cache/decoder-review-probe`; no production source, root CMake, public header, queue or other worktree was edited.

A standalone CMake project builds the exact candidate `../../src/worker/main.cpp`, a copy of the baseline worker, and a Qt host using Core/Gui. The baseline copy's text was independently compared case-sensitively with `git show d0c6595:src/worker/main.cpp` after line-ending normalization: equal. Its on-disk byte hash below records the copy actually compiled. Both workers used the same host/toolchain/Release flags.

Each host creates a disposable 4096x4096 PNG filled RGBA(17,34,51,255), hashes it before starting the worker, validates the worker's probe geometry/64MiB admission request, creates and zeros an admitted QSharedMemory mapping, then sends the random key. No user media or settings are used.

For successful transfer, the host waits for decoded, checks 4096x4096 and stride16384, locks the transfer and compares **all 67,108,864 bytes** with the expected RGBA pattern before sending `ok`. It checks normal worker exit0 and successful detach. This verifies actual content rather than phase messages alone.

For cancellation, test-only volatile byte observations wait for first byte17 while last byte remains0. The host kills the worker, waits for termination, and requires the same first17/last0 evidence **after termination**, so a copy that finished between observation and kill does not qualify. It records pending control output, then flushes `detach_begin` and calls detach. Incomplete-buffer reads are restricted to this instrumentation; production retains decoded-before-read ordering. A separate Python supervisor terminates the host's own process tree after a 60s whole-host deadline and records final source hash. The baseline was limited to at most three attempts and stopped after its first qualifying partial kill; candidate ran five attempts.

## Actual commands and results

Windows native MSVC19.44.35229.0, project-local Qt6.8.3, CMake/Ninja Release C++20. Initialized the existing VS2022 x64 developer environment and placed project-local Python Scripts and Qt bin on process PATH. A `vswhere.exe` PATH warning occurred inside developer setup; compiler detection and all six compile/link steps completed successfully, with cache pointing to MSVC14.44.35207 x64. No toolchain changes were made.

```text
cmake -S .cache/decoder-review-probe -B .cache/decoder-review-probe/build -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH=C:/src/Github/Glancelot/.tools/Qt/6.8.3/msvc2022_64
cmake --build .cache/decoder-review-probe/build --parallel 2
[6/6] Linking CXX executable decoder_probe.exe
exit0

C:/src/Github/Glancelot/.tools/python/Scripts/python.exe .cache/decoder-review-probe/run_probe.py --bin .cache/decoder-review-probe/build --out .cache/decoder-review-probe/evidence-v1 --attempts 5
runner exit0 (records outcomes, does not convert the expected baseline timeout into a pass)
```

- Candidate complete transfer: exit0; decoded geometry correct; every RGBA pixel matches; acknowledgement followed by normal worker exit0; detach succeeds.
- Candidate cancellations0..4: **5/5 qualify**, stopped child, first17/last0 after termination, empty remaining control output, successful detach, host exit0. Detach durations **14,13,13,17,14ms**.
- Baseline complete transfer: exit0; same exhaustive payload/ack/normal-exit/detach checks pass.
- Baseline cancellation0: qualifies with stopped child and first17/last0 after termination. Log ends at `detach_begin`, no `detach_end`. Outer deadline expires; own host tree terminated at **60.258s**, host exit1, `timed_out=true`. No second baseline cancellation was launched. This is a reproduced failure, not an inconclusive missed window.
- All eight generated fixtures retain identical before/after SHA256 `0a7c21cd000d2debe563928e8c8faf3acf3d9d3f902dce4e5f2e3b66128c4da9`.
- After runner termination, Win32_Process inventory filtered to the isolated build's absolute executable path found **zero remaining review processes**. Native slot was released back to the lead immediately.

## Evidence identity and boundaries

SHA256 of actual source/harness/evidence bytes:

- Candidate worker: `df2390c99477a005240cb14d3c1fa3f075e17df8b9e1e027ccf2f1bfa0c9c650`.
- Baseline worker copy: `adeb4e03e07e929bb79672c38a089a1a8eb4b3366aef8945d0b27d7e725abe2f`.
- Qt host `host.cpp`: `d18f4b42b67fb24dc4cad2ee27215f3b056c688dbd3c0ad021ed803e5b227899`.
- Supervisor `run_probe.py`: `b204595e80c7fa284dfd009fd0d659049f85f6874fd187e4d13da2eb7d967899`.
- Raw `evidence-v1/results.json`: `4804ef3eef2f977b1b2bf911db8848d70ec90f869658ca36d7e5bc45f59570f0`.

Raw per-trial JSONL stdout/stderr, source fixture hashes, commands and timings remain ignored under the evidence directory. No claims of full controller shutdown, repeated UI image navigation, native GPU presentation, Linux/Wayland/X11 cancellation, hardware qualification or all failure modes are made from this host test. The outer supervisor is a bounded experiment and does not relax production behavior. Integrated native full-suite and Docker checks remain the lead's required gates.

`git diff --exit-code a6bda32 -- src include ui tests CMakeLists.txt` returned0 before report commit; production remains exact. This report alone is added by this follow-up review.
