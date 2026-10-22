from pathlib import Path
import json,hashlib,subprocess
root=Path.cwd(); names=['docs/verification/export-csv-01/integration-2026-10-22','docs/verification/grid-resize-test-01','docs/verification/cache-baseline-test-01']
excluded={'artifact-manifest.json','staged-byte-audit.json'}
files=[p for name in names for p in (root/name).rglob('*') if p.is_file() and p.name not in excluded]
records=[]
for p in sorted(files):
 b=p.read_bytes();records.append(dict(path=p.relative_to(root).as_posix(),bytes=len(b),sha256=hashlib.sha256(b).hexdigest(),git_blob=hashlib.sha1(b'blob '+str(len(b)).encode()+b'\0'+b).hexdigest()))
e=root/names[0]
(e/'artifact-manifest.json').write_text(json.dumps(records,indent=2)+'\n',encoding='utf-8')
subprocess.run(['git','add',*names],check=True)
entries=subprocess.check_output(['git','ls-files','-s','-z','--',*names]).split(b'\0');index={}
for entry in entries:
 if not entry:continue
 meta,path=entry.split(b'\t',1);index[path.decode()]=meta.split()[1].decode()
for row in records:assert index.get(row['path'])==row['git_blob'],row['path']
result=dict(raw_files=len(records),mismatches=0,source='5db97272926105e7b6185eca671b5f6a837b9dd4',result='All recorded raw file bytes match staged Git blobs')
(e/'staged-byte-audit.json').write_text(json.dumps(result,indent=2)+'\n',encoding='utf-8')
print(json.dumps(result))
