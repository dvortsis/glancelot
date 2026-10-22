# GRID-RESIZE-TEST-01 correction

Source `498d92279a356d064595c2781f84bad89c3624c3`, Codex lane, assigned worktree/branch/build. This changes only existing test synchronization: wait up to 2000ms for asynchronously retired GridView delegates before sampling the settled count. The limit remains eight times the columns; all original geometry assertions and binding-loop checks remain. This bounds the settled count and does not qualify peak GPU/CPU allocation.

The unchanged baseline at75175d5 failed the same 9-versus-8 assertion twice in ten focused repetitions. The preceding full Windows gate failed41/42 in596.98s, solely that assertion. Production QML and CSV sources are unchanged. Those complete gate logs are retained in the CSV integration archive.

Actual independent Claude Sonnet5.5 review of498d922: APPROVED, session f6a96801-bf25-443b-b85d-3b15c4f28166, no permission denials. See `claude-review/REVIEW.md`, prompt, result and metadata. Accepted nonblocking notes: pre-wait synchronous geometry reads, settled rather than peak count, redundant immediate assertion. No inference of physical hardware qualification.

Actual command (test-local Qt bin PATH, QT_QPA_PLATFORM=offscreen, QT_QUICK_BACKEND=software):

    grid_ui_tests.exe reviewCompactCellsSnapToWholeRows -o <corrected-N.txt>,txt

Actual output, ten consecutive repetitions:

```text
run=1 exit=0
run=2 exit=0
run=3 exit=0
run=4 exit=0
run=5 exit=0
run=6 exit=0
run=7 exit=0
run=8 exit=0
run=9 exit=0
run=10 exit=0
```

Every Qt record reports `3 passed, 0 failed, 0 skipped`; typical duration about3.1s. All raw corrected records are adjacent. An initial launcher omitted Qt bin PATH and exited -1073741515 before testing; its status is retained, then process-local PATH was fixed. No system environment or Qt installation was modified. Full source/build scripts and actual configure/build output are retained.

Integration and full Windows/Docker checks remain pending at this source-review checkpoint. Final outcome is recorded by the CSV integration handoff and lead closeout on main.
