import json,re,sys
from pathlib import Path
expected=['baseline','fixture-generation','initial-publication','ordinary-measurement-complete','filtered-writer','filtered-measurement-complete','clear-filter','fixture-removal','rename-complete-baseline','fixture-destruction']
reports=[]
for filename in sys.argv[1:]:
 text=Path(filename).read_text();lines=[x for x in text.splitlines() if 'FS_WATCH_' in x]
 phases=[re.search(r'FS_WATCH_PHASE (\S+)',x).group(1) for x in lines if 'FS_WATCH_PHASE' in x]
 assert phases==expected,(filename,phases)
 assert len(lines)==17,(filename,len(lines))
 for kind in ['generated','removed']:
  counts=[int(re.search(r'FS_WATCH_PROGRESS '+kind+r' (\d+)',x).group(1)) for x in lines if 'FS_WATCH_PROGRESS '+kind in x]
  assert counts==[25000,50000,75000],(filename,kind,counts)
 elapsed=[int(re.search(r'total_ms (\d+)',x).group(1)) for x in lines]
 assert elapsed==sorted(elapsed),(filename,elapsed)
 assert 'FS_WATCH_LIFETIME last_phase fixture-destruction' in lines[-1],filename
 assert '3 passed, 0 failed, 0 skipped' in text,filename
 reports.append({'file':filename,'markers':len(lines),'monotonic_elapsed':True,'phase_order':phases,'lifetime_ms':elapsed[-1],'QtTest':'3 passed 0 failed 0 skipped','lines':lines})
print(json.dumps({'passed':True,'reports':reports},indent=2))
