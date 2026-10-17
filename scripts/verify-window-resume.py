"""Process-level UX-12 regression checks using only generated media and isolated settings.
Usage: python scripts/verify-window-resume.py <path-to-glancelot-executable>
Put the matching Qt bin directory on PATH for an undeployed Windows build.
"""
import ctypes
import json
import os
from pathlib import Path
import struct
import subprocess
import sys
import tempfile
import time
import zlib
from contextlib import contextmanager


def png(path, width, height):
    def chunk(kind, data):
        return struct.pack('>I', len(data)) + kind + data + struct.pack('>I', zlib.crc32(kind + data))
    pixels = (b'\x00' + b'\x22\x66\xaa' * width) * height
    path.write_bytes(b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', width, height, 8, 2, 0, 0, 0)) + chunk(b'IDAT', zlib.compress(pixels)) + chunk(b'IEND', b''))


def read(path):
    try:
        return json.loads(path.read_text())
    except (OSError, ValueError):
        return {}


def until(predicate, timeout=12):
    end = time.monotonic() + timeout
    while time.monotonic() < end:
        if predicate():
            return
        time.sleep(.03)
    raise AssertionError('Timed out waiting for process state')


@contextmanager
def locked(path):
    if os.name == 'nt':
        kernel = ctypes.WinDLL('kernel32', use_last_error=True)
        kernel.CreateFileW.argtypes = [ctypes.c_wchar_p, ctypes.c_uint32, ctypes.c_uint32, ctypes.c_void_p, ctypes.c_uint32, ctypes.c_uint32, ctypes.c_void_p]
        kernel.CreateFileW.restype = ctypes.c_void_p
        kernel.CloseHandle.argtypes = [ctypes.c_void_p]
        handle = kernel.CreateFileW(str(path), 0xC0000000, 0, None, 4, 0x80, None)
        assert handle != ctypes.c_void_p(-1).value, ctypes.get_last_error()
        try:
            yield
        finally:
            kernel.CloseHandle(handle)
    else:
        import fcntl
        with path.open('a+b') as stream:
            fcntl.flock(stream, fcntl.LOCK_EX)
            try:
                yield
            finally:
                fcntl.flock(stream, fcntl.LOCK_UN)


def main():
    executable = str(Path(sys.argv[1]).resolve())
    children = []
    with tempfile.TemporaryDirectory(prefix='glancelot-resume-') as temp:
        root = Path(temp)
        config = root / 'config'
        config.mkdir()
        first, second, explicit = (root / name for name in ('one.png', 'two.png', 'explicit.png'))
        for path, size in ((first, (16, 10)), (second, (24, 18)), (explicit, (32, 20))):
            png(path, *size)
        source_bytes = {path: path.read_bytes() for path in (first, second, explicit)}
        env = dict(os.environ, GLANCELOT_CONFIG_DIR=str(config), QT_QPA_PLATFORM='offscreen')
        def start(name, image=None, duration=1300, extra=()):
            report = root / (name + '.json')
            args = [executable, '--quit-after', str(duration), '--report', str(report), *extra]
            if image:
                args.append(str(image))
            process = subprocess.Popen(args, env=env, stdout=subprocess.PIPE, stderr=subprocess.PIPE, creationflags=0x08000000 if os.name == 'nt' else 0)
            children.append(process)
            return process, report
        def finish(run, slot, image):
            process, report = run
            out, err = process.communicate(timeout=20)
            assert process.returncode == 0, (process.returncode, out, err)
            data = read(report)
            assert data.get('window_slot') == slot, data
            assert Path(data['image_path']) == image, data
            assert data['width'] > 0 and data['decoded_ms'] >= 0 and not data['loading'], data
            assert b'Error' not in err and b'ReferenceError' not in err, err
        def has_slot(number):
            return any(row.get('slot') == number for row in read(config / 'sessions.json').get('live', []))
        try:
            a = start('first', first, 3600)
            until(lambda: has_slot(1))
            b = start('second', second)
            finish(b, 2, second)  # close in reverse order
            finish(a, 1, first)
            a = start('resume-first', duration=3600)
            until(lambda: has_slot(1))
            b = start('resume-second')
            finish(b, 2, second)
            finish(a, 1, first)
            print('PASS: two independent processes retain positions across full restart and reverse close order', flush=True)
            finish(start('explicit', explicit), 1, explicit)
            slots = read(config / 'settings.json')['windows']
            assert Path(slots['2']['image']) == second
            print('PASS: explicit file overrides resume and unopened slot is retained', flush=True)
            with locked(config / 'sessions.json.lock'):
                delayed = start('delayed-restore', duration=6000)
                time.sleep(.5)
            finish(delayed, 1, explicit)
            with locked(config / 'sessions.json.lock'):
                delayed = start('delayed-explicit', first, 6000)
                time.sleep(.5)
            finish(delayed, 1, first)
            assert Path(read(config / 'settings.json')['windows']['1']['image']) == first
            print('PASS: real retry timer restores a saved image and remembers an explicit image decoded before claim', flush=True)
            with locked(config / 'settings.json.lock'):
                exiting = start('exit-owner', second, 800)
                until(lambda: bool(read(exiting[1])))
                assert exiting[0].poll() is None, 'exiting owner did not wait for final flush'
                replacement = start('during-exit', explicit, 1300)
                until(lambda: has_slot(2))
                assert has_slot(1), 'old slot released before final save'
            finish(exiting, 1, second)
            finish(replacement, 2, explicit)
            finish(start('after-exit'), 1, second)
            print('PASS: final flush holds slot ownership while a new process opens', flush=True)
            for path, data in source_bytes.items():
                assert path.read_bytes() == data, 'source image changed'
            print('PASS: all generated source media remain byte-identical', flush=True)
        finally:
            for process in children:
                if process.poll() is None:
                    process.kill()
                process.communicate()


if __name__ == '__main__':
    main()
