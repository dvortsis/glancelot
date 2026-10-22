from pathlib import Path
import yaml
class Strict(yaml.SafeLoader):pass
def mapping(loader,node,deep=False):
 result={}
 for key_node,value_node in node.value:
  key=loader.construct_object(key_node,deep=deep)
  if key in result:raise ValueError(f'duplicate key {key}: line{key_node.start_mark.line+1}')
  result[key]=loader.construct_object(value_node,deep=deep)
 return result
Strict.add_constructor(yaml.resolver.BaseResolver.DEFAULT_MAPPING_TAG,mapping)
data=yaml.load(Path('docs/WORK-QUEUE.yaml').read_text(encoding='utf-8-sig'),Loader=Strict)
tasks=data['tasks'];ids=[t['id'] for t in tasks]
assert len(ids)==56 and len(ids)==len(set(ids)),ids
for task_id in ['EXPORT-CSV-01','GRID-RESIZE-TEST-01','CACHE-BASELINE-TEST-01','PLATFORM-FIXTURE-SECURITY-01']:
 task=next(t for t in tasks if t['id']==task_id)
 print(f"{task_id}: {task['state']}")
assert 'Retire flagged fixture' not in next(t for t in tasks if t['id']=='EXPORT-CSV-01')['acceptance']
assert 'Retire flagged fixture' in next(t for t in tasks if t['id']=='PLATFORM-FIXTURE-SECURITY-01')['acceptance']
print('VALID: 56 unique tasks; strict duplicate-key parse; correct acceptance ownership')
print('integration_lock:',data['integration_lock'])
