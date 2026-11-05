# Phase 2 contract decisions, November 5, 2026

Packet: PHASE2-CONTRACTS. Decided by the packet session under the lead's delegation: best performance and user experience, keeping today's behaviour where an option does. Contracts: [docs/contracts/](../contracts/).

Each row is an open question from the October 22 drafts, a problem found while checking them against `main` at `b1d57cd`, or a fix from the fresh review (`docs/verification/PHASE2-CONTRACTS-REVIEW.md`, `claude/review-phase2-contracts`). The lead confirmed J1 and J4 and decided J11, R6 and D9. "Visible" marks a decision the user will see. The lead may overrule any of them, and those are the ones to read first.

## META-ROLES

| # | Question | Decision | Alternatives rejected | Visible |
| --- | --- | --- | --- | --- |
| M1 | Read keys for the whole folder for `captured` and `dimensions`? | Yes, on the listing or navigation worker, with a budget of 20,000 files or 4 s on network storage. The old order stays until the keys are in (`sortPending`). When the budget runs out, unread files go last (`sortPartial`) | Restrict these sorts to the grid's loaded rows: breaks NAV-06 parity with traversal, so next/previous would disagree with the grid | Yes |
| M2 | Rating source | Embedded EXIF and XMP until C1. Then a sidecar overrides embedded (the Lightroom convention), through a `RatingOverlay` function that C1 installs and `readMetaKeys` calls | Merge rules inside the reader: it would need C1's naming profile. A separate C1 role: two sources of truth for one rating | When C1 lands |
| M3 | Capture time | Order on the wall-clock value, ignoring `OffsetTimeOriginal`; keep the offset for display | Normalise to UTC when an offset exists: mixed sources order differently from the times the user reads, and camera offsets are often unset or wrong | Yes |
| M4 | Add the new keys to the `sort` command cycle? | No. Menu and Settings only; the cycle stays at four keys | Add them: changes what an existing shortcut does, and triggers a metadata pass on a key press | No change |
| M5 | Cache placement | `src/app/metadata_keys.hpp`, Track A owned. C5 reads the `captureTime` role after `requestAllMetadata` (M9) | Shared header now: freezes an internal type no consumer needs | No |
| M6 | In-process parsing of untrusted headers | Accepted, as the HUD already does, bounded to 64 KiB per file, with a mandatory F4 fuzz target | Read keys through the decoder process: a process launch per file is far too slow for a 20,000-file pass | No |
| M7 | Found: the new keys fell into the *size* branch of `sortBefore` | `sortBefore` treats both as "every key missing" until A2, which matches the contract's empty-reader rule | Leave it, since nothing reaches them: a wrong order for any early caller of `fileSortBefore` | No |
| M8 | Found: `settings_policy.cpp:140` allows only four sort keys | A2 must add both, or a saved new key fails validation. In its migration table and tests | | No |
| M9 | Review B7: rating groups, chips, jump to date and bursts need every row's value under any sort | One operation, `requestAllMetadata`, reusing the order path's worker and budget. Features call it only while they are on | A sort by rating: doesn't serve chips or jumps. Each feature its own pass: four budgets to fence | Yes |
| M10 | Review MR-N1: `Navigator` is value-constructed at many sites | A process-wide `setDefaultSortKeyReader`, which also removes the `keys` parameters | A per-instance setter: any missed site silently breaks traversal parity | No |
| M11 | Review MR-N2 to N5 | Admission waits within the budget; time budget on anything not known to be an SSD; one pass per folder and revision; the overlay only when ratings are wanted | | No |

## DECODE-TIER

| # | Question | Decision | Alternatives rejected | Visible |
| --- | --- | --- | --- | --- |
| D1 | Fit buckets | Powers of two from 512 to 8192; Fit then Full only when the source has at least 2x the bucket's pixels | Share A5's thumbnail buckets: different sizes and lifetimes, so sharing only couples two services | Faster first paint |
| D2 | Keep Preview after Full? | No. A higher insert evicts lower tiers of the same source and token | Keep a small Preview for fast reverse navigation: more memory for a case the lookahead's reverse work should measure first | No |
| D3 | Wire format | Hand-written JSON fields, as today. The structs stay in-process | Serialise the structs: ties the wire to `std::filesystem` and `file_time_type`, which do not cross processes meaningfully | No |
| D4 | Decoder version in the key | Deferred. The cache is memory-only for one process. A future disk cache must add it; the contract says so | Add it now: an unused field that nothing can test | No |
| D5 | `DisplayManager` overload as well as the controller fence? | Both. The overload is defence in depth for every other caller. D1 applies the diff | Controller only: leaves the equal-generation gap open for the presentation path | No |
| D6 | Persistent worker | Rules frozen now: a request id echoed on every message, per-request reset, the same deadline and isolation, the same apply rule. D1 chooses the rest | Freeze the whole protocol: D1's measurement decides whether it exists at all | No |
| D7 | Found: Phase 1 code gates on `imageReady`, `loading` and now `navigating` | The first tier sets `imageReady` and clears `loading`. `imageFinal` tracks the rest. `navigating` does not wait for refinement. The slideshow clock runs from the first tier. Lookahead waits for `imageFinal` | Hold `loading` until the last tier: stalls the slideshow, the output and every test that waits on it | Yes, once D1 lands |
| D8 | Found: a pin or reference taken at Fit would stay at Fit | Full is re-offered to the holder through the existing calls. If refused, it stays labelled | Refuse pins until final: makes pinning feel broken during a load | Yes, once D1 lands |
| D9 | Review B3, lead decision: EN-11's same-generation refresh stays | `DecodeRequest::refresh` may replace an equal tier. A refresh never starts below what's shown, so the view never drops in quality and `DisplayManager` needs nothing more. Decodes of one generation are sequential, so a higher rank with a new token just applies, with no restart | A new generation for the refresh: EN-11 records why not. Restart a ladder on a token mismatch (the review's version): more states and a loop to bound, for the same result. Refresh from Fit with Full shown: a visible drop in quality on a quiet reload | No |
| D10 | Review B8: D2 had no path to the applied image's colour | `viewer.imageColour`, in D1's diff, fenced by the apply rule. **D2 now depends on D1**, recorded for the lead | D2 parses the status string: translated, and D2 removes it | No |
| D11 | Review DT-N1 to N4, RL-N7 | `kFitMaxSourceBytes` per format class; re-publishes keep their tier; a Fit insert evicts other buckets; new dimensions under a new token aren't an error; first tier `Foreground`, refinement `Visible` | | No |

## CAPABILITY

| # | Question | Decision | Alternatives rejected | Visible |
| --- | --- | --- | --- | --- |
| C1 | Container signal | `GLANCELOT_DEPLOYMENT=container` in the Dockerfile `ENV` (F1 sets it) as the only signal | Path heuristics: forbidden, they guess | Yes, in Docker |
| C2 | Delete `DisplayCapabilities` and `DecoderCapability` | Deleted in this packet. Nothing used them | Keep until D2: two competing frozen types | No |
| C3 | Generated capability page | `docs/SUPPORTED-CAPABILITIES.md` stays hand-written; `exportText` serves E2 | Generate it: a build on one machine can't state per-platform facts, and physical status comes from runbooks | No |
| C4 | Who owns the populators | D2 owns all four. F2 reads | F2 owns Source: splits one model's fencing rules across two tracks | No |
| C5 | A decoder probe per window process | Accepted: one short deferred run per window | A shared cross-process cache of probe results: needs invalidation for a cost that is already small | No |
| C6 | G-5 check (lead decision) | `QPlatformIntegration::MultipleWindows`, the existing Wayland `canPosition` rule and the container signal, in `referenceWindowSupport`. A runtime expose deadline (2 s) catches platforms that claim support and fail | Plugin-name allowlist: guesses. Screen count: a second window works fine on one screen. No runtime check: a platform's claim would be the only evidence | Only where a second window can't open |
| C7 | G-5 in Docker and on Wayland | `Partial`: the window keeps opening as it does today, and Settings says it can't be placed. Only a platform with no multiple-window support refuses | Refuse in Docker: breaks a working feature, because `openbox` shows the window fine; only multi-output is unsupported | Settings text only |
| C8 | Found: `metadata_parser.cpp` does parse colour space | The contract now says what's true: bit depth is never parsed, and colour space is only a default when EXIF and ICC are both absent | | No |
| C9 | Review CA-N1 to N4 | `serverPaths` is a process fact; one builder per (section, scope); D6 owns the bit-depth parser; populators live in the viewer sources | | No |

## RESOURCE-LEDGER

| # | Question | Decision | Alternatives rejected | Visible |
| --- | --- | --- | --- | --- |
| R1 | Cross-process windows | Per-process ledgers, a documented gap, revisited after B8 | Divide the cap by the live window count: wrongly penalises idle windows. A local coordinator: IPC with no measured need | No |
| R2 | GPU in the aggregate | Off in M0 and M1. In M2, on for unified memory or an unknown type | Always on: double counts discrete VRAM. Always off: undercounts integrated GPUs | No |
| R3 | Foreground headroom | `min(1 GiB, effectiveLimit / 2)`, with the fallback at 1 GiB plus the reserve (review RL-N1: scratch, transfer and the parent copy are all held at once) | A flat value: starves all background work at the fallback limit. Keep 768 MiB: refuses a decode that passes today | No |
| R4 | Owner granularity | One per service and canvas group, plus `compare:<slot>`. Per-pane attribution through `Request::tag` | One owner per pane: spends the owner cap for diagnostics alone | No |
| R5 | `Request::tag` into `perf_trace`? | No. Snapshot diagnostics only, since `tryReserve` runs under a lock on any thread. E2 fixes `cache_lookup` separately | Trace from the ledger: I/O risk on the render thread | No |
| R6 | Review B2, lead decision: PR 3 replaces its transfer class | The contracts state rules for an abstract transfer buffer: the reservation is released after the buffer is really unmapped and closed, and the GUI thread never blocks on one. No leak is charged for the process lifetime. E1, D1 and C3 start after PR 3 merges | Name PR 3's class: the contracts would break each time it changes | No |
| R7 | Found: `DisplayManager` holds six slots since F040 | Mapped as six, and F040's transactional admission and `referenceShed` are parity tests | | No |
| R8 | Review B6: a reservation made at `probe` can't name a `QImage` that doesn't exist | `commit(actualBytes, shareKey)` | Reserve after the copy: breaks reserve-first. Reserve twice: spurious refusals at the limit | No |
| R9 | Review RL-N2 to N6 | `IoScheduler` keeps its byte check until M2; retired owners; a public constructor for tests; `GpuTextures` out of the charge order; `Quality` never swaps shown pixels | | No |

## ASSET-GROUP

| # | Question | Decision | Alternatives rejected | Visible |
| --- | --- | --- | --- | --- |
| A1 | Default scope | File-only every time; the choice is not remembered | Remember the last choice: a later delete could silently take companions, which breaks "never implicit" | Yes |
| A2 | Collision behaviour | Exclude the ambiguous members with a reason and continue | Block until the user picks: stops a whole batch over one odd stem | Yes |
| A3 | Apple content identifier | Stem matching for B1; `Match::ContentId` reserved. Every companion is listed by name before anything happens | Parse content ids now: needs a HEIC and MOV parser for a rare false match that the preview already exposes | No |
| A4 | Do TIFF and PNG count as companions of a RAW? | No. New role `Derived`: TIFF, PNG and WebP are members only when they are the stem's sole image, so a sidecar can attach. Beside a RAW or Rendered file they are excluded, and the confirmation shows them | Treat them as Rendered: an "asset" delete could take an edited master. Ignore them entirely: PNG+XMP would never group | Yes |
| A5 | Cross-folder sidecars | Stay non-goals | | No |
| A6 | Found: the header is Qt-free, but its rules need Qt classes | B1 implements it in `src/app/asset_group.cpp`, in `glancelot_platform`; the header stays Qt-free | Move the extension tables to core: duplicates `RawPreviewExtractor` | No |
| A7 | Found: "more than one Rendered of one family" had no definition | Families are listed in the contract: jpeg, heif, avif, jxl | | No |
| A8 | Found: asset rename could fail halfway | Stop, reverse the members already renamed as a new operation with `undoOf`, and report each final name. It is `Kind::BatchRename`, so it needs the journal and arrives with B2 (review OJ-N5) | Leave the partial state: a split asset the user must repair by hand. Run it unjournaled: a crash splits an asset with no record | Yes |
| A9 | Review AG-N1, AG-N2 | Bound the expansion (`kMaxExpandedItems`, `kMaxExcludedListed`) and replace name guessing with one directory read per parent, within `kExpandBudgetMs`; past it, the confirmation says companions weren't all checked | Per-target probes: 250,000 stats on a 10,000-file network delete, and mixed case is never found | Yes, on a slow share |
| A10 | Review AG-N5: scoped entry points had no diff | `planTransfer`/`confirmTransfer` and `planRename`/`confirmRename`. B1 adds them, and B2 adds Overwrite and asset rename | A scope argument on `enqueueTransfer`: it executes at once, leaving no confirmation step | Yes |

## OPERATION-JOURNAL

| # | Question | Decision | Alternatives rejected | Visible |
| --- | --- | --- | --- | --- |
| J1 | Unwritable journal | Confirmed by the lead. Operations that work today keep working, with a notice and no undo. Sidecar writes, lossless transforms and batch rename, which exist only with the journal's protection, refuse (`requiresJournal`). A write failure after an operation has begun stops it before the next batch | Fail closed for everything (the draft): read-only profiles and portable media would lose delete, move and rename, a regression from today. Proceed for a single item only: arbitrary, since a batch trash is no less safe than one | Yes |
| J2 | Trash tokens | Record the token on both platforms. Advertise `RestoreFromTrash` per platform only after a real round-trip test passes there; otherwise `None` | Windows `None` until later: gives up undo without trying. Advertise from the API alone: a claim with no evidence | Yes |
| J3 | Preimage retention | The life of the operation record, within `kMaxJournalBytes`. A preimage that doesn't fit after pruning refuses that operation | A fixed number of days: a time bound does not bound disk | No |
| J4 | Overwrite's replaced original | Confirmed by the lead. B2 moves it to the trash after the atomic replace, which also gives Overwrite an undo: the `.~gb` backup on Windows, a hard link on POSIX | Trash the target *before* replacing (the draft): leaves the name empty for the whole copy. Remove Overwrite: COPY-SAFETY-01 already made it safe | Yes: replaced files land in the trash |
| J5 | Intent batch size | Fixed at 64 | Storage-aware: no measurement yet says it pays | No |
| J6 | `.~gb` recovery (lead decision, COPY-SAFETY-01 N1) | The backup path is durable in the intent before `ReplaceFileW`. At startup, reconcile restores the original when the target name is free and identity matches, using a move that can't overwrite; otherwise it reports, with **Show in folder**, **Put back** and **Move to Trash**. `Outcome::OriginalDisplaced` covers the non-crash 1177 | Scan disks for `.~gb*`: wakes sleeping drives, stalls on shares, and the random name doesn't say what file it was. Auto-trash when the target exists: never destroys without asking, even into the trash | Yes |
| J7 | Found: `partialPath` would delete an original in a case-only rename | Originals are recorded in `backupPath`, never `partialPath`; only partials may be deleted. Case-only renames (`.tmp_rename_<id>`) go through the same restore table | | No |
| J8 | Found: the draft's `.glancelot-partial-*` name | Reuse COPY-SAFETY-01's `.~gl<8 hex>.tmp` and its sweep. B2 moves non-Overwrite copies onto it too | A second temporary scheme | No |
| J9 | Found: the Overwrite row and `digest()` helper were stale | The row now describes COPY-SAFETY-01. The journal uses `QCryptographicHash::Sha256` directly, because `digest()` is file-local | | No |
| J10 | Backups with no journal record | Out of scope and untouched. A `dismiss` is refused and pruning skips an operation while its backup still exists | | No |
| J11 | Review B1, lead decision: a volume with no usable trash. The earlier text said an Overwrite confirmation already warned; none exists | `TrashBackend::probeTrash` runs at the plan step. **B2 builds the Overwrite prompt.** `Unavailable` or `Unknown` says replaced files are permanently deleted and needs explicit consent. A failed trash move never falls through to removal: the operation stops and reports, and the original stays where it is. A route that can't keep a backup needs the same consent | Learn it after the replace: too late to ask. Remove on `TrashUnavailable`: silently loses a file the user was told would land in the trash | Yes |
| J12 | Review B4: nothing could read an operation's items | `OperationJournal::items`, paged. `OperationSummary::reconciled` makes a pass durable (OJ-N4) | Parse `.gjl` files elsewhere: breaks the interface | No |
| J13 | Review B5: a preimage was recorded after the write | `ItemIntent::preimagePath`, `Operation::sealPreimage` before the item touches the original, and a reconcile table for in-place writes, so a Done transform is never retried | Keep it on the outcome: a crash leaves the only original unreferenced, and retry transforms twice | No |
| J14 | Review OJ-N1, OJ-N2 | `probe()` at the plan step puts the journal notice in the confirmation. `kMaxOperationFileBytes` 256 MiB and `kMaxJournalBytes` 1 GiB, because the old file cap held only 671 bytes per item. `LimitExceeded` takes the same route as an unwritable journal | Lower the item cap: refuses operations that work today | No |
| J15 | Review OJ-N3, N5 to N9 | Reconcile tables by kind, with `overwritten` identity checked first; asset rename journaled; the sweep kept only when the journal is unavailable, with an age bound; a case-only rename restores the old spelling; `replacedPath` on both platforms; directory fsync on POSIX | | No |

## COMPARE-PANES

| # | Question | Decision | Alternatives rejected | Visible |
| --- | --- | --- | --- | --- |
| P1 | `Delete` in compare scope | Removes the pane. Deleting the file needs the explicit delete command and its normal confirmation | Keep delete-current on the key: a muscle-memory press while culling would start a file deletion | Yes |
| P2 | Over-budget panes before D1 | `TooLarge`, with honest text; the quota isn't raised | Raise the quota while comparing: changes budgets with no E1 evidence | Yes |
| P3 | Does C3 depend on D1? | No. C3 uses D1's ladder if it has landed; `TooLarge` remains only where no labelled lower tier exists | Wait for D1: delays Track C for a large-image edge case | No |
| P4 | Compare decode concurrency | One besides the current-image decode, with `IoScheduler` admission | Storage-aware counts: no measurement, and this keeps memory predictable | No |
| P5 | Session-only compare | Never persisted, including by F015 (a `TabLocation` can't hold it) and F049 presets, which store `baseContentMode` | | Yes |
| P10 | Review CP-N1: nothing connected the session to the surface | `setSurface`, and the ledger owner in the constructor | Wire images beside the session: duplicates swap and removal state | No |
| P11 | Review CP-N2, N4 to N6 | No file access in `begin`; reclaim only when not exposed, not merely unfocused; `TooLarge` settled at probe, naming the limit; `compare:<slot>` quota 256 MiB; `deliver` uses DECODE-TIER's rule | | Yes: `TooLarge` wording |
| P6 | Found: the draft took pane generations from the controller's counter | The session owns its own counter. Incrementing `generation` would cancel the current image's decode (`token != generation`). `GenerationSource` only reads it for `DisplayManager` handoff | | No |
| P7 | Found: "the controller's normal decode path" can't serve a pane | C3 adds a path modelled on `startPrefetch`, because `decode` is bound to one worker and the selection | Route through `decode`: cancels on every navigation and collides with the current image | No |
| P8 | Found: `modeValid` is shared with `load` and presets | It stays at three values, and `setContentMode` gets its own compare check. The contract lists every F049 and monitor site | Widen `modeValid` (the draft's implication): a hand-edited setting would restore compare | No |
| P9 | Tab switch during compare | Ends compare | Keep it open: its panes belong to the previous folder and the selection has moved | Yes |

## Headers in this packet

Existing headers only gain appended enum values, defaulted trailing fields, new standalone types and comments. No existing function signature changes, and no `Q_PROPERTY`, `Q_INVOKABLE` or non-inline member is added to an existing QObject, because moc and existing callers would then need definitions in this packet. Those changes are frozen as exact diffs in each contract and applied by the implementing packet under its brief's grant.
