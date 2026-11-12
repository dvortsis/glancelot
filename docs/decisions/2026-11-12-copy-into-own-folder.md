# Copy into the source's own folder, and the B1 review's lead items

Written for: B1, B2 and anyone changing copy, move or paste.

**Decision (lead, November 12, 2026, under the user's rule that user experience and performance come first):** answers to the lead items in `docs/verification/B1-REVIEW.md` (on `claude/review-b1`, commit `e1a4859`):
- **L-1:** B1's edits to lead-owned files are ratified: the one `include` line in the root `CMakeLists.txt`, `cmake/AssetGroupTests.cmake`, and the `ui/Main.qml` call sites (`modalOpen` and one `TransferFlow` instance). B1's round 2 moves the clipboard commands into their owning component under the EN-07 rule when it merges `main`. This item is recorded here, not in the contract.
- **L-2:** the header diffs are ratified: the contract text plus the two `#include` lines. Now in `docs/contracts/ASSET-GROUP.md`.
- **L-3:** Ctrl+C then Ctrl+V in a file's own folder duplicates it with no dialog, as keep both. Requested files of one group share the same `(n)`. Any other reason to confirm still asks. Now in the contract, under "Lead decisions, November 12, 2026".
- **L-4:** the summary keys, the row `name` field and the B2 Overwrite note are now in the contract.

The review's findings I-2 (plan cost and cancel) and I-3 (what counts as one asset) became contract rules in the same block. B1's round 2 implements them.

**Why:**
- Duplicating in place is a one-key habit in Windows Explorer and most desktop file managers. Before B1, it made `name (2)` with no prompt. B1 made it ask, and the question tells the user nothing they don't know: the only "collision" is the file itself, and keep both is the only useful answer.
- A plan on a slow share must stay cancellable and must not hold up the next paste.
- Unrelated files that happen to share a stem must not be renamed or skipped together. Files of one group, though, are skipped or numbered together even in file scope. Otherwise a skip would land a sidecar beside a different photo of the same name, or keep both would rename a sidecar so it no longer names its file (November 5 decision).

**Also noted:** B1-REVIEW P-10 points out that `cmake/AssetGroupTests.cmake` carries production build lines as well as test targets. The ratification covers the file as it is. Moving those lines into `cmake/ViewerSources.cmake` is a lead follow-up at integration, not a B1 change. The own-folder check compares paths with the same per-OS case folding as the move's same-folder check. Where that differs from a volume's case rule, the only effect is an extra prompt, never a missed collision.

**Cost:** a user who wanted to be asked before every duplicate isn't asked. Keep both never overwrites, and execution re-checks each target. The run copies only the requested files, which were read and never changed, so the copy can be deleted like any other file.
