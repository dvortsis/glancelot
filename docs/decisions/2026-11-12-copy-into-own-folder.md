# Copy into the source's own folder, and the B1 review's lead items

Written for: B1, B2 and anyone changing copy, move or paste.

**Decision (lead, November 12, 2026, under the user's rule that user experience and performance come first):** answers to the lead items in `docs/verification/B1-REVIEW.md` (on `claude/review-b1`, commit `e1a4859`), now in `docs/contracts/ASSET-GROUP.md`:
- **L-1:** B1's edits to lead-owned files are ratified: the one `include` line in the root `CMakeLists.txt`, `cmake/AssetGroupTests.cmake`, and the `ui/Main.qml` call sites (`modalOpen` and one `TransferFlow` instance). After the main merge, the clipboard commands are handled in their owning component, under the EN-07 rule.
- **L-2:** the header diffs are ratified: the contract text plus the two `#include` lines.
- **L-3:** Ctrl+C then Ctrl+V in a file's own folder duplicates it with no dialog, as keep both, with the same `(n)` on every member of an asset. Any other collision still asks.
- **L-4:** the summary keys and the B2 Overwrite note are now in the contract.

**Why:** duplicating in place is a one-key habit in every desktop file manager, and before B1 it made `name (2)` with no prompt. Once B1 made it ask, the habit gained a dialog that asks nothing the user doesn't already know: the only "collision" is the file itself, and keep both is the only useful answer. The other rules come from the review's findings I-2 and I-3. A plan on a slow share must stay cancellable and must not block the next paste. Unrelated files that happen to share a stem must not be renamed or skipped together.

**Cost:** a user who wanted to be asked before every duplicate isn't asked. Nothing is overwritten, nothing unlisted is touched, and the copy can be deleted like any other file.
