# Platform launcher test environment prerequisite

Status: candidate, not independently approved or integrated.
Branch codex/platform-test-env; worktree .worktrees/codex-platform-test-env.
Lead authorization: docs/PLATFORM-01-PLAN.md, frozen main444f896.

The existing Linux tests called real desktop launchers and asserted process-start
success in a Qt build image with no xdg-open/terminal installed. This made the
PLATFORM-OPS-01 integration break the required Docker test gate. No production
adapter, public header, root configuration or QML is changed by this repair.

Linux tests now create disposable executable links in a private temporary PATH.
Each starts an actual detached Qt process that atomically records its launcher name,
argument vector and working directory to JSON. Tests verify exact target directories,
file-parent mapping, literal Unicode/space/metacharacter paths, root terminal cwd,
TERMINAL override and missing-override fallback. Missing launchers must return false;
invalid inputs must not create a request. The private PATH and two test variables
are restored. No shell command string, desktop window or original media is used.
Windows keeps its existing native launcher assertions; the helper is Linux-only.

Initial Linux target build/check in pinned Qt6.8.3 image (before the final additional
literal-path case):

```
cmake -S /src -B /tmp/platform-test-build -G Ninja -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=ON
cmake --build /tmp/platform-test-build --target platform_integration_tests --parallel4
ctest --test-dir /tmp/platform-test-build -R '^platform_integration$' --output-on-failure
100% tests passed,0 tests failed out of1
Total Test time(real)=0.37sec
```

The source checkout was mounted read-only and the container had --network none.
Final literal-path case and full candidate Windows/Docker checks remain required.
Include this prerequisite in PERF candidate review so an actual Claude reviewer
can audit it before any main integration. Real native Linux file-manager/terminal
UX remains unverified; captured process-start routing is the scope of these tests.

The final test source (including literal Unicode/metacharacter paths) is included
in combined PERF candidate affabf4. Its required full Docker build passes 15/15
in 128.24s; platform_integration passes in 0.47s. This verifies real helper-process
argv/cwd assertions in the headless build image. Independent review and main
integration remain pending.
