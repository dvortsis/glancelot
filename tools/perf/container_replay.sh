#!/bin/sh
# Isolated software-GL replay desktop; use an unpublished, network-isolated test container.
set -eu
if [ "$#" -lt 4 ] || [ "$#" -gt 5 ]; then
    echo 'usage: container_replay.sh EXE FIXTURES NEW_OUTPUT SOURCE_COMMIT [RUNS]' >&2
    exit 2
fi
script=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
exe=$1
fixtures=$2
output=$3
commit=$4
runs=${5:-1}
if [ -e /tmp/.X99-lock ] || [ -e /tmp/.X11-unix/X99 ]; then
    echo 'Refusing to replace an existing display-99 endpoint' >&2
    exit 2
fi
work=$(mktemp -d /tmp/glancelot-perf-display.XXXXXX)
python3 - "$work/password" <<'PY'
import os,secrets,subprocess,sys
password=(secrets.token_hex(4)+'\n').encode('ascii')
result=subprocess.run(['tigervncpasswd','-f'],input=password,stdout=subprocess.PIPE,check=True)
fd=os.open(sys.argv[1],os.O_WRONLY|os.O_CREAT|os.O_EXCL,0o600)
with os.fdopen(fd,'wb') as target:
    target.write(result.stdout)
PY
Xtigervnc :99 -geometry 1180x780 -depth 24 -rfbport 5999 -localhost yes -SecurityTypes VncAuth -PasswordFile "$work/password" -nolisten tcp >"$work/display.log" 2>&1 &
display_pid=$!
finish() {
    status=$?
    kill "$display_pid" 2>/dev/null || true
    wait "$display_pid" 2>/dev/null || true
    if [ "$status" -ne 0 ]; then tail -n 30 "$work/display.log" >&2; fi
    exit "$status"
}
trap finish EXIT INT TERM
python3 - "$display_pid" <<'PY'
import os,pathlib,sys,time
pid=int(sys.argv[1])
for _ in range(100):
    os.kill(pid,0)
    if pathlib.Path('/tmp/.X11-unix/X99').exists():
        break
    time.sleep(0.1)
else:
    raise SystemExit('Replay display startup deadline exceeded')
PY
export DISPLAY=:99 QT_QPA_PLATFORM=xcb LIBGL_ALWAYS_SOFTWARE=1 LANG=C.UTF-8 LC_ALL=C.UTF-8
unset QT_QUICK_BACKEND
sh "$script/replay.sh" --exe "$exe" --fixtures "$fixtures" --output "$output" --source-commit "$commit" --runs "$runs" --steps 3 --cycles 2 --require-render
