# PLATFORM-01 native launcher fixture verification

Source ac067ae (reviewed fixture code plus lead-only Docker wiring and evidence).
Independent fixture review APPROVED8888d41, candidate b4fa53e; Docker source-copy
fix5b03339 ACCEPTED7bb7b80. See the unchanged report
PLATFORM-01-NATIVE-FIXTURE-IMPLEMENTATION-CLAUDE-REVIEW.md for exact scope and limits.

## Problem and resulting behavior

Windows platform unit tests previously launched real persistent Explorer/terminal
windows and checked only launch success. They now copy a console-less GUI helper
and runner into a unique generated TEMP directory, capture exact UTF16 argv/CWD,
module/hash/token and semantic Shell PIDLs, test native success/fallback and COM
balance, and bound owned children with a kill-on-close job. Production platform
adapter/header and all app/render/controller/worker code are unchanged. Linux
continues linking the real production library with its existing launcher fixtures.
Root Docker now copies tracked cmake fragments before configure. No helper is
installed into the product package.

## Required candidate checks

- Windows: scripts/build.ps1 -BuildDir build-codex-platform-native-fixture -Test
  on ac067ae passed18/18,185.93s. folder_grid95.40s; directory_model24.41s;
  platform_integration0.85s. Log .cache/platform-native-fixture-retry-windows.log.
  The source SHA is captured in .cache/platform-native-fixture-retry-source.txt.
  Strict250ms responsiveness assertions and existing timeouts are unchanged.
  The native slot was clear at acquisition; no concurrent native suite was seen
  during this passing run. Another lane's compilation began during the run,
  so this is not an isolated host-performance measurement.
- Docker: docker build -t glancelot-browser:main . through WSL Ubuntu passed
 18/18,110.41s on code5b03339, including directory_model3.24s and
  platform_integration0.46s. Log .cache/platform-native-fixture-docker-retry.log.
  The subsequent required command on ac067ae passed using identical cached
  layers/image; only uncopied documentation changed between the two commands.
  Log .cache/platform-native-fixture-final-docker.log. Both produce image
  0ba808df636ddcd1a66115898c9d46cce5f486b5190c956eee6e42f8ce542692.
- Independent Windows runtime: reviewer built only the fixture/helper in its own
  tree and passed1/1 CTest0.79s, nine slots574ms; no owned helper remained.
  This is independent fixture evidence, not independent full-suite execution.

Earlier attempts are retained honestly: clean build201/201 passed, but its native
suite was stopped during window_slots after another lane's full suite overlapped.
Only the owned CTest34848 tree was stopped. The first Docker configure failed
because cmake/ was not copied; the one-line reviewed fix produced the passing run.
No failed/cancelled attempt qualifies as a gate pass.

## Limits and remaining checks

Required integrated-commit Windows and Docker checks remain pending until the lead
records them below. Captured launch requests do not qualify visible Explorer or
terminal presentation. The test requires an ASCII8.3 alias of its exact Unicode
sandbox and fails closed without it. When the helper process object has already
vanished, completion is inferred from its sealed capture and empty owned job;
not every helper exit code is read. Parent abort may leave inert TEMP copies.

The changed host load and removal of future Shell side effects do not prove the
cause of earlier stock-Qt responsiveness stalls. Physical DPI/ICC/HDR/high-refresh,
native Linux display and network/filesystem qualification stay unverified unless
separately measured. FS-WATCH and INPUT UI remain independently unapproved here.
## Integrated fixture qualification, October19

Corrective integrated commit1d51c17 passed required
scripts/build.ps1 -BuildDir build-codex-platform-native-fixture -Test:
18/18,176.11s, with unchanged strict250ms directory responsiveness assertions.
folder_grid95.20s, directory_model21.60s and platform_integration1.14s passed.
The clean integration worktree was fast-forwarded to exact main1d51c17, so the
root checkout's two unrelated blank-line edits were excluded. Native slot was
verified quiet30s before acquisition; no competing native suite/build was
observed during this integrated run. Logs/source record:
.cache/platform-native-fixture-integrated-{windows.log,source.txt}.

Required integrated docker build -t glancelot-browser:main . passed using
identical cached image0ba808df636ddcd1a66115898c9d46cce5f486b5190c956eee6e42f8ce542692.
No new test execution is claimed for this cached command. The preceding actual
Linux test run passed18/18,110.41s on code-identical inputs (documentation-only
merges followed); log .cache/platform-native-fixture-integrated-docker.log records
integrated command success. Earlier full Linux and native results remain above.

The fixture prerequisite is independently reviewed and checked on its integrated
commit. The integration lock is released after recording these results. PLATFORM
software close-out remains pending a final independent evidence audit; historical
model stall causes are not established by this pass. Physical gates remain open.
