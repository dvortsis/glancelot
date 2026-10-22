# SAFE-MODE-01 transaction and ownership cases (proposal)

Planning only against FS candidate825f7f2 and main8b28060. No shared declaration,
startup option, queue state or test target is frozen here. Preserve the entire
F064/F057 scope in the existing contract/API proposals. Implementation starts
only in its assigned next worktree after the lead freezes the packet under lock.

## Exact raw revision proposal

Use SHA256 over the following binary sequence, independent of JSON formatting:
ASCII `Glancelot.SettingsPair`, one zero byte, version byte1, then primary and
backup in that fixed order. Each member contributes a presence byte0/1, an
unsigned64 little-endian byte count, and32 SHA256 bytes. For absence, length0
and32 zero bytes; present-empty uses length0 and SHA256(empty). Presence, length
and digest all participate. Hash the exact bounded bytes read, never reserialized
JSON. A failed/oversized read returns an error without a usable revision.

Required discriminating cases: absent vs present-empty; whitespace-only edit;
same JSON with reordered keys; backup-only replacement; primary replacement
after preview; missing backup created after preview; same primary/backup bytes
under another configuration namespace. Bind the plan separately to the resolved
configuration namespace, package revision and requested section/slot scope.
No path hash is evidence of permissions, file identity or exclusive ownership.

## Current known-field inventory to validate before freezing

Observed controller read/write fields: boundary(continue/stop/loop),
wheel(navigate/zoom/pan), filmstrip/tree/treePinned(bool), textScale[0.75,2],
reducedMotion(bool), sort.key(name/modified/size/shuffle), sort.descending(bool),
sort.seed(integer in[1,2^53), absent/zero retains ordinary default semantics),
and bindings per currently registered command ID using the existing conflict
validator. Treat nearest/backdrop from resetSection's deletion list as unknown:
no current controller persistence establishes them as owned recovery fields.

Theme owns only appearance.family(valid bundled catalog ID) and
appearance.mode(system/dark/light). Preserve unknown appearance/sort/binding
members. A future command binding cannot become known merely by matching a
string pattern; derive known IDs from the shipped registry after all merges.

Window positions own windows.<owned-slot>.folder/image/saved. Current
writePosition replaces that entire slot object; F057 must merge its known fields
to preserve nested unknown data instead of propagating that existing limitation.
Layouts own only the current-version slot's version/locked, center orientation,
ratio/mode, and known panes' region/order/extent/visible/frozen fields. Preserve
other slots, unknown panes and nested fields. A newer layout version refuses
that layout restore rather than downgrading it. An invalid known source value
must be reported in preview; never silently apply a partial applicable plan.

## Proposed ownership rules

Global-only capture/preview needs no window claim. It must never construct a
ViewerController, ThemeManager or PaneLayout merely to discover defaults.
Interactive per-slot restore/checkpoint requires a value token bound to the
already held WindowSlot claim: resolved namespace, slot, pid and start stamp.
Validate that exact live record at commit; slot0 is never an alternative.
Do not trust a caller-supplied number or an arbitrary saved recovery pid.

For maintenance CLI per-slot restore, propose an explicit offline mode: validate
sessions.json under its existing lock and refuse while any viewer is live or
bookkeeping is corrupt, unreadable, oversized or future-versioned. Missing state
is distinguishable from damaged state. Do not fabricate an ordinary opening-order
claim or reset the counter to gain access to another slot. A future frozen API
must represent the offline lease distinctly from a live owned-window token.

If commit holds both locks, freeze and enforce sessions lock before settings
lock everywhere. Hold the ownership lease through checked settings commit;
release it on every no-op/failure/cancellation path. Validate package bytes
before either lock. Filesystem operations remain worker/maintenance-only; no
interactive lock waits. Existing FileLock is OS-held, not a stale-file protocol:
never unlink its path as a way of taking ownership.

Required process cases: a second viewer starts between preview and commit;
the original claimant exits; slot1 is reused in a new session; pid is reused
with another start stamp; caller swaps config namespace; maintenance sees
unknown/future live records; lease contention, cancellation and owner teardown.
None may overwrite another live slot or reinterpret unreadable state as empty.

## Evidence-preserving transaction cases

Test a primary plus backup raw snapshot before last-good substitution. A corrupt
primary with valid backup is still a distinct preimage. Seal exclusive preimage
members before primary replacement; injected seal failure leaves all originals
unchanged. NoChange/Conflict/Cancelled must not update .bak or .damaged.
NoChange compares the intended logical patch after preserving unknown fields;
format-only current edits still conflict with a stale preview revision.

Fault points: after destination reservation, each member write, manifest seal,
current-revision check, preimage seal, immediately before primary commit and
after successful commit. Before commit, retain old primary; after commit report
Committed even if cancellation arrives. Preserve any inert sealed preimage in
the result; do not delete recoverable evidence as cleanup. Verify both source
and unrelated slot hashes. Process-kill tests qualify process interruption,
not physical power failure or every filesystem's durability guarantee.

Checkpoint cases: successful navigation/position+checkpoint are one settings
transaction; failed decode cannot advance it; explicit open beats delayed lazy
restore; late slot claim preserves explicit intent; failed final persistence
cannot mark clean. Recovery namespace/version, record bound and dirty/clean
encoding still need exact shared declarations before implementation. A safe-mode
launch must leave original settings, backup, damaged evidence, sessions and
recovery records byte-identical; its private temporary namespace owns all writes.
