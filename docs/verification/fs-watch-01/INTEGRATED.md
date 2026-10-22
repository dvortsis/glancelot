# FS-WATCH-01 integrated qualification

FS-WATCH-01 software packet is done on merged source
`1bd44616d801f09b61f1191724d918f7decd8992`. This closes F063 and its frozen private
cache/revision/admission integration corrections; it does not close the entire
Codex lane goal or broader CACHE-01/IO-01 requirements.

Independent source acceptance is actual Claude R7/R9/R11/R12/R13/R14, including
the reproduced Linux Starting lease fix, weak-pointer retirement and metadata
fast path. R14 report9851f03 accepts product4a86528 subject full/integrated gates.
Gated candidate80964f7 has identical product/tests; evidence-only3ffcc6b merged
--no-ff as1bd4461 under the held lead lock. Later SAFE-MODE contract declarations
are a separate packet, not part of this measured source.

## Actual merged-commit execution

Own branch codex/verify-fs-watch-integrated; worktree
.worktrees/codex-fs-watch-integrated pinned1bd4461; build
build-codex-fs-watch-integrated. Source stayed unchanged throughout.

- `scripts/build.ps1 -BuildDir build-codex-fs-watch-integrated -Test` returned0:
  full32/32 in580.54s, grid227.44s. Exact Qt backend82/82 zero skips85.026s,
  dense3/3 zero skips136.737s, UI3/3 zero skips3.606s and replacement platform
  26/26 zero skips872ms. Dense setup89155ms, initial GUI13ms, sustained GUI74ms,
  filtered GUI98ms and maximum publication88ms meet unchanged250ms/180s bounds.
- `docker build -t glancelot-browser:main .` returned0 and built the runtime image,
  reusing the successful compiled/test layers. That invocation is recorded as
  cached, not a fresh test execution. The outer wrapper returned1 because its
  summary check found no new CTest lines from that cached layer; Windows was not
  rerun and no cached result was called a new execution.
- Exposed that exact cached build stage as glancelot-fs-watch-integrated-build,
  then ran **fresh** `ctest --test-dir /build --output-on-failure` in a new owned
  disposable container, QT_QPA_PLATFORM=offscreen. All32 passed in323.97s, process
  and inspected container exit0. Git diff of Dockerfile/.dockerignore and every
  compiled/copied input between candidate80964f7 and merged1bd4461 is empty;
  the tested build artifact matches the merged source. See integrated-source-proof.json.
  Actual Qt backend83/83 zero skips70.999s, dense3/3 zero skips31.070s, UI3/3 zero
  skips3.244s. The failed-start lease row observed QProcess::Starting. Dense
  setup6029ms, initial GUI54ms, sustained GUI37ms, filtered GUI209ms and maximum
  publication198ms remain below250ms. Grid163.83s. The completed owned container
  was removed only after copying its fresh case logs.

Exact integrated-windows-*, integrated-linux-* and source proof retain outputs,
case counts, cached-versus-fresh distinction and helper/lease evidence. Text hash
manifest explicitly normalizes UTF8 line endings to LF for Git checkout portability.
No assertions, timeouts, feature targets or skips were weakened to obtain passes.

## Scope and limits

Use ACCEPTANCE-AUDIT.md for the original frozen behavior/amendment-to-test mapping.
Candidate and historical failure logs remain, including the29/32 failed run and
different-renderer auxiliary grid timeout. The later selected software-renderer
assertion passed; it was not substituted for the full grid gate. No universal
host timing, physical GPU/DPI/network/ReFS/cloud-storage or power-loss claim.
This is generated-media Windows and Linux test-runtime evidence.

The retired Defender-triggering fixture stays deleted. This source contains the
separately reviewed/qualified334065e capture replacement. Candidate post-run
repository-only Defender records showed only inactive/nonexecuted retired-helper
records with successful actions; no machine-clean or false-positive conclusion,
protection change, exclusion or restoration was used. Native helper tests pass.

CACHE-01 eviction-pressure and aggregate current/worker/staging/cache headroom,
IO-01 Linux rotational detection/adaptive/physical storage evidence and hard-limit
policy remain in their owners' review scope. FS's correct revision/lease behavior
does not establish those wider requirements. PERF-ENUM/resource and remaining
Codex packets retain their full scope; only this FS packet is being closed.
