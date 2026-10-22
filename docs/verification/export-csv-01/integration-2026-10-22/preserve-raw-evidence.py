from pathlib import Path
import shutil
root=Path.cwd()
for name in ['grid-resize-test-01','cache-baseline-test-01']:
 src=root.parent/('codex-'+name)/'docs/verification'/name
 dst=root/'docs/verification'/name
 assert src.resolve().is_relative_to(Path('C:/src/Github/Glancelot/.worktrees'))
 shutil.copytree(src,dst,dirs_exist_ok=True)
p=root/'.gitattributes'
s=p.read_text(encoding='utf-8')
for name in ['export-csv-01/integration-2026-10-22','grid-resize-test-01','cache-baseline-test-01']:
 line=f'docs/verification/{name}/** -text'
 if line not in s:s+='\n'+line+'\n'
p.write_text(s,encoding='utf-8')
