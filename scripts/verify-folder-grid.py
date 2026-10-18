"""Generate disposable media and record native grid/resource evidence.
Usage: python scripts/verify-folder-grid.py <viewer-exe> <output-directory>
The matching Qt bin directory must be on PATH for undeployed builds.
"""
import importlib.util
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile


def main():
    executable=Path(sys.argv[1]).resolve()
    output=Path(sys.argv[2]).resolve()
    output.mkdir(parents=True,exist_ok=True)
    spec=importlib.util.spec_from_file_location('resume_fixture',Path(__file__).with_name('verify-window-resume.py'))
    fixture=importlib.util.module_from_spec(spec);spec.loader.exec_module(fixture)
    with tempfile.TemporaryDirectory(prefix='glancelot-grid-') as temp:
        root=Path(temp);media=root/'media';media.mkdir()
        for i in range(36):
            fixture.png(media/f'photo-{i:05}.png',40+(i%4)*20,30+(i%3)*20)
        # Failed previews must retain a selectable filename and stable cell geometry.
        (media/'photo-broken.png').write_bytes(b'not a png')
        first=media/'photo-00000.png'
        original=first.read_bytes()
        env=dict(os.environ,GLANCELOT_CONFIG_DIR=str(root/'config'),QT_QPA_PLATFORM='windows' if os.name=='nt' else os.environ.get('QT_QPA_PLATFORM','offscreen'))
        def run(name,theme,size,duration=2400):
            report=output/(name+'.json')
            result=subprocess.run([str(executable),str(first),'--show-grid','--theme-mode',theme,'--window-size',size,'--quit-after',str(duration),'--report',str(report),'--screenshot',str(output/(name+'.png'))],env=env,capture_output=True,timeout=30,creationflags=0x08000000 if os.name=='nt' else 0)
            (output/(name+'.stderr')).write_bytes(result.stderr)
            data=json.loads(report.read_text())
            assert result.returncode==0,(name,result.returncode,data,result.stderr)
            assert data['grid_visible'] and data['width']>0 and data['folder_count']>=37,data
            assert data['thumbnail_active']<=2 and data['thumbnail_pending']<=256 and data['thumbnail_cache_bytes']<=64*1024*1024,data
            print(f'PASS: {name}: rows={data["folder_count"]}, live cells={data.get("grid_live_cells")}, active={data["thumbnail_active"]}, pending={data["thumbnail_pending"]}, cache={data["thumbnail_cache_bytes"]}',flush=True)
            return data
        run('grid-compact-dark','dark','540x360')
        run('grid-compact-light','light','540x360')
        run('grid-wide-dark','dark','1180x780')
        for i in range(36,10000):
            (media/f'photo-{i:05}.png').write_bytes(original)
        large=run('grid-10000','dark','1180x780',6000)
        assert large['folder_count']==10001,large
        assert 0<large['grid_live_cells']<200,large
        assert first.read_bytes()==original
        assert len(list(media.iterdir()))==10001
        print('PASS: 10001 source entries unchanged; delegate/worker/queue/cache limits respected',flush=True)


if __name__=='__main__':
    main()
