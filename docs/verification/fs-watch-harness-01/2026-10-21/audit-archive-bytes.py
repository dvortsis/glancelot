from pathlib import Path
import hashlib,json,subprocess
r=Path('C:/src/Github/Glancelot')
prefix='docs/verification/fs-watch-harness-01/2026-10-21/'
src=r/'.worktrees/codex-fs-watch-harness-01/.cache/watcher-harness'
rows=[]
for entry in subprocess.check_output(['git','ls-files','--stage','-z',prefix],cwd=r).split(b'\0'):
    if not entry: continue
    meta,path=entry.split(b'\t',1)
    path=path.decode('utf-8'); name=path[len(prefix):]
    if name in ('README.md','.gitattributes','audit-archive-bytes.py','archive-byte-audit.json'): continue
    data=(r/path).read_bytes()
    sha=hashlib.sha1(b'blob '+str(len(data)).encode()+b'\0'+data).hexdigest()
    assert sha==meta.split()[1].decode(),path
    assert data==(src/name).read_bytes(),path
    rows.append({'path':path,'sha256':hashlib.sha256(data).hexdigest(),'bytes':len(data)})
print(json.dumps({'passed':True,'raw_files':len(rows),'files':rows},indent=2))
