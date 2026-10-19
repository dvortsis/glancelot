"""Launch a sealed package with development Qt removed and disposable source media."""
import argparse
import ctypes
import json
import os
import platform
import shutil
import struct
import subprocess
import sys
import time
import zlib
from pathlib import Path
from artifacts import MANIFEST, digest_file, encoded, read_limited, require, verify_tree


def chunk(kind, data):
    return struct.pack(">I", len(data)) + kind + data + struct.pack(">I", zlib.crc32(kind + data))


def fixture(path):
    width, height = 1024, 768
    rows = []
    for y in range(height):
        colors = ((255, 0, 0), (0, 255, 0)) if y < height // 2 else ((0, 0, 255), (255, 255, 255))
        rows.append(b"\0" + bytes(colors[0]) * (width // 2) + bytes(colors[1]) * (width // 2))
    path.write_bytes(b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, 8, 2, 0, 0, 0))
                     + chunk(b"IDAT", zlib.compress(b"".join(rows), 9)) + chunk(b"IEND", b""))


def screenshot_colors(path):
    data = read_limited(path, 64 * 1024**2)
    require(data[:8] == b"\x89PNG\r\n\x1a\n", "screenshot is not PNG")
    offset, compressed, header = 8, bytearray(), None
    while offset + 12 <= len(data):
        size = struct.unpack(">I", data[offset:offset + 4])[0]
        require(size <= 64 * 1024**2 and offset + size + 12 <= len(data), "invalid PNG chunk bounds")
        kind, payload = data[offset + 4:offset + 8], data[offset + 8:offset + size + 8]
        require(zlib.crc32(kind + payload) == struct.unpack(">I", data[offset + size + 8:offset + size + 12])[0], "invalid PNG chunk CRC")
        if kind == b"IHDR":
            require(header is None and size == 13, "invalid PNG header")
            header = struct.unpack(">IIBBBBB", payload)
        elif kind == b"IDAT":
            compressed.extend(payload)
        elif kind == b"IEND":
            break
        offset += size + 12
    require(header is not None, "PNG dimensions absent")
    width, height, depth, color, method, filtering, interlace = header
    require(depth == 8 and color in (2, 6) and method == filtering == interlace == 0
            and 0 < width <= 8192 and 0 < height <= 8192 and width * height <= 16000000, "unsupported screenshot format or dimensions")
    bpp = 3 if color == 2 else 4
    stride = width * bpp
    expected = (stride + 1) * height
    decoder = zlib.decompressobj()
    raw = decoder.decompress(compressed, expected + 1)
    require(len(raw) == expected and decoder.eof and not decoder.unused_data and not decoder.unconsumed_tail, "invalid screenshot decompression")
    previous = bytearray(stride)
    counts = {"red": 0, "green": 0, "blue": 0}
    for y in range(height):
        start = y * (stride + 1)
        mode = raw[start]
        require(mode <= 4, "invalid PNG row filter")
        row = bytearray(raw[start + 1:start + stride + 1])
        for x in range(stride):
            left, up, upper_left = (row[x - bpp] if x >= bpp else 0), previous[x], (previous[x - bpp] if x >= bpp else 0)
            if mode == 1: predictor = left
            elif mode == 2: predictor = up
            elif mode == 3: predictor = (left + up) // 2
            elif mode == 4:
                p = left + up - upper_left
                a, b, c = abs(p - left), abs(p - up), abs(p - upper_left)
                predictor = left if a <= b and a <= c else up if b <= c else upper_left
            else: predictor = 0
            row[x] = (row[x] + predictor) & 255
        for x in range(0, stride, bpp):
            r, g, b = row[x:x + 3]
            if bpp == 4 and row[x + 3] < 200: continue
            if r > 220 and g < 40 and b < 40: counts["red"] += 1
            if g > 220 and r < 40 and b < 40: counts["green"] += 1
            if b > 220 and r < 40 and g < 40: counts["blue"] += 1
        previous = row
    require(all(value >= 100 for value in counts.values()), "expected generated source colors absent from actual screenshot")
    return {"width": width, "height": height, "source_color_pixels": counts}


def qt_modules(process):
    if os.name == "nt":
        from ctypes import wintypes
        modules = (wintypes.HMODULE * 2048)()
        needed = wintypes.DWORD()
        enum = ctypes.windll.psapi.EnumProcessModulesEx
        enum.argtypes = [wintypes.HANDLE, ctypes.POINTER(wintypes.HMODULE), wintypes.DWORD, ctypes.POINTER(wintypes.DWORD), wintypes.DWORD]
        enum.restype = wintypes.BOOL
        if not enum(process._handle, modules, ctypes.sizeof(modules), ctypes.byref(needed), 3):
            return []
        require(needed.value <= ctypes.sizeof(modules), "module audit exceeded bound")
        filename = ctypes.windll.psapi.GetModuleFileNameExW
        filename.argtypes = [wintypes.HANDLE, wintypes.HMODULE, wintypes.LPWSTR, wintypes.DWORD]
        filename.restype = wintypes.DWORD
        paths = []
        for index in range(needed.value // ctypes.sizeof(wintypes.HMODULE)):
            buffer = ctypes.create_unicode_buffer(32768)
            if filename(process._handle, modules[index], buffer, len(buffer)):
                paths.append(Path(buffer.value))
    else:
        maps = read_limited(Path("/proc") / str(process.pid) / "maps", 16 * 1024**2).decode()
        paths = [Path(line.split(None, 5)[5].replace("\\040", " ")) for line in maps.splitlines() if len(line.split(None, 5)) == 6 and line.split(None, 5)[5].startswith("/")]
    return sorted(set(path.resolve() for path in paths if path.name.lower().startswith(("qt6", "libqt6"))
                      or path.name.lower() in ("qwindows.dll", "libqxcb.so", "qoffscreen.dll")))


def run(package, output, image, missing=False, dimensions=(1024, 768), color_oracle=True):
    output.mkdir()
    env = os.environ.copy()
    for name in ("QTDIR", "QT_PLUGIN_PATH", "QT_QPA_PLATFORM_PLUGIN_PATH", "QML_IMPORT_PATH", "QML2_IMPORT_PATH",
                 "QT_QUICK_BACKEND", "LD_LIBRARY_PATH", "GLANCELOT_TRACE_FILE", "GLANCELOT_PERF_REPLAY", "QT_LOGGING_RULES"):
        env.pop(name, None)
    if os.name == "nt":
        windows = Path(os.environ["SystemRoot"])
        env["PATH"] = str(windows / "System32") + os.pathsep + str(windows)
        env.update(QT_QPA_PLATFORM="windows", QSG_RHI_BACKEND="d3d11")
        program = package / "bin/glancelot.exe"
    else:
        env["PATH"] = "/usr/bin:/bin"
        env.update(QT_QPA_PLATFORM="xcb", LIBGL_ALWAYS_SOFTWARE="1", QSG_RHI_BACKEND="opengl")
        program = package / "Glancelot.sh"
    env["GLANCELOT_CONFIG_DIR"] = str(output / "config")
    report, screenshot = output / "report.json", output / "screenshot.png"
    with (output / "viewer.log").open("wb") as log:
        process = subprocess.Popen([str(program), "--no-resume", "--quit-after", "6000", "--report", str(report),
            "--screenshot", str(screenshot), "--window-size", "1180x780", str(image)], env=env, stdout=log, stderr=subprocess.STDOUT)
        observed = []
        try:
            deadline = time.monotonic() + 4
            ready_after = time.monotonic() + 2
            display_plugin = "qwindows.dll" if os.name == "nt" else "libqxcb.so"
            while process.poll() is None and time.monotonic() < deadline:
                observed = qt_modules(process)
                if len(observed) >= 4 and display_plugin in {path.name.lower() for path in observed} and time.monotonic() >= ready_after: break
                time.sleep(0.1)
            require(len(observed) >= 4 and display_plugin in {path.name.lower() for path in observed}
                    and all(path.is_relative_to(package) for path in observed), "Qt modules did not load exclusively from package")
            process.wait(timeout=20)
        finally:
            if process.poll() is None:
                process.kill(); process.wait(timeout=5)
    require(process.returncode == 0, "packaged viewer failed")
    value = json.loads(read_limited(report, 1024**2))
    require(value["qt"] == "6.8.3" and not value["loading"], "packaged viewer did not settle")
    summary = {"qt": value["qt"], "version": value["version"], "graphics_api": value["graphics_api"],
        "device_pixel_ratio": value["device_pixel_ratio"], "frames": value["frames"],
        "first_image_frame_ms": value["first_image_frame_ms"], "qt_modules": sorted({path.name for path in observed}),
        "qt_modules_from_package": True, "development_qt_removed_from_path": True}
    if missing:
        require(value["width"] == value["height"] == 0 and value["first_image_frame_ms"] < 0
                and "Decoder is missing" in value["status"], "missing packaged decoder was not diagnosed")
        summary["missing_decoder_diagnosed"] = True
    else:
        require((value["width"], value["height"]) == dimensions and value["decoded_ms"] >= 0
                and value["first_image_frame_ms"] >= 0 and value["frames"] > 0, "packaged image decode/render failed")
        if color_oracle: summary["screenshot"] = screenshot_colors(screenshot)
        else: require(screenshot.is_file() and screenshot.stat().st_size > 100, "JPEG screenshot absent")
        summary["decoded_dimensions"] = list(dimensions)
    return summary


def smoke(package, output):
    package, output = Path(package).resolve(), Path(output).resolve()
    require(not output.exists() and not output.is_relative_to(package), "smoke output must be new and outside package")
    manifest = verify_tree(package)
    output.mkdir(parents=True)
    image = output / "generated-quadrants.png"
    fixture(image)
    original = digest_file(image)
    positive = run(package, output / "positive", image)
    require(positive["version"] == manifest["build"]["app_version"], "binary version differs from provenance")
    jpeg = output / "generated-smoke.jpg"
    shutil.copyfile(Path(__file__).parent / "fixtures/smoke.jpg", jpeg)
    jpeg_original = digest_file(jpeg)
    require(jpeg_original == "801532c946d6e60c7490c96ec8adb258506e854e98c14bf48966322dd597cf3b", "generated JPEG fixture changed")
    jpeg_result = run(package, output / "jpeg", jpeg, dimensions=(64, 48), color_oracle=False)
    broken = output / "missing-decoder-package"
    decoder_name = "glancelot-decoder.exe" if os.name == "nt" else "glancelot-decoder"
    shutil.copytree(package, broken, ignore=lambda folder, names: {decoder_name} if Path(folder).resolve() == package / "bin" else set())
    require(not (broken / "bin" / decoder_name).exists(), "negative fixture still has decoder")
    negative = run(broken, output / "missing-decoder", image, missing=True)
    require(digest_file(image) == original, "generated source media changed")
    require(digest_file(jpeg) == jpeg_original, "generated JPEG source changed")
    verify_tree(package)
    result = {"schema": 1, "passed": True, "source": manifest["source"], "target": manifest["target"],
              "manifest_sha256": digest_file(package / MANIFEST),
              "executable_hashes": {row["path"]: row["sha256"] for row in manifest["files"] if row["executable"]},
              "source_media_sha256_unchanged": original, "package_inventory_unchanged": True,
              "generated_jpeg_sha256_unchanged": jpeg_original,
              "positive": positive, "jpeg": jpeg_result, "negative": negative,
              "physical_color_dpi_refresh_qualification": "unverified"}
    result["environment"] = {"os": platform.system(), "release": platform.release(), "version": platform.version(),
        "machine": platform.machine(), "python": platform.python_version(), "logical_cpu_count_visible": os.cpu_count(),
        "cpu_affinity_count": len(os.sched_getaffinity(0)) if hasattr(os, "sched_getaffinity") else None,
        "power_thermal_state": "uncontrolled", "filesystem_cache_state": "uncontrolled", "active_gpu_identity": "unverified"}
    (output / "smoke.json").write_bytes(encoded(result))
    print("PASS packaged decode/render/source colors, package-only Qt modules, missing-decoder diagnosis and unchanged source/package hashes")
    return result


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("package", type=Path)
    parser.add_argument("output", type=Path)
    args = parser.parse_args()
    smoke(args.package, args.output)
