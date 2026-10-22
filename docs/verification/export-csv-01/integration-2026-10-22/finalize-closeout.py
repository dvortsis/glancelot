from pathlib import Path
import subprocess,json,shutil,re,hashlib,yaml
root=Path.cwd(); integrated='5db97272926105e7b6185eca671b5f6a837b9dd4';gated='845509f1930bd62cb24194a3182d94267c67dafc'
assert subprocess.check_output(['git','rev-parse','HEAD'],text=True).strip()==integrated
proof=json.loads((root/'.cache/csv-integrated-proof/status.json').read_text(encoding='utf-8-sig'))
assert proof['exit']==0 and proof['source']==integrated
paths=['CMakeLists.txt','cmake','app','assets','include','src','ui','tests','scripts','Dockerfile','docker']
checks=[]
for before,after,scope in [(gated,integrated,paths),('a17b882',integrated,[]),('25146f6',integrated,['src/core/selection_export.cpp','tests/selection_export_tests.cpp']),('498d922',integrated,['tests/grid_ui_tests.cpp']),('d63cd9d',integrated,['tests/cache_tests.cpp','CMakeLists.txt'])]:
 result=subprocess.run(['git','diff','--exit-code',before,after,'--',*scope],capture_output=True,text=True)
 assert result.returncode==0,result.stdout
 checks.append(dict(before=before,after=after,paths=scope or ['entire tree'],exit=result.returncode,output=result.stdout))
e=root/'docs/verification/export-csv-01/integration-2026-10-22'
shutil.copytree(root/'.cache/csv-integrated-proof',e/'integrated-proof',dirs_exist_ok=True)
(e/'integrated-source-equality.json').write_text(json.dumps(checks,indent=2)+'\n',encoding='utf-8')
binaries={p:hashlib.sha256((root/p).read_bytes()).hexdigest() for p in ['build-codex-integrate-export-csv-01/selection_export_tests.exe','build-codex-integrate-export-csv-01/cache_tests.exe','build-codex-integrate-export-csv-01/grid_ui_tests.exe','build-codex-integrate-export-csv-01-production/glancelot_core.lib']}
(e/'integrated-binary-sha256.json').write_text(json.dumps(binaries,indent=2)+'\n',encoding='utf-8')
s=(root/'docs/WORK-QUEUE.yaml').read_text(encoding='utf-8')
data=yaml.safe_load(s);assert data['integration_lock']['holder']=='codex-lead-wrapup-2026-10-22'
for task in ['EXPORT-CSV-01','GRID-RESIZE-TEST-01','CACHE-BASELINE-TEST-01']:
 start=s.index('  - id: '+task+'\n');match=re.search(r'(?m)^(?:  - id: |[A-Za-z_])',s[start+len('  - id: '+task+'\n'):]);end=start+len('  - id: '+task+'\n')+match.start() if match else len(s)
 block=s[start:end];assert '    state: "integration"' in block
 block=block.replace('    state: "integration"','    state: done')
 block=block.rstrip()+'\n    integrated_commit: '+integrated+'\n    integrated_checks: Windows42/42 620.01s and fresh Docker42/42 375.21s under one main gate-lock block; exact merge tree identical; integrated CSV/cache2/2 6.24s plus grid3/3 3.182s and production core BUILD_TESTING=OFF six steps pass. Actual output in docs/handoff/EXPORT-CSV-01-INTEGRATION-2026-10-22.md\n'
 s=s[:start]+block+s[end:]
s=re.sub(r'(?m)^integration_lock: .*$', 'integration_lock: null',s,count=1)
(root/'docs/WORK-QUEUE.yaml').write_text(s,encoding='utf-8')
h=root/'docs/handoff/EXPORT-CSV-01-INTEGRATION-2026-10-22.md'
with h.open('a',encoding='utf-8') as stream:
 stream.write(f'''\n## Exact integrated commit and closeout\n\nNo-ff merge `{integrated}` has an entire tree identical to qualified candidate a17b882. Every product/build input is identical to gated845509f; CSV source/tests exactly match25146f6, grid correction498d922, and cache source/CMake d63cd9d. Actual Git comparisons (all exit0) and binary SHA256 records are in the evidence archive.\n\nExecuted under the main checkout's gate-lock wrapper from the integration worktree checked out at this exact merge:\n\n```powershell\n& C:\\src\\Github\\Glancelot\\scripts\\gate-lock.ps1 -Branch codex/integrate-export-csv-01 -Run {{\n    ctest --test-dir build-codex-integrate-export-csv-01 -R '^(selection_export|cache)$' --output-on-failure -V\n    grid_ui_tests.exe reviewCompactCellsSnapToWholeRows -o <proof>/grid-resize.txt,txt\n    cmake -S . -B build-codex-integrate-export-csv-01-production -G Ninja -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=OFF -DGLANCELOT_BUILD_VIEWER=OFF\n    cmake --build build-codex-integrate-export-csv-01-production --target glancelot_core --parallel 4\n}}\n```\n\nThe archived invocation script supplies MSVC and project-local Qt PATH; no system environment or original media changed. Actual output:\n\n```text\n100% tests passed out of 2\nTotal Test time (real) =   6.24 sec\nTotals: 3 passed, 0 failed, 0 skipped, 0 blacklisted, 3182ms\n[6/6] Linking CXX static library glancelot_core.lib\n```\n\nThe production core build explicitly compiles selection_export.cpp with tests disabled. [Complete integrated output](../verification/export-csv-01/integration-2026-10-22/integrated-proof/). Windows full-gate raw Qt logs additionally show both production100ms late-baseline rows passing,82 watcher checks with0fail/0skip, and10 cache checks with0fail/0skip.\n\nEXPORT-CSV-01, GRID-RESIZE-TEST-01 and CACHE-BASELINE-TEST-01 are done. The repaired queue validates56 unique tasks with strict duplicate-key rejection; the formerly detached security/export tasks and misplaced fixture acceptance fields are corrected. The integration lock is cleared in the final documentation closeout. Remaining Claude corrections/confirmation and monitor qualification are explicit in the lead handoff, with the next Main.qml ownership wave frozen.\n''')
p=root/'docs/handoff/CODEX-LANE-STATUS-2026-10-21.md'
old=p.read_text(encoding='utf-8')
p.write_text(f'''# Lead wrapup, October 22, 2026\n\nEXPORT-CSV-01 is integrated at `{integrated}` after actual independent Claude approval of Gemini25146f6. Two independently reviewed Codex test-fixture corrections (grid deferred cleanup and cache baseline/config isolation) are included. Full locked Windows42/42 620.01s and fresh Linux Docker42/42 375.21s pass; exact-merge CSV/cache2/2, grid3/3 and production core with tests disabled pass. All earlier failures, raw output and reviews are retained in [the final integration handoff](EXPORT-CSV-01-INTEGRATION-2026-10-22.md). Queue structure/status repaired; integration lock cleared at closeout.\n\nMONITOR-CUSTOM-01 remains done. [Remaining work and frozen Claude contract](LEAD-WRAPUP-2026-10-22.md): Claude retains its stack implementation and owns Main.qml for the serial synchronization/correction wave; Gemini confirmation is still required afterward. MONITOR-VERIFY is dependency-unblocked for Codex harness/evidence work, with independent Claude review and physical/native-platform qualification still required. CANVAS/DECODE physical limits remain explicit. No unrelated backlog is promoted. Older pending/status statements below are historical.\n\n---\n\n'''+old,encoding='utf-8')
print('Integrated proofs accepted; three tasks done; queue lock released; final status/handoffs written')
