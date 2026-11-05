# Asset confirmations: when to ask, and whole-asset collisions

Written for: B1, B2 and anyone changing copy, move or paste.

**Decision (lead, November 5, 2026, under the user's rule that user experience and performance come first):** answers to B1's three open questions, now in `docs/contracts/ASSET-GROUP.md` under "Lead decisions":
1. A paste or move with nothing to decide runs straight through, as it did before B1, and the confirmation appears only when there is something to show or choose.
2. Under asset scope, a collision moves or skips the whole asset, and keep both names every member `stem (n)`.
3. A move into the source's own folder is a non-modal notice, not an error or a dialog.

**Why:** asking on every Ctrl+V adds a dialog to the most common file action for no safety gain, because file-only stays the default and nothing is included silently. A sidecar next to the wrong photo describes the wrong picture, which is worse than a renamed set. A cut pasted back into its own folder changed nothing and shouldn't read as a failure.

**Cost:** a user who expected to see the confirmation every time won't. The plan still runs and is still revalidated, so the executed list is the planned one.
