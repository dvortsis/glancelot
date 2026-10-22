#!/bin/sh
set -eu
if [ "$#" -ne 2 ]; then echo 'usage: linux_smoke.sh PACKAGE NEW_OUTPUT' >&2; exit 2; fi
script=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
if [ -e /tmp/.X99-lock ] || [ -e /tmp/.X11-unix/X99 ]; then
    echo 'Refusing an existing display endpoint' >&2; exit 2
fi
work=$(mktemp -d /tmp/glancelot-package-display.XXXXXX)
python3 - "$work/password" <<'PY'
import os,secrets,subprocess,sys
value=subprocess.run(['tigervncpasswd','-f'],input=(secrets.token_hex(4)+'\n').encode(),stdout=subprocess.PIPE,check=True).stdout
with os.fdopen(os.open(sys.argv[1],os.O_WRONLY|os.O_CREAT|os.O_EXCL,0o600),'wb') as target:
    target.write(value)
PY
Xtigervnc :99 -geometry 1180x780 -depth 24 -rfbport 5999 -localhost yes -SecurityTypes VncAuth -PasswordFile "$work/password" -nolisten tcp >"$work/display.log" 2>&1 &
display_pid=$!
finish() {
    status=$?
    kill "$display_pid" 2>/dev/null || true
    wait "$display_pid" 2>/dev/null || true
    if [ "$status" -ne 0 ]; then tail -n 20 "$work/display.log" >&2; fi
    exit "$status"
}
trap finish EXIT INT TERM
python3 - "$display_pid" <<'PY'
import os,pathlib,sys,time
for _ in range(100):
    os.kill(int(sys.argv[1]),0)
    if pathlib.Path('/tmp/.X11-unix/X99').exists(): break
    time.sleep(0.1)
else: raise SystemExit('Display startup deadline exceeded')
PY
export DISPLAY=:99 LANG=C.UTF-8 LC_ALL=C.UTF-8
python3 "$script/package_smoke.py" "$1" "$2"
