# Lead integration of the Claude stack and evidence branches, October 22, 2026

Written for: the lead or integrator and reviewers of STACK-CORRECTION-01.

On October 22 the user directed "merge all branches", choosing the option to merge the stack plus the evidence branches. Branch `lead/integrate-all-2026-10-22` (worktree `.worktrees/lead-integrate-all`, build `build-lead-integrate-all`) was cut from `main` `d632a53`.

## What was merged

| Commit | Content |
| --- | --- |
| `3b2b0db` | `claude/f051`, which contains the F002 to F051 stack. Five conflicts were resolved; the commit message lists each resolution. |
| `813fad6` | Lead takeover documents: AGENTS.md override, lane amendment, queue, takeover plan. |
| `bfe7b27` | `pathbar_ui_tests` attached to MonitorCustomization in `cmake/MonitorCustomization.cmake`. The first compile failed with LNK2019 on `MonitorCustomization` symbols, because F002's shared harness now carries main's monitor context. |
| 42 `merge(evidence)` commits | Every `claude/review-*`, `claude/verify-*`, `codex/review-*` and `agent/review-f0xx` branch, plus `claude/stack-sync-2026-10-22` and `claude/audit-ux`. Documents only: code paths, `docs/WORK-QUEUE.yaml` and `docs/AGENT-LANES-PLAN.md` were kept at the integration tip, and `main` won conflicting document hunks. After the merges, `git diff --name-only bfe7b27 HEAD -- . ':(exclude)docs'` was empty. Spot checks of five branches found every one of their documents present. |
| `ec05e42` | Record-only merge of `claude/f002` and `claude/f037` (their own sync onto `d632a53`). Their `GLANCELOT_TEST_WITHOUT_MONITOR` opt-out was dropped in favor of `bfe7b27`. The staged diff was empty apart from that define. |

Deliberately not merged:

- `claude/input-02-impl`, `claude/input-02-mainsync` and `claude/input-02-snapshot`: superseded, because INPUT-02 is integrated.
- `codex/platform-native-fixture`: the retired fixture that Defender flagged.

## Gates on `ec05e42`

Both gates ran in one `gate-lock.ps1 -Run` block from the integration worktree:

```powershell
& C:\src\Github\Glancelot\scripts\gate-lock.ps1 -Branch lead/integrate-all-2026-10-22 -Run {
    & ./scripts/build.ps1 -BuildDir build-lead-integrate-all -Test
    docker build --progress=plain -t glancelot-browser:main .
}
```

| Gate | Result |
| --- | --- |
| Windows `scripts/build.ps1 -Test` | `100% tests passed out of 43`; `Total Test time (real) = 722.35 sec`; 760 s wall clock including the incremental build. Raw output: `windows-gate.log` and `windows-LastTest.log`. |
| Docker `docker build` (Ubuntu 24.04, Qt 6.8.3 gcc_64, offscreen) | `100% tests passed, 0 tests failed out of 43`; `Total Test time (real) = 411.23 sec`; 1096 s wall clock. Raw output: `docker-gate.log`. |

The 43rd test is the stack's `pathbar_ui`. The slowest Windows tests:

| Test | Seconds |
| --- | --- |
| `folder_grid` | 228.39 |
| `fs_watch_budget` | 88.69 |
| `fs_watch` | 87.47 |
| `input_ui` | 79.03 |
| `recovery_process` | 35.87 |

The commit after `ec05e42` (`ebf259a` and its follow-up) adds only `.github/workflows/windows-gate.yml` and documents. No source, CMake or test file changed after the gated commit.

A history scan of `origin/main..HEAD` (`git log -G`) for GitHub, OpenAI-style, AWS and Slack tokens and private-key headers found no matches.

## Limits

- These gates prove that the merged tree builds and passes the existing suites. They do not approve the stack's frozen corrections. STACK-CORRECTION-01 remains `ready`: F015 must move to the recovery checkpoint, F040 needs pinned-output parity, F049 needs lock/freeze and unknown-data checks, and F054 must cover main's newer strings.
- Physical DPI, refresh and color, Linux Wayland/X11 hardware, NVDA/Orca, and real keyboard layouts and IME remain unverified.
- The fresh-context confirmation review of the merge resolutions in `3b2b0db` is still owed.
