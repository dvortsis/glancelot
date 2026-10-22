from pathlib import Path
import json,re,yaml
root=Path.cwd()
s=(root/'docs/WORK-QUEUE.yaml').read_text(encoding='utf-8')
start=s.index('  - id: PLATFORM-FIXTURE-SECURITY-01\n')
late=s[start:]
s=s[:start]
acc='    acceptance: Retire flagged fixture; no executable impersonation/self-copy/PATH override/suspended-thread startup; preserve real Unicode argv/CWD and COM/PIDL/fallback validation; independent PLATFORM review before execution, complete gates and integrated checks before releasing hold\n'
out='    outcome: Reviewed by Claude752758c/e7afce5 and merged334065e; integrated Windows29/29 364.45s and fresh Linux29/29 305.76s; platform26/26 zero skips; replacement qualified, retired fixture permanently blocked; production unchanged\n'
assert acc in late and out in late
late=late.replace(acc,'').replace(out,'')
pos=late.index('\n  - id: EXPORT-CSV-01')
late=late[:pos]+acc+out+late[pos:]
pos=s.index('next_milestone:')
s=s[:pos]+late.rstrip()+'\n'+s[pos:]
lines=[]
for line in s.splitlines():
 m=re.match(r'^(\s*(?:- )?[A-Za-z_][\w-]*: )(.*)$',line)
 if m:
  v=m[2]
  if ': ' in v and v and v[0] not in '\"\'[{|>':
   line=m[1]+json.dumps(v,ensure_ascii=False)
 lines.append(line)
s='\n'.join(lines)+'\n'
class Strict(yaml.SafeLoader):pass
def mapping(loader,node,deep=False):
 d={}
 for k,v in node.value:
  key=loader.construct_object(k,deep=deep)
  if key in d:raise ValueError(f'duplicate key {key} at line {k.start_mark.line+1}')
  d[key]=loader.construct_object(v,deep=deep)
 return d
Strict.add_constructor(yaml.resolver.BaseResolver.DEFAULT_MAPPING_TAG,mapping)
data=yaml.load(s,Loader=Strict)
ids=[x['id'] for x in data['tasks']]
assert len(ids)==len(set(ids))
assert 'EXPORT-CSV-01' in ids and 'PLATFORM-FIXTURE-SECURITY-01' in ids
(root/'.cache/queue-repaired.yaml').write_text(s,encoding='utf-8')
print(f'QUEUE REPAIR VALID: {len(ids)} unique tasks; strict duplicate-key parse passed; detached entries restored; fixture fields relocated')
