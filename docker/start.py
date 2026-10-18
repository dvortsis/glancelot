"""Supervise a single private VNC desktop, browser proxy and Glancelot."""
import os
import pathlib
import re
import signal
import subprocess
import sys
import time

children = []
stopping = False


def stop(signum=None, frame=None):
    global stopping
    stopping = True


signal.signal(signal.SIGTERM, stop)
signal.signal(signal.SIGINT, stop)


def launch(arguments):
    child = subprocess.Popen(arguments)
    children.append(child)
    return child


try:
    size = os.environ.get('DESKTOP_SIZE', '1440x900')
    if not re.fullmatch(r'[1-9][0-9]{2,3}x[1-9][0-9]{2,3}', size):
        raise ValueError('DESKTOP_SIZE must be WIDTHxHEIGHT')
    width, height = map(int, size.split('x'))
    if not (560 <= width <= 3840 and 400 <= height <= 2160):
        raise ValueError('DESKTOP_SIZE must be between 560x400 and 3840x2160')
    secret = pathlib.Path(os.environ['VNC_PASSWORD_FILE']).read_text().rstrip('\r\n')
    # VNC authentication uses exactly the first eight bytes. Reject misleading passwords.
    if len(secret) != 8 or not secret.isascii() or any(c.isspace() for c in secret):
        raise ValueError('The VNC password file must contain exactly 8 ASCII non-space characters')
    runtime = pathlib.Path('/tmp/glancelot-runtime')
    runtime.mkdir(mode=0o700, exist_ok=True)
    os.environ['XDG_RUNTIME_DIR'] = str(runtime)
    password = runtime / 'vnc-passwd'
    password.write_bytes(subprocess.run(['tigervncpasswd', '-f'], input=secret.encode(), check=True, capture_output=True).stdout)
    password.chmod(0o600)
    # A killed container restarts on the same filesystem; stale X locks would block the new display.
    for stale in ('/tmp/.X0-lock', '/tmp/.X11-unix/X0'):
        pathlib.Path(stale).unlink(missing_ok=True)
    launch(['Xtigervnc', ':0', '-geometry', size, '-depth', '24', '-rfbport', '5900', '-localhost', 'yes', '-SecurityTypes', 'VncAuth', '-PasswordFile', str(password), '-AlwaysShared', '-desktop', 'Glancelot'])
    for _ in range(100):
        if stopping or children[0].poll() is not None:
            raise RuntimeError('Display startup interrupted')
        if pathlib.Path('/tmp/.X11-unix/X0').exists():
            break
        time.sleep(0.1)
    else:
        raise RuntimeError('Display startup timed out')
    launch(['openbox'])
    launch(['/opt/glancelot/glancelot', '--window-size', f'{max(540,width-4)}x{max(360,height-40)}'])
    launch(['websockify', '--web=/usr/share/novnc', '6080', '127.0.0.1:5900'])
    while not stopping and all(child.poll() is None for child in children):
        time.sleep(0.2)
    failed = not stopping
except Exception as error:
    print(f'Glancelot desktop: {error}', file=sys.stderr)
    failed = True
finally:
    # Flush viewer preferences before shutting down its display.
    for child in reversed(children):
        if child.poll() is None:
            if child.args[0] == '/opt/glancelot/glancelot':
                # WM_DELETE_WINDOW invokes Qt close/aboutToQuit and final settings flush.
                subprocess.run(['wmctrl', '-c', 'Glancelot'], check=False)
                try:
                    child.wait(timeout=5)
                    continue
                except subprocess.TimeoutExpired:
                    pass
            child.terminate()
            try:
                child.wait(timeout=5)
            except subprocess.TimeoutExpired:
                child.kill()
                child.wait()
sys.exit(1 if failed else 0)
