# Folder-jump audit

Date: October 18, 2026. Base: `85a468e`. Branch: `fix/folder-jump`.
Build: isolated branch-local Release directory.

## Navigation behavior and scope

Glancelot retains PgUp/PgDown for adjacent-folder jumps by explicit user choice.
Its existing Stop, Loop Folder and Continue Tree modes provide manual/automatic
crossing control; explicit folder jumps operate independently of those modes.

The repository's NAV-01/02/03/04/06/08 contracts remain authoritative: natural
preorder folder traversal, reverse traversal through the deepest previous subtree,
forward natural-first/backward natural-last landing, bypass of image-less folders,
file-sort-independent folder order and explicit landing, bounded traversal root,
no root wrap and no automatic link following. Automatic image stepping instead
uses the destination's visible file-sort endpoint so it does not skip images.

This change verifies Glancelot's navigation contract. Exact behavioral parity
with external viewers has not been measured. JPEG/PNG eligibility remains the
current qualified scope. No new filtering, history, favorites or format support
is claimed.

## Findings and changes

- `Navigator::folder` incorrectly required a current image, so an exact visit to
  an empty folder/root stranded folder jumping. Use the visited directory as the
  ordering anchor. Unopened navigators and cancelled operations still do nothing.
- `Navigator::setRoot` also required a current image, so widening an empty folder's
  root silently failed. A valid visited directory now suffices; expansion is still
  restricted to safe ancestors.
- `navigateDirectory` discarded an explicitly widened root. Keep that root for
  in-scope visits, including empty folders; outside visits start at the requested
  folder. Never silently widen to a drive/share or scan sibling roots.
- A target can disappear between candidate search and final listing. Reject a
  missing landing before publishing directory/current/files together. This guard
  is source-reviewed; no deterministic filesystem-race injection was introduced.

All scans remain on the existing navigation worker, using its cancellation,
generation validation and bounded intent/replacement queues. Viewing writes no
source media. Grid paging and text/modal shortcut scope are unchanged.

## Verification

Generated disposable files only. Commands below express the isolated build
directory as `$buildDir`; set it to your branch-local build directory. Baseline
command:

```powershell
./scripts/build.ps1 -BuildDir $buildDir -CoreOnly -Test
```

Actual baseline output before production fixes:

```text
1/1 Test #1: navigation ... Failed
navigation failure: cannot jump from an image-less root to its first child
0% tests passed, 1 tests failed out of 1
```

Full verification command (shared project-local Qt/tool binaries; isolated build):

```powershell
$env:PATH="C:\src\Github\Glancelot\.tools\python\Scripts;$env:PATH"
./scripts/build.ps1 -BuildDir $buildDir -QtRoot C:\src\Github\Glancelot\.tools\Qt\6.8.3\msvc2022_64 -Test
```

New tests cover empty-root/empty-sibling anchors, ancestor expansion without an
image, unopened/zero-direction no-ops, preserving a root through empty/nonempty
exact visits, resetting outside scope, and actual Qt canvas PgUp/PgDown dispatch.
The explicit-jump matrix covers four file sorts, both sort directions, all three
boundary modes, both jump directions and root stops. Existing generated preorder
and reverse oracles, deleted anchors, link/cycle, cancellation, and stale/failing
path visits remain part of the full suite. Unix permission-denial enforcement is
not established by Windows execution.

Final expanded Windows/MSVC 2022/Qt 6.8.3 result:

```text
1/9 navigation          Passed  2.24 sec
2/9 viewer_acceptance   Passed  6.18 sec
3/9 thumbnails         Passed  6.16 sec
4/9 folder_tree        Passed  7.21 sec
5/9 pane_layout        Passed  0.24 sec
6/9 window_slots       Passed 21.40 sec
7/9 folder_grid        Passed 28.53 sec
8/9 viewport           Passed  0.05 sec
9/9 themes             Passed  0.13 sec
100% tests passed out of 9
Total Test time (real) = 72.30 sec
```

The initial full run was 8/9 because the new test compared native Windows
backslashes with QTemporaryDir forward slashes. Corrected test comparisons use
QDir::cleanPath; the expanded run above passed. Branch-local detailed logs are
under `.cache/folder-jump/` (ignored generated evidence).

Independent reviewer `folder_jump_review` approved the implementation and expanded
tests with no blocking defects, contingent on the passing suite now recorded.
`git diff --check` passed. Qt UI tests ran offscreen with the software backend;
key events exercise the actual QML shortcut dispatch, not physical GPU rendering.
The user requested a separate branch, so main integration remains pending and the
queue task remains in review rather than claiming integrated delivery.
Physical Linux, disconnected shares and external viewer comparisons are unverified.
