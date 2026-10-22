from pathlib import Path
import re,json,shutil,subprocess,yaml,hashlib
root=Path.cwd()
status=json.loads((root/'.cache/csv-integration/status.json').read_text(encoding='utf-8-sig'))
assert status['exit']==0,status
pin=status['source']
subprocess.run(['git','diff','--exit-code',pin,'HEAD','--','CMakeLists.txt','cmake','app','assets','include','src','ui','tests','scripts','Dockerfile','docker'],check=True)
e=root/'docs/verification/export-csv-01/integration-2026-10-22'
e.mkdir(parents=True,exist_ok=True)
for folder in ['csv-integration','csv-integration-attempt-1','csv-integration-attempt-2','grid-resize-probe','cache-probe']:
 shutil.copytree(root/'.cache'/folder,e/folder,dirs_exist_ok=True)
shutil.copy2(root/'.cache/run-csv-integration.ps1',e/'run-csv-integration.ps1')
qt=e/'windows-qt-results';qt.mkdir(exist_ok=True)
for file in (root/'build-codex-integrate-export-csv-01').glob('*results*.txt'):shutil.copy2(file,qt/file.name)
(root/'docs/handoff/LEAD-WRAPUP-2026-10-22.md').write_bytes((root/'.cache/LEAD-WRAPUP-2026-10-22.md').read_bytes())
stack=subprocess.check_output(['git','show','b81cd73:docs/handoff/CLAUDE-STACK-SYNC-2026-10-22.md'])
(root/'docs/handoff/CLAUDE-STACK-SYNC-2026-10-22.md').write_bytes(stack)
s=(root/'.cache/queue-repaired.yaml').read_text(encoding='utf-8')
def update(task,fields):
 global s
 start=s.index('  - id: '+task+'\n')
 found=re.search(r'(?m)^(?:  - id: |[A-Za-z_])',s[start+len('  - id: '+task+'\n'):])
 end=start+len('  - id: '+task+'\n')+found.start() if found else len(s)
 block=s[start:end]
 for key,value in fields.items():
  line='    '+key+': '+json.dumps(value,ensure_ascii=False)+'\n'
  pattern=r'(?m)^    '+re.escape(key)+r': .*\n'
  if re.search(pattern,block):block=re.sub(pattern,lambda m:line,block,count=1)
  else:block=block.rstrip()+'\n'+line
 s=s[:start]+block+s[end:]
update('EXPORT-CSV-01',dict(state='integration',review_verdict='APPROVED at25146f6450fcac5b7f6a8c6b71f47fe470665698; independent Claude Sonnet5.5 review0a38bd9c18c1fb72e7b564038f612ae7adbc9014',review_evidence='docs/verification/EXPORT-CSV-01-CLAUDE-REVIEW-R2.md',blocked_by=None,candidate_commit='25146f6450fcac5b7f6a8c6b71f47fe470665698',integration_branch='codex/integrate-export-csv-01',integration_worktree='.worktrees/codex-integrate-export-csv-01',integration_build='build-codex-integrate-export-csv-01',gated_commit=pin,contract='include/glancelot/selection_export.hpp; cmake/ExportTests.cmake required unconditionally',integration_evidence='docs/handoff/EXPORT-CSV-01-INTEGRATION-2026-10-22.md',follow_up='Approved nonblocking O1 invalid native Unicode item path can throw with RAII cleanup; future Qt service must contain worker exceptions. O2 link-test skip visibility and O3 filesystems without hard links remain recorded in independent review. Engine only; no export dialog claimed.'))
update('GRID-RESIZE-TEST-01',dict(state='integration',candidate_commit='498d92279a356d064595c2781f84bad89c3624c3',review_evidence='docs/verification/grid-resize-test-01/claude-review/REVIEW.md',evidence='docs/verification/grid-resize-test-01/README.md; docs/handoff/EXPORT-CSV-01-INTEGRATION-2026-10-22.md'))
update('CACHE-BASELINE-TEST-01',dict(state='integration',candidate_commit='d63cd9d5f36f0134a4b42bcddeaa4b62ddad198f',review_evidence='docs/verification/cache-baseline-test-01/review/REVIEW.md',evidence='docs/verification/cache-baseline-test-01/README.md; docs/handoff/EXPORT-CSV-01-INTEGRATION-2026-10-22.md'))
for task,branch,sha in [('PATH-COMPLETE-01','claude/f002','e05614f'),('VIEW-POLISH-01','claude/f037','2891250')]:
 update(task,dict(state='review',branch=branch,candidate_commit=sha,blocked_by='Current-main stack synchronization, frozen corrections and per-packet Gemini confirmation; docs/handoff/LEAD-WRAPUP-2026-10-22.md',evidence='docs/handoff/CLAUDE-STACK-SYNC-2026-10-22.md; original Gemini approvals predate integrated monitor customization'))
update('MONITOR-VERIFY-01',dict(reviewer_lane='claude',qualification_status='Dependency-unblocked; physical mixed-DPI, refresh, per-monitor ICC/HDR and native Linux Wayland/X11 matrix remains unverified; see docs/handoff/LEAD-WRAPUP-2026-10-22.md'))
s=s.replace('Planning only; all new tasks remain ready and unverified.','Original planning note; current per-task states and pinned evidence are authoritative.')
s=s.replace('  - Full Linux GUI build (core tests verified in Ubuntu 24.04 WSL/GCC)','  - Native Linux Wayland/X11 physical GUI qualification (full Docker GUI builds and offscreen tests do not qualify physical outputs)')
(root/'docs/WORK-QUEUE.yaml').write_text(s,encoding='utf-8')
windows=(e/'csv-integration/windows-gate.txt').read_text(encoding='utf-8-sig')
docker=(e/'csv-integration/docker-gate.txt').read_text(encoding='utf-8-sig')
ws=windows[windows.rfind('100% tests passed'):].strip()
ds='\n'.join(line for line in docker.splitlines() if '100% tests passed' in line or 'Total Test time (real)' in line or 'selection_export ' in line)
text=f'''# EXPORT-CSV-01 integration, October 22, 2026

Gemini source25146f6450fcac5b7f6a8c6b71f47fe470665698 is independently APPROVED by Claude Sonnet5.5 at review0a38bd9. [Review](../verification/EXPORT-CSV-01-CLAUDE-REVIEW-R2.md) resolves all four required corrections, including discriminatory mutation evidence. Original Gemini worktree and pin are preserved. The lead synchronized current main without conflicts in its own `codex/integrate-export-csv-01` worktree and made `cmake/ExportTests.cmake` mandatory (75175d5). This delivers the engine only; no UI is claimed.

The first full Windows gate at75175d5 failed41/42 in596.98s: the pre-existing grid resize check observed9 delegates against8 during deferred cleanup. Docker was not run after that failure. Unchanged focused repetitions failed2/10 at that same transition. Separate Codex correction498d922 waits up to2000ms for cleanup while preserving limits and geometry assertions. Actual independent Sonnet5.5 approval and ten corrected passes are [retained](../verification/grid-resize-test-01/README.md). Accepted notes and the initial missing-Qt-PATH launcher failure are explicit in that record. No product UI changed for the test correction.

The second full Windows attempt ata83e43e failed41/42 in583.07s, solely the cache cached-revisit assertion; the corrected grid suite passed183.62s. Docker was not run. Explicit Qt logging reproduced3/10 cache failures. Separate Codex fixtured63cd9d uses the existing private baseline-deadline seam for cache-hit assertions and disposable preferences. Production100ms fallback and revision validation remain unchanged, and the full gate still exercises the original late-baseline fresh-pixel/newer-open tests. The corrected fixture has actual independent Sonnet5.5 approval, ten passing CTest repetitions, ten exact five-decode warm traces and passing parallel grid checks; see [complete fixture evidence](../verification/cache-baseline-test-01/README.md). Its initial namespace compile failure and both review checkpoints are retained.

Final gated source: `{pin}`. Whole serial invocation:

```powershell
& C:\\src\\Github\\Glancelot\\scripts\\gate-lock.ps1 -Branch codex/integrate-export-csv-01 -Run {{
    & ./scripts/build.ps1 -BuildDir build-codex-integrate-export-csv-01 -Test
    docker build --progress=plain -t glancelot-browser:main .
}}
```

Actual Windows output:

```text
{ws}
```

Actual Docker output (selection test and complete-suite summary):

```text
{ds}
```

[Complete actual output, failed attempt, source pin, image ID, invocation script and Qt records](../verification/export-csv-01/integration-2026-10-22/). Both gates ran serially under the main checkout's machine gate lock. Docker remains virtual/offscreen evidence. The image is local; no push or deployment.

Exact merge validation and final queue/lock closeout are recorded below after integration. Physical monitor qualification and the Claude stack remain separately open; see [lead handoff](LEAD-WRAPUP-2026-10-22.md).
'''
(root/'docs/handoff/EXPORT-CSV-01-INTEGRATION-2026-10-22.md').write_text(text,encoding='utf-8')
plan=root/'docs/AGENT-LANES-PLAN.md'
with plan.open('a',encoding='utf-8') as stream:
 stream.write('\n\n## Lead closeout, October 22, 2026\n\nMONITOR-CUSTOM-01 is integrated and its Codex Main.qml implementation wave is closed. Claude owns Main.qml for the serial current-main stack correction wave in its existing assigned worktrees. Codex MONITOR-VERIFY uses separate feature tests/harness files. The lead-frozen correction contract, recovery extension and independent confirmation requirements are in [LEAD-WRAPUP-2026-10-22.md](handoff/LEAD-WRAPUP-2026-10-22.md). Root CMake and integration remain lead-only under the integration lock; Gemini reviews only the Claude stack, never its original monitor implementation.\n')
print('Prepared gate evidence, current queue integration states and lead/stack handoffs at '+pin)
