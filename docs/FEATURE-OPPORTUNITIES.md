# Feature opportunities and UX strategy

Planning date: October 17, 2026. This is a comprehensive opportunity inventory for a native, offline Windows/Linux image viewer with an optional Docker browser desktop. It is not a shipped-capability list or an instruction to implement every idea. The [96 requirement rows](REQUIREMENTS.md), [execution queue](WORK-QUEUE.yaml) and verification records remain authoritative. Performance work is specified in [PERFORMANCE-STRATEGY.md](PERFORMANCE-STRATEGY.md).

## 1. Product direction and prioritization

Optimize five workflows: find a folder/image, inspect it accurately, compare and cull, organize or hand off selected files, and recover confidently when storage or the application fails. Preserve immediate open-without-import, deterministic traversal, supplied branding, full keyboard access and source safety. Every new control must earn its place through one of these workflows.

The eight approved tasks retain first priority: PATH-BAR-01, PANES-01, GRID-PREVIEW-01, FILE-GRID-01, DELETE-01, DELETE-KEYS-01, UI-SETTINGS-01 and TOPBAR-THEME-01. Dependencies still determine executable order: mixed entries precede full mixed-entry grid/preview integration. This document does not reorder them or weaken the platform/cache/I/O gates that follow.

Candidate notation:

- **E**: extends or completes an existing requirement. The referenced requirement retains its original milestone and acceptance unless explicitly amended.
- **N**: a new proposal, requiring a requirement/task packet before implementation.
- **C**: conditional proposal with a feasibility, safety or platform gate.
- **P1/P2/P3**: recommended next, later, or specialist priority after the approved queue. These are judgments about likely workflow benefit, not measured user demand.
- **S/M/L/XL**: relative implementation and review complexity, not calendar estimates. S is localized; M spans a few components; L crosses subsystems/platforms; XL needs new infrastructure or qualification. Each row includes a minimum acceptance test; project-wide safety, accessibility and performance gates also apply.

Score promoted work using frequency of use, time saved, error reduction, accessibility/reach, confidence in the benefit, and total build/maintenance cost. Prefer removal of repeated friction over more toolbar buttons. Validate with generated-media workflow trials and voluntary user feedback; no telemetry or automatic collection is needed. Score separately for native and remote users where their needs differ.

## 2. Navigation and finding images

| ID | Feature and user value | Status / priority / size | Dependencies and minimum acceptance |
| --- | --- | --- | --- |
| F001 | Back/forward folder history with preserved scroll, selection and preview; makes exploration reversible | N / P1 / M | PATH-BAR-01, FILE-GRID-01; history distinguishes folder visits from image steps; stale/inaccessible locations can be skipped without losing the current location. |
| F002 | Breadcrumbs plus path completion; jump to ancestors and siblings without navigating the whole tree | E UX-13 / P1 / M | PATH-BAR-01; cancellable completion only for the typed scope; clipboard, UNC, Unicode, keyboard and long paths retain exact text semantics. |
| F003 | Favorites, recent folders and pinned locations with optional aliases | E UX-03, FAVORITES-01 / P1 / M | Existing tree/settings; reorder by keyboard, clear history, mark missing mounts and avoid probing every disconnected favorite at startup. |
| F004 | Instant current-folder filename search and type-to-select | N / P1 / M | FILE-GRID-01; incremental matching with clear scope/count, Escape reset and IME support; search never steals text from another field. |
| F005 | Explicit recursive search with progress, stop and partial results | N / P2 / L | IO-01, stable entry IDs; choose root/depth/types, avoid link cycles, bound outstanding work, explain incomplete results and stop publishing stale matches. |
| F006 | Saved filter/search recipes without a mandatory catalog | E NAV-05 / P2 / M | FILTER-01, F004/F005; serialize query and root policy rather than stale results; preserve persistent versus temporary filters across navigation. |
| F007 | Scope/status controls for traversal root, hidden files, exclusions and offline mounts | E NAV-08, UX-16 / P1 / M | FILE-GRID-01; visible scope and reset; excluding a folder does not delete it; disconnected storage does not freeze the UI. |
| F008 | Jump to image index, filename or capture date with folder-boundary feedback | E NAV-02, NAV-06 / P2 / M | Image model and lazy date metadata; deterministic ordering and missing dates; show destination before expensive remote search. |

## 3. Grid, selection and browsing workspace

| ID | Feature and user value | Status / priority / size | Dependencies and minimum acceptance |
| --- | --- | --- | --- |
| F009 | Details/list mode alongside thumbnail grid; inspect filenames, dimensions, size and dates efficiently | N / P1 / L | FILE-GRID-01, META-HUD-01; shared selection/sort, virtualized rows, configurable columns and lazy metadata; changing mode preserves the visible anchor. |
| F010 | Thumbnail size presets, continuous zoom and density controls | E UX-17 / P1 / M | UI-SETTINGS-01; bounded thumbnail size buckets; resizing does not re-decode every source or move the selected item offscreen. |
| F011 | Rectangle selection, invert selection and explicit selection summary | E UX-16 / P1 / M | FILE-GRID-01; keyboard equivalents, offscreen selection indicators, cancellation and consistent Shift anchors after filtering/sorting. |
| F012 | Selection basket spanning folders for later culling or handoff | E META-06, META-07 / P2 / L | Stable identities and file-ops snapshot; distinguish basket, temporary tags and current selection; missing or renamed entries remain explainable. |
| F013 | Group headers by date/type/rating with collapse and count | N / P2 / L | Metadata/filter models; missing values form an explicit group; collapsed groups do not decode hidden thumbnails or alter traversal semantics. |
| F014 | Thumbnail badges for rating, type, dimensions, errors and preview quality | E UX-02, META-05 / P1 / M | Lazy metadata; badges remain readable at small sizes, use labels/accessibility beyond color, and avoid overlapping filenames. |
| F015 | Tabs and named workspace sessions for independent folder investigations | N / P2 / L | PANES-01, stable window slots and shared admission; closing a tab releases work; restore lazily; reopening does not multiply memory limits. |
| F016 | Large-folder progressive loading with stable anchors and useful empty/error states | E UX-16, PERF-05 / P1 / L | IO-01; deterministic final sort, progress versus total distinction, load-more/limit visibility, and no silent loss of entries beyond current model caps. |

## 4. Safe file organization

| ID | Feature and user value | Status / priority / size | Dependencies and minimum acceptance |
| --- | --- | --- | --- |
| F017 | Recoverable operation history and undo where the platform supports it | N / P1 / L | DELETE-01; record exact targets/outcomes; restore conflicts handled; never claim undo for permanent deletion or expired external trash. |
| F018 | Rename a file/folder and create a folder directly in the browser | N / P1 / L | Mutation service; invalid/reserved names, case-only rename, collisions, source replacement and failed permissions; tree/preview/history reconcile atomically where possible. |
| F019 | Batch rename with token templates and a complete before/after preview | N / P2 / L | F018, metadata; collision detection including case folding; dry run, cancellation, paired-file policy and restart recovery for partial batches. |
| F020 | Copy/move selected or tagged items with a nonblocking queue | E META-07 / P1 / L | File-ops service and shared I/O admission; cross-volume copy verifies before source removal; pause/cancel and per-item progress distinguish committed work. |
| F021 | Drag/drop between panes, folders and external file managers | N / P2 / L | F020, platform adapters; explicit copy/move modifier feedback, safe drop targets, denied operations and keyboard alternatives; no accidental move on ambiguous drop. |
| F022 | RAW+JPEG, sidecar and motion-pair operation policy | E META-05, META-07, ADV-09 / P1 / L | Asset grouping; preview every affected companion, handle same-stem collisions and let users choose file-only versus asset operations. |
| F023 | Exact duplicate finder, scoped to chosen folders | N / P2 / L | IO-01; size/hash stages with final content validation, cancellation, hard-link awareness and review-only results; never auto-delete. |
| F024 | Visual similarity/near-duplicate groups for burst cleanup | C / P3 / XL | F023, local bounded analysis; show confidence and retain originals; no automatic destructive choice, model download or remote inference. |

## 5. Culling, comparisons and review

| ID | Feature and user value | Status / priority / size | Dependencies and minimum acceptance |
| --- | --- | --- | --- |
| F025 | Pick/reject, stars and color labels with keyboard advance | E META-05, META-06 / P1 / L | Sidecar conflict service; distinguish ephemeral marks from saved metadata; failed writes never look successful; auto-advance is optional. |
| F026 | Two-up/four-up comparison with linked pan/zoom and candidate replacement | E ADV-01, VIEW-02 / P1 / L | PANES-01, shared memory admission; deterministic active pane, aspect-ratio anchors and no source deletion when removing a pane. |
| F027 | A/B blink, wipe and difference inspection | N / P2 / L | F026, color pipeline; aligned transforms, explicit normalization/color domain and reduced-motion alternative; difference is not a claim of perceptual equivalence. |
| F028 | Burst/sequence stacks with user-selected representative | N / P2 / L | Metadata/time sorting; reversible grouping, manual correction, missing timestamps and unequal camera clocks; collapsed stacks remain countable. |
| F029 | Culling progress and next-unreviewed navigation | N / P2 / M | F025, optional session state; scope visible, progress survives sorting, and skipped/unsupported items do not falsely count as reviewed. |
| F030 | Session-only collections, saved selection manifests and review handoff | N / P2 / M | F012, portable settings; relative/absolute path policy, moved-root repair and no required import/database. |
| F031 | Slideshow with pause, shuffle seed, fit policy and presentation monitor | E UX-10 / P2 / L | Animation-aware clock, bounded prefetch; user input cancels reliably, missing files skipped with limits, unplugged monitor recovers. |
| F032 | Contact sheet export with filenames and chosen metadata | N / P2 / L | Bounded export worker; preview pagination, output dimensions/quality and overwrite policy; export never alters source media. |

## 6. Viewing and format breadth

| ID | Feature and user value | Status / priority / size | Dependencies and minimum acceptance |
| --- | --- | --- | --- |
| F033 | Progressive fit-sized preview followed by full detail | E PERF-03, PERF-06 / P1 / L | Decoder tiers and revision ownership; visible quality label, identical orientation/color intent, stable pan/zoom, no stale replacement. |
| F034 | Tiled/region viewing of huge panoramas and scans | E PERF-06 / P2 / XL | Tile-capable codecs and GPU budget; hard memory ceiling, pan cancellation and honest fallback for non-region-decodable formats. |
| F035 | Common raster and RAW embedded-preview coverage | E FORMAT-02, FMT-02, FMT-03, FMT-06 / P1 / L | Codec qualification; tested subtype/camera fixtures, licensing/provenance, malformed inputs and explicit absent-preview behavior. |
| F036 | Animation and multipage document controls | E ADV-06, ADV-07 / P2 / L | Bounded frame cache; correct disposal/timing, seek and pause; TIFF pages have no invented frame duration. |
| F037 | Quick loupe, zoom presets, navigator overview and zoom-lock polish | E VIEW-02, VIEW-03 / P1 / M | Viewport contract; predictable anchor, focus-loss/hold cancellation and physical 1:1 under fractional DPI. |
| F038 | On-canvas pixel sampling, guides, clipping warnings and histogram | E ADV-04, ADV-05, ADV-12, ADV-13 / P2 / L | Color/metadata workers; source versus displayed domains labeled; bounded computation and freshness on every transform. |
| F039 | Archive/comic/continuous-strip browsing | E ADV-02, ADV-10, ADV-11 / P3 / XL | Virtual source provider; bounded decompression, no archive extraction while viewing, correct cover/LTR/RTL layout and sparse visible working set. |
| F040 | Always-on-top reference window, opacity and safe click-through | E UX-09 / P2 / L | Platform capability checks; an always-accessible recovery route, actual compositor tests and clear unsupported states. |

## 7. Metadata, color and professional inspection

| ID | Feature and user value | Status / priority / size | Dependencies and minimum acceptance |
| --- | --- | --- | --- |
| F041 | Searchable metadata inspector and configurable favorite fields | E META-01, UX-04 / P1 / M | META-HUD-01; async parsing, raw versus formatted values, units/time zones and malformed/unbounded metadata tests. |
| F042 | Accurate capture-date/dimension sorts and metadata filter chips | E NAV-06, NAV-05 / P1 / L | Metadata scheduler; missing data last by defined policy, stable ties, cancel/re-sort and no metadata read per QML delegate. |
| F043 | Explicit image/preview/color capability status | E COLOR-01, COLOR-02, UX-04 / P1 / M | Backend/decoder capability model; distinguish source bit depth from working/output precision and preview size from full source. |
| F044 | Per-monitor ICC management and validated SDR output | E COLOR-01, COLOR-02 / P1 / XL | Native physical monitor testing; no double transform, invalid profiles, profile changes and mixed-monitor movement. |
| F045 | Local AI-generation metadata extraction and prompt/workflow copy | E META-02 / P2 / M | Bounded versioned parsers; metadata is never executed and absent/unknown schemas remain readable as text. |
| F046 | Privacy-aware derived export with optional GPS/metadata stripping | N / P2 / L | Export worker, metadata schemas; clear preserved/removed fields, filename/output policy, verify exported bytes and retain originals. |
| F047 | Lossless JPEG transforms and copy-as-rendered workflows | E META-08, META-09 / P2 / L | Explicit mutation/export commands; MCU-edge/orientation cases and metadata retention; viewing rotation never silently becomes a source edit. |
| F048 | HDR/high-bit-depth/gain-map and specialist channel inspection | E COLOR-04..07, FMT-07..10 / P3 / XL | Existing conditional requirements; calibrated output and format-specific evidence; SDR fallback and truthful precision labels. |

## 8. Customization, accessibility and daily usability

| ID | Feature and user value | Status / priority / size | Dependencies and minimum acceptance |
| --- | --- | --- | --- |
| F049 | Named layout presets and one-command focus mode | E UX-14, UX-17 / P1 / M | PANES-01, UI-SETTINGS-01; reset/recover hidden panes, per-window scope and no loss of selection when switching layouts. |
| F050 | Searchable settings, changed-from-default view and import preview | E UX-17, IN-06 / P1 / M | UI-SETTINGS-01; identify scope and restart requirements, discard/revert edits, preserve unknown fields and validate portable imports. |
| F051 | Shortcut recorder, conflict explanations and printable cheat sheet | E IN-01, IN-04, IN-05 / P1 / L | INPUT-02; keyboard-layout/IME and scope tests; unavailable OS shortcuts explained; no destructive command capture leaks. |
| F052 | Full keyboard focus navigation and assistive-technology support | E UX-11 / P1 / L | Qt accessibility; NVDA/Orca trials for path/tree/grid/settings; logical focus restore, semantic selection, names and live-status restraint. |
| F053 | Text scale, high contrast, reduced motion and touch-target options | E UX-11, UX-17 / P1 / M | Existing tokens; 200% text, long translated labels, no color-only state, supported narrow layouts and no hover-only essential actions. |
| F054 | Localization, right-to-left layout and locale-aware display formatting | N / P2 / L | Translation extraction and native text support; paths remain exact, traversal comparator policy stays deterministic, bidirectional text and plural counts tested. |
| F055 | Context menus and customizable toolbar with equivalent command routes | E IN-01, UX-17 / P1 / M | Command registry; menu enablement matches keyboard/palette, clear destructive scope and reachable actions in compact widths. |
| F056 | Small first-run hints, shortcut discovery and context-sensitive help | N / P2 / S | Stable workflows; skippable/replayable hints, no blocking tour, no repeated prompts and an offline help route. |

## 9. Reliability, diagnosis and resource control

| ID | Feature and user value | Status / priority / size | Dependencies and minimum acceptance |
| --- | --- | --- | --- |
| F057 | Recovery of browsing workspace after crash or power loss | E UX-12, SYS-03 / P1 / L | Window slots/settings; restore lazily, explicit-open wins, corrupted state safe mode and interrupted migration tests. |
| F058 | Local performance/diagnostics panel with exportable redacted report | E SYS-05 / P1 / M | PERF-MEASURE proposal; show measured backend, memory, queue, cache and source latency; paths/EXIF excluded by default; no upload. |
| F059 | Performance profiles: Auto, Battery, Low memory, Fast browsing, Remote | N / P1 / L | Shared admission plus benchmarks; changes explain tradeoffs, respect caps and never silently reduce exact-inspection quality. |
| F060 | Pause background work, clear disposable caches and choose cache location | E PERF-04, PERF-05 / P1 / M | Cache ownership; cancel safely, report reclaimable versus active bytes; clearing cache cannot erase metadata, tags or preferences. |
| F061 | Structured recovery for unavailable shares, corrupt images and missing codecs | E NAV-04, SYS-02 / P1 / M | Async errors; retry/skip/open-folder options, no modal storm, failed source never replaces a valid preview with unrelated content. |
| F062 | In-app operation center with progress, pause/cancel and failed-item retry | E META-07 / P1 / L | Mutation scheduler; stable target snapshots, restart reconciliation, skip already completed work and honest cancellation boundaries. |
| F063 | File-change reconciliation and explicit refresh | E META-04, NAV-08 / P1 / L | Bounded watchers; replacement/rename/delete and overflow recover; selected identities survive where possible, changed revisions invalidate caches. |
| F064 | Safe mode, configuration diff and settings backup/restore | E SYS-03 / P1 / M | Atomic settings; recover without deleting originals/unknown preferences, isolate bad layout/renderer overrides and preserve last-good state. |

## 10. Native integration and external workflows

| ID | Feature and user value | Status / priority / size | Dependencies and minimum acceptance |
| --- | --- | --- | --- |
| F065 | Reveal in system file manager and open a terminal at the current folder | N / P1 / M | Platform adapter; explicit user action, argument arrays without shell interpolation, correct Unicode paths and unavailable-tool feedback. |
| F066 | Open-with/external-editor presets and nine configurable tool slots | E META-10 / P1 / L | Scoped arguments; single/selection/tagged targets, Unicode/metacharacters, missing executable and command-size limits; no auto-execution from media. |
| F067 | Copy full/relative path, file objects, original bitmap or rendered image | E META-09 / P1 / L | Clipboard adapters; clearly separate source and rendered semantics, bounded transfer, accurate color/alpha and Linux/Windows native tests. |
| F068 | Shell associations, open-folder action and deterministic instance routing | E UX-08, SYS-04 / P1 / L | Packaging/IPC; race-safe launch, explicit path overrides resume, quoted/long paths and clean install/uninstall. |
| F069 | Command-line folder, image, layout and temporary-config entry points | N / P2 / M | Command registry; documented stable arguments, diagnostic exit codes and no hidden mutations; GUI state matches equivalent commands. |
| F070 | Selection manifest or CSV export for downstream tools | N / P2 / M | Snapshot identities/metadata; Unicode/quoting, spreadsheet formula-injection-safe option, relative-root choice and no metadata parsing on UI thread. |
| F071 | Lightweight image conversion/resize/export presets | C / P2 / L | Qualified encoders/export service; preview dimensions, color/metadata/quality policy, output conflicts and cancellation; never default to overwriting originals. |
| F072 | User-invoked GPS map handoff with privacy context | E META-03 / P2 / S | Valid coordinates only; explain external browser request and provider; never fetch maps in background. |

## 11. Remote desktop, Docker and deployment UX

The current container is one shared desktop/cursor/session. Remote proposals must preserve that model unless a separately scoped isolation design is approved. Browser convenience must not imply native color or pixel fidelity.

| ID | Feature and user value | Status / priority / size | Dependencies and minimum acceptance |
| --- | --- | --- | --- |
| F073 | Remote quality/responsiveness presets and connection status | N / P1 / M | PERF-REMOTE proposal; explain resolution, compression and frame-cap effects; detect reconnect and keep settings per client where supported. |
| F074 | Remote desktop resize-to-browser with a fixed-size fallback | C / P2 / M | Server/client capability probe; debounce resize, bound total pixels and preserve pane minimums; simultaneous clients cannot endlessly resize each other. |
| F075 | Clear server-path and read-only-mount indicators | N / P1 / S | Source capability model; paste refers to server paths, disabled mutation explains mount permissions, and host client paths are not guessed. |
| F076 | Clipboard bridge with explicit paste and shortcut forwarding | C / P1 / M | noVNC/browser constraints; text size limits, permission failures, focus release and no automatic bidirectional clipboard harvesting. |
| F077 | Opt-in browser upload/download into an explicit staging area | C / P3 / XL | New authenticated transfer design; size/quota, filename and content validation, cancellation, no source overwrite, separate permissions and no mandatory server catalog. |
| F078 | Single-controller/view-only access modes and visible shared-session state | C / P2 / L | Remote session controller; no assumption that a view-only client is an authorization boundary; server enforces mutation/input rights. |
| F079 | Deployment self-check for mounts, GPU, limits, free space and persistent settings | N / P1 / M | PERF-CONTAINER proposal; redact secrets, show actionable status and distinguish HTTP health from a rendered usable desktop. |
| F080 | Reproducible portable/native/container packages and offline repair guidance | E SYS-04 / P1 / L | PLATFORM-01; clean-machine tests, pinned provenance, rollback/config compatibility and explicit supported platforms; no automatic updates. |

## 12. Recommended promotion sequence

Complete the eight approved workspace tasks, carrying shared resource and accessibility acceptance into their implementation. The strongest follow-on candidates are:

| Sequence | Package | Why it belongs early | Dependency/gate |
| --- | --- | --- | --- |
| A | F001/F003/F004: history, favorites and fast local search | Removes repeated navigation effort in almost every session | Mixed grid/path model; no eager recursive indexing |
| B | F009/F014/F016: details, useful badges and progressive large folders | Makes the file-manager workflow usable beyond photos and small folders | Lazy metadata and bounded model updates |
| C | F017/F018/F020/F022/F062: recoverable organization | Completes explicit deletion with predictable rename/copy/move and companion handling | Mutation correctness, disposable failure fixtures and independent review |
| D | F025/F026/F037: efficient culling and comparison | Direct benefit to photo selection and image inspection | Shared admission, sidecar policy and accurate viewport behavior |
| E | F041/F042/F043/F044: metadata and trustworthy color | Helps users find and judge images correctly | Format-specific metadata and physical monitor qualification |
| F | F051/F052/F053/F055: complete input and accessibility | Prevents the richer workspace from becoming slower or harder to use | Native keyboard and assistive-technology evidence |
| G | F058/F059/F060/F061/F063/F064: visible control and recovery | Makes performance and failure states understandable and recoverable | PERFORMANCE-STRATEGY work packages |
| H | F065/F066/F067/F068: everyday OS handoff | Fits existing tools without building an editor/catalog | Native adapter and packaging tests |
| I | F073/F075/F076/F079: remote usability | Benefits Docker users without committing to a separate web application | Browser/server end-to-end tests |

These packages are recommendations, not new active tasks. Use platform qualification, CACHE-01 and IO-01 already in the queue as enabling work rather than duplicating them. Search, mutation, metadata and thumbnail work must compete through the same scheduler.

## 13. Detailed interaction contracts to settle before implementation

1. **Location, focus, preview and selection:** current browsing folder, focused pane/item, opened image, multiselection, basket and temporary tags are separate concepts. Every destructive or external command names its target source. A delayed confirmation retains its original snapshot and revalidates identity before acting.
2. **Workspace ownership:** layout settings are per window slot or a named preset; general themes/defaults are user-wide. Switching a preset never changes the folder or deletes a selection. Freeze locks layout; pin controls visibility. Offer keyboard moves and an always-reachable layout reset.
3. **Progressive results:** users see loading, partial, empty, filtered-empty, inaccessible, offline and failed states distinctly. Counts say when they are incomplete. Incremental sorting must not continuously shift the keyboard target.
4. **Quality:** a small embedded preview, fit-sized decode, full-resolution image and remote lossy frame must never share a misleading quality label. Exact inspection fixes quality; adaptive performance may defer detail but cannot silently change the interpretation of pixels.
5. **Mutations:** confirmation identifies files, folders, descendants and companions. Undo is advertised only when supported. Read-only sources remain browsable. Permanent deletion, overwrites and cross-volume moves retain explicit conflict/recovery semantics.
6. **Preferences:** consistent apply/live-preview/revert behavior, visible scope, per-section reset and portable versioning. Do not expose inactive experimental knobs as finished functionality.
7. **Native UI craft:** apply application-interface-design, layout/grids/spacing, typography, geometry, iconography, motion and responsive-layout to Qt/QML. Preserve supplied theme tokens and branding. No web framework migration is implied. Require independent design-review for substantial UI changes.
8. **Accessibility:** complete commands without a pointer; focus remains visible across panes, dialogs and restoration. Use native accessible names/roles and avoid high-frequency announcements while scrolling. No important state conveyed only by color, animation or a hover tooltip.

## 14. How to accept or reject a proposal

For a promoted candidate, create one task packet with requirement mapping, target users/workflow, measurable benefit, owned files, frozen contracts, failure/cancellation states, platform limits, resource ceilings and rollback. Reuse existing primitives before adding abstractions. Assign an isolated branch/worktree/build directory and obtain independent review before integrated checks and completion.

Run five repeatable tasks: locate a named image in a nested tree; compare and mark a burst; organize a mixed selection; recover from a disconnected share; restore a customized workspace. Record completion time, keystrokes, recoverable mistakes, focus failures and subjective confusion. Repeat with keyboard only, large text and a remote connection. Keep results local and redact paths. Improvement in one scenario must not silently break another.

Reject or defer mandatory cloud accounts/catalogs, telemetry, background whole-machine indexing, automatic media rewriting, auto-delete based on AI, unrestricted scripting/plugins, a full destructive editor, social publishing and paid infrastructure. Native ARM64, macOS, independent multi-user web sessions and hardware video streaming remain separate feasibility/scope decisions. This keeps the inventory broad while protecting the project's core purpose.
