# Phase 2 contract decisions, November 5, 2026

Packet: PHASE2-CONTRACTS. Decided by the packet session under the lead's delegation: best performance and user experience, keeping today's behaviour where an option does. Contracts: [docs/contracts/](../contracts/).

Each row is an open question from the October 22 drafts, or a problem found while checking them against `main` at `b1d57cd`. "Visible" marks a decision the user will see. The lead may overrule any of them, and those are the ones to read first.

## META-ROLES

| # | Question | Decision | Alternatives rejected | Visible |
| --- | --- | --- | --- | --- |
| M1 | Read keys for the whole folder for `captured` and `dimensions`? | Yes, on the listing or navigation worker, with a budget of 20,000 files or 4 s on network storage. The old order stays until the keys are in (`sortPending`). When the budget runs out, unread files go last (`sortPartial`) | Restrict these sorts to the grid's loaded rows: breaks NAV-06 parity with traversal, so next/previous would disagree with the grid | Yes |
| M2 | Rating source | Embedded EXIF and XMP until C1. Then a sidecar overrides embedded (the Lightroom convention), through a `RatingOverlay` function that C1 installs and `readMetaKeys` calls | Merge rules inside the reader: it would need C1's naming profile. A separate C1 role: two sources of truth for one rating | When C1 lands |
| M3 | Capture time | Order on the wall-clock value, ignoring `OffsetTimeOriginal`; keep the offset for display | Normalise to UTC when an offset exists: mixed sources order differently from the times the user reads, and camera offsets are often unset or wrong | Yes |
| M4 | Add the new keys to the `sort` command cycle? | No. Menu and Settings only; the cycle stays at four keys | Add them: changes what an existing shortcut does, and triggers a metadata pass on a key press | No change |
| M5 | Cache placement | `src/app/metadata_keys.hpp`, Track A owned. C5 reads the `captureTime` role, which covers everything it groups | Shared header now: freezes an internal type no consumer needs | No |
| M6 | In-process parsing of untrusted headers | Accepted, as the HUD already does, bounded to 64 KiB per file, with a mandatory F4 fuzz target | Read keys through the decoder process: a process launch per file is far too slow for a 20,000-file pass | No |
| M7 | Found: the new keys fell into the *size* branch of `sortBefore` | `sortBefore` treats both as "every key missing" until A2, which matches the contract's empty-reader rule | Leave it, since nothing reaches them: a wrong order for any early caller of `fileSortBefore` | No |
| M8 | Found: `settings_policy.cpp:140` allows only four sort keys | A2 must add both, or a saved new key fails validation. In its migration table and tests | | No |

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

## RESOURCE-LEDGER

| # | Question | Decision | Alternatives rejected | Visible |
| --- | --- | --- | --- | --- |
| R1 | Cross-process windows | Per-process ledgers, a documented gap, revisited after B8 | Divide the cap by the live window count: wrongly penalises idle windows. A local coordinator: IPC with no measured need | No |
| R2 | GPU in the aggregate | Off in M0 and M1. In M2, on for unified memory or an unknown type | Always on: double counts discrete VRAM. Always off: undercounts integrated GPUs | No |
| R3 | Foreground headroom | `min(768 MiB, effectiveLimit / 2)` | A flat 768 MiB: starves all background work under a 1 GiB limit, which is the fallback value | No |
| R4 | Owner granularity | One per service and canvas group, plus `compare:<slot>`. Per-pane attribution through `Request::tag` | One owner per pane: spends the owner cap for diagnostics alone | No |
| R5 | `Request::tag` into `perf_trace`? | No. Snapshot diagnostics only, since `tryReserve` runs under a lock on any thread. E2 fixes `cache_lookup` separately | Trace from the ledger: I/O risk on the render thread | No |
| R6 | Found: PR 3's transfer reaper | The Transfer reservation lives with the segment: released after the reaper deletes it; a leaked segment stays charged and visible. E1 starts after PR 3 merges | Release at kill time: undercounts mapped memory while a reap blocks | Diagnostics only |
| R7 | Found: `DisplayManager` holds six slots since F040 | Mapped as six, and F040's transactional admission and `referenceShed` are parity tests | | No |

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
| A8 | Found: asset rename could fail halfway | Stop, reverse the members already renamed, and report each final name | Leave the partial state: a split asset the user must repair by hand | Yes |

## OPERATION-JOURNAL

| # | Question | Decision | Alternatives rejected | Visible |
| --- | --- | --- | --- | --- |
| J1 | Unwritable journal | Operations that work today keep working, with a notice and no undo. Sidecar writes, lossless transforms and batch rename, which exist only with the journal's protection, refuse (`requiresJournal`). A write failure after an operation has begun stops it before the next batch | Fail closed for everything (the draft): read-only profiles and portable media would lose delete, move and rename, a regression from today. Proceed for a single item only: arbitrary, since a batch trash is no less safe than one | **Yes, please check** |
| J2 | Trash tokens | Record the token on both platforms. Advertise `RestoreFromTrash` per platform only after a real round-trip test passes there; otherwise `None` | Windows `None` until later: gives up undo without trying. Advertise from the API alone: a claim with no evidence | Yes |
| J3 | Preimage retention | The life of the operation record, within `kMaxJournalBytes`. A preimage that doesn't fit after pruning refuses that operation | A fixed number of days: a time bound does not bound disk | No |
| J4 | Overwrite's replaced original | B2 moves it to the trash after the atomic replace, which also gives Overwrite an undo: the `.~gb` backup on Windows, a hard link on POSIX. A volume with no trash removes it as today, with undo `None` | Trash the target *before* replacing (the draft): leaves the name empty for the whole copy. Remove Overwrite: COPY-SAFETY-01 already made it safe | **Yes, please check**: replaced files now land in the trash |
| J5 | Intent batch size | Fixed at 64 | Storage-aware: no measurement yet says it pays | No |
| J6 | `.~gb` recovery (lead decision, COPY-SAFETY-01 N1) | The backup path is durable in the intent before `ReplaceFileW`. At startup, reconcile restores the original when the target name is free and identity matches, using a move that can't overwrite; otherwise it reports, with **Show in folder**, **Put back** and **Move to Trash**. `Outcome::OriginalDisplaced` covers the non-crash 1177 | Scan disks for `.~gb*`: wakes sleeping drives, stalls on shares, and the random name doesn't say what file it was. Auto-trash when the target exists: never destroys without asking, even into the trash | Yes |
| J7 | Found: `partialPath` would delete an original in a case-only rename | Originals are recorded in `backupPath`, never `partialPath`; only partials may be deleted. Case-only renames (`.tmp_rename_<id>`) go through the same restore table | | No |
| J8 | Found: the draft's `.glancelot-partial-*` name | Reuse COPY-SAFETY-01's `.~gl<8 hex>.tmp` and its sweep. B2 moves non-Overwrite copies onto it too | A second temporary scheme | No |
| J9 | Found: the Overwrite row and `digest()` helper were stale | The row now describes COPY-SAFETY-01. The journal uses `QCryptographicHash::Sha256` directly, because `digest()` is file-local | | No |
| J10 | Backups with no journal record | Out of scope and untouched. A `dismiss` is refused and pruning skips an operation while its backup still exists | | No |

## COMPARE-PANES

| # | Question | Decision | Alternatives rejected | Visible |
| --- | --- | --- | --- | --- |
| P1 | `Delete` in compare scope | Removes the pane. Deleting the file needs the explicit delete command and its normal confirmation | Keep delete-current on the key: a muscle-memory press while culling would start a file deletion | Yes |
| P2 | Over-budget panes before D1 | `TooLarge`, with honest text; the quota isn't raised | Raise the quota while comparing: changes budgets with no E1 evidence | Yes |
| P3 | Does C3 depend on D1? | No. C3 uses D1's ladder if it has landed; `TooLarge` remains only where no labelled lower tier exists | Wait for D1: delays Track C for a large-image edge case | No |
| P4 | Compare decode concurrency | One besides the current-image decode, with `IoScheduler` admission | Storage-aware counts: no measurement, and this keeps memory predictable | No |
| P5 | Session-only compare | Never persisted, including by F015 (a `TabLocation` can't hold it) and F049 presets, which store `baseContentMode` | | Yes |
| P6 | Found: the draft took pane generations from the controller's counter | The session owns its own counter. Incrementing `generation` would cancel the current image's decode (`token != generation`). `GenerationSource` only reads it for `DisplayManager` handoff | | No |
| P7 | Found: "the controller's normal decode path" can't serve a pane | C3 adds a path modelled on `startPrefetch`, because `decode` is bound to one worker and the selection | Route through `decode`: cancels on every navigation and collides with the current image | No |
| P8 | Found: `modeValid` is shared with `load` and presets | It stays at three values, and `setContentMode` gets its own compare check. The contract lists every F049 and monitor site | Widen `modeValid` (the draft's implication): a hand-edited setting would restore compare | No |
| P9 | Tab switch during compare | Ends compare | Keep it open: its panes belong to the previous folder and the selection has moved | Yes |

## Headers in this packet

Existing headers only gain appended enum values, defaulted trailing fields, new standalone types and comments. No existing function signature changes, and no `Q_PROPERTY`, `Q_INVOKABLE` or non-inline member is added to an existing QObject, because moc and existing callers would then need definitions in this packet. Those changes are frozen as exact diffs in each contract and applied by the implementing packet under its brief's grant.
