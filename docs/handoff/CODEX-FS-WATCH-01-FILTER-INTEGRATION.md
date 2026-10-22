# FS-WATCH integration compatibility notes after FILTER-01

Read-only audit of main090ccc8 against the feature's f4c7cbe base. This is an
integration checklist, not permission to modify Gemini's implementation or merge
while its lock is held. Source candidate9bc891f and independent R5 stay fixed.

main now has Folder History, Shortcuts Help and Filter commands plus public
controller/model filter properties. Keep every per-task command and test target;
keep the sole FS-WATCH Main.qml refresh allow-list token alongside those routes.
The test harness must receive the same controller-to-grid filter synchronization
as the application when testing combined behavior. Do not copy older public
headers over these frozen additions.

DirectoryEntryModel::apply now keeps an unfiltered rawListing and projects visible
rows through applyFilter. FS-WATCH identities must be built from the complete
unfiltered observation, retained independently from filtered row visibility, and
never infer physical deletion from a filter hiding a row. Rename selection keys,
anchor and cursor from the complete native identity relation before projection.
Keep the last complete identity baseline through failed/truncated observations;
keep main's raw rows and hidden selections recoverable. Filtering/reset must not
create an extra unbounded history or perform native identity probes on the GUI.
Check list ownership/copy-on-write costs against the same250ms oracle, including
100k rows and repeated monitor-driven refreshes. Do not waive the R5 mixed results.

Regression cases to add after the actual merge (new FS-specific feature file or
existing fs_watch_tests, not grid_ui_tests):

- Persistent filter hides a selected noncurrent asset; rename it externally while
  hidden, refresh, clear filter and recover the renamed selection/cursor only
  from complete native identity evidence, with unchanged source hashes.
- Rename current image through the watcher without directory crossing: temporary
  and persistent filter state remains, current image follows the proven object.
- Explicit directory crossing retains Filter's success/failure policy; missing or
  inaccessible refresh cannot clear a temporary filter or widen the traversal root.
- An atomic replacement under a filtered row updates derived epochs and pixels;
  unrelated hidden-file membership refreshes do not reopen the unchanged image.
- Filtered partial listing retains recoverable raw state and the last complete
  physical baseline; no deletion inference from a projection or truncated snapshot.

Shared CMake must keep Filter/History/Help targets as well as fs_watch and the real
QML F5 test. Main's root CMake is lead-owned under the serial integration lock.
Any conflict is resolved in the assigned Codex worktree, then independently
re-reviewed with full Windows and Docker gates on the resulting source. Current
R5 results cannot qualify these as-yet-unmerged public contracts.
