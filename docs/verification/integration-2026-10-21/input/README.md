# INPUT integration evidence, October 21, 2026

Candidate source: `83100c4` on `codex/input-02-r3`, synchronized with main `14a97e3`. Original review pin `87b063d` remains unchanged. Accepted prior corrections `953adf9` and `330d050` are retained in ancestry; main's virtualized result-row test supersedes the temporary delegate wait.

Main-sync resolutions preserve all current-main feature sources, CMake entries and watcher revision checks. File-path clipboard `copy-path` remains distinct from the new `copy-folder-path` command. Rehearsal `5bf1a79` provided the registry scopes, printable pane-key guard and binding reference.

Actual Sonnet integration confirmation first found a test-only `Ctrl+Alt+R` collision with Reveal. Commit `4e91fe6` changed that probe to `Ctrl+Alt+Y`; confirmation accepted it. Its complete Windows gate nevertheless failed 33/34 in 521.09 seconds, solely `input_ui`. Direct output isolated the global `K,S` chord swallowed by grid type-search (17 passed, 1 failed, 1 capture skip). Docker was canceled before qualification.

Broad printable routing `3672036` passed the original focused probes, but Sonnet rejected a default `G`/`S` search regression. The new per-feature regression test reproduced the issue: 2 passed, 1 failed. Narrowing to chord steps at `83100c4` passed that test plus original Escape and native remap cases: 5 passed, 0 failed, 7093ms. Actual Sonnet session `d8cfb3ea-735f-4076-8dd6-ce21818af455` ACCEPT, exit0; all nine UI skills read. Review JSONs and result files are unedited. The first routing review skipped skills; the final confirmation explicitly completed them.

Full gate command, in the candidate worktree:

```powershell
& C:\src\Github\Glancelot\scripts\gate-lock.ps1 -Branch codex/input-02-r3 -Run {
    & ./scripts/build.ps1 -BuildDir build-codex-input-02-r3 -Test
    docker build --progress=plain -t glancelot-browser:main .
}
```

`gate.ps1` is the actual wrapper: it records Windows exceptions and Docker native exit status separately. Full raw output is `windows.txt` and `docker.txt`; status files are authoritative. The Windows final gate passed34/34 in493.56seconds, exit0. Linux final gate passed34/34 in297.09seconds, exit0; image export completed. Interrupted/failing runs retain their names and are not passes.

Physical keyboard layouts, IME and trackpad coverage remain unverified. Capture-only optional test skips do not establish physical UI qualification. Existing cheat-sheet `keys` and grid integer QML warnings remain outside this correction.


Integrated main commit: `d59369014574658893458ea403a08161d986b67e` (`--no-ff`). `git diff --exit-code 83100c4 d593690` returned0, proving the entire integrated tree equals the independently accepted and full-gated candidate. No product change occurred between those checks and integration.

Focused checks at the exact integration commit passed3/3 in92.18seconds, exit0 (`integrated-ctest.txt`):

```powershell
& C:\src\Github\Glancelot\scripts\gate-lock.ps1 -Branch codex/input-02-r3 -Run {
    ctest --test-dir build-codex-input-02-r3 -R '^(input_ui|platform_integration|window_slots)$' --output-on-failure
}
```

The first focused invocation could not find ctest in a fresh shell; no tests ran. The successful invocation prepended project-local `.tools/python/Scripts` and Qt bin to PATH. No assertion or timeout was relaxed.

Full gates are candidate gates; the exact integrated commit has the recorded tree-equivalence proof and focused checks. No second full suite was claimed on the merge commit. Claude's later feature stack must merge this integrated main forward, then obtain the routed confirmations before those packets qualify.
