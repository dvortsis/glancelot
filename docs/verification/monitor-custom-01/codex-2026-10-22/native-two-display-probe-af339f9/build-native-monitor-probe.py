from pathlib import Path
import subprocess
root=Path.cwd();build=root/'build-codex-monitor-custom-01';qt=Path('C:/src/Github/Glancelot/.tools/Qt/6.8.3/msvc2022_64');obj=root/'.cache/native-monitor-probe.obj';exe=root/'.cache/native-monitor-probe.exe'
includes=[root/'include',root/'src/render',qt/'include']+list((qt/'include').iterdir())
subprocess.run(['cl.exe','/nologo','/std:c++20','/Zc:__cplusplus','/permissive-','/DQT_NO_DEBUG','/EHsc','/MD','/DUNICODE','/D_UNICODE','/c',str(root/'.cache/native-monitor-probe.cpp'),'/Fo'+str(obj)]+['/I'+str(p) for p in includes if p.is_dir()],check=True)
lines=(build/'build.ninja').read_text().splitlines();i=next(i for i,line in enumerate(lines) if line.startswith('build monitor_customization_tests.exe:'));objects=lines[i].split(' | ')[0].split()[3:];objects=[o for o in objects if not o.endswith('tests\\monitor_customization_tests.cpp.obj')];libs=next(line.split(' = ',1)[1].split() for line in lines[i:i+12] if line.strip().startswith('LINK_LIBRARIES ='))
subprocess.run(['link.exe','/nologo','/SUBSYSTEM:CONSOLE','/OUT:'+str(exe),str(obj)]+objects+libs,cwd=build,check=True)
