# Independent post-integration check of main ded69e6 (Claude lane)

Commit: `ded69e6` (includes IO-01 merge `1f97893` and the META-HUD-01 contract freeze). Checked in worktree
`claude-verify-main-io`, build directory `build-claude-verify-main-io`. No product, test or queue file was changed.

- Windows: `scripts/build.ps1 -Test` passed 25 of 25, total test time 185.71 s.
- Docker: `docker build -t glancelot-browser:verify-main .` exited 0. Only the build result was read; no Linux test count was inspected.
- Not run: native GPU, hardware or second-monitor checks (unverified).
