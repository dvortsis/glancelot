from pathlib import Path
import subprocess,json,hashlib,difflib
path='tests/fs_watch_budget_tests.cpp';baseline='029ad47~';candidate='029ad47'
original=subprocess.check_output(['git','show',baseline+':'+path],text=True)
changed=subprocess.check_output(['git','show',candidate+':'+path],text=True)
lines=[]
for line in changed.splitlines(keepends=True):
 text=line.strip()
 if any(x in text for x in ['FS-WATCH-HARNESS-01:','QElapsedTimer lifetime;','const auto phaseLog=','const auto lifetimeLog=','phaseLog(','phase="','FS_WATCH_PROGRESS']):continue
 lines.append(line)
restored=''.join(lines).replace('for(int i=1;i<100000;++i) {\n','for(int i=1;i<100000;++i)\n')
restored=restored.replace('"generated"));\n        }','"generated"));').replace('QChar(\'0\')))));\n        }','QChar(\'0\')))));')
result={'path':path,'baseline_commit':subprocess.check_output(['git','rev-parse',baseline],text=True).strip(),'candidate_commit':subprocess.check_output(['git','rev-parse',candidate],text=True).strip(),'baseline_blob':subprocess.check_output(['git','rev-parse',baseline+':'+path],text=True).strip(),'candidate_blob':subprocess.check_output(['git','rev-parse',candidate+':'+path],text=True).strip(),'comparison':'LF-normalized source after removing only logging/guard additions and two added loop brace pairs','baseline_sha256':hashlib.sha256(original.encode()).hexdigest(),'stripped_candidate_sha256':hashlib.sha256(restored.encode()).hexdigest(),'equal_after_only_logging_and_braces_removed':restored==original}
Path('.cache/watcher-harness/source-equivalence.json').write_text(json.dumps(result,indent=2))
if restored!=original: print(''.join(difflib.unified_diff(original.splitlines(True),restored.splitlines(True))));raise SystemExit(1)
print('Verified code equivalence',result['baseline_sha256'])
