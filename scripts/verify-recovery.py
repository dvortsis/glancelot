#!/usr/bin/env python3
"""Exercise the actual viewer with disposable settings/media; retain local evidence."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import selectors
import struct
import subprocess
import sys
import tempfile
import time
import zlib


def require(condition, message):
    if not condition:
        raise AssertionError(message)


def read_json(path):
    return json.loads(Path(path).read_text(encoding="utf-8"))


def write_json(path, value):
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(value, ensure_ascii=False, separators=(",", ":")), encoding="utf-8")


def digest_tree(root):
    return {str(p.relative_to(root)): hashlib.sha256(p.read_bytes()).hexdigest()
            for p in root.rglob("*") if p.is_file()}


def members(root):
    return {name: (root / name).read_bytes() if (root / name).exists() else None
            for name in ("settings.json", "settings.json.bak", "settings.json.damaged")}


def png(path, reverse=False):
    def chunk(kind, data):
        return struct.pack(">I", len(data)) + kind + data + struct.pack(">I", zlib.crc32(kind + data))
    colors = [(255, 0, 0), (0, 255, 0), (0, 0, 255)]
    if reverse:
        colors.reverse()
    row = b"".join(bytes(colors[x // 20]) for x in range(60))
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", 60, 36, 8, 2, 0, 0, 0))
                     + chunk(b"IDAT", zlib.compress((b"\0" + row) * 36)) + chunk(b"IEND", b""))


def screenshot_rgb(path, report):
    require(path.stat().st_size <= 64 * 1024 * 1024, "Screenshot file exceeds bound")
    data = path.read_bytes()
    require(data[:8] == b"\x89PNG\r\n\x1a\n", "Screenshot is not PNG")
    offset, compressed, dimensions = 8, bytearray(), None
    while offset + 12 <= len(data):
        length, = struct.unpack_from(">I", data, offset)
        require(length <= len(data) - offset - 12, "Truncated screenshot chunk")
        kind, body = data[offset + 4:offset + 8], data[offset + 8:offset + 8 + length]
        require(zlib.crc32(kind + body) == struct.unpack_from(">I", data, offset + 8 + length)[0], "Screenshot CRC mismatch")
        if kind == b"IHDR":
            dimensions = struct.unpack(">IIBBBBB", body)
        elif kind == b"IDAT":
            compressed.extend(body)
        offset += length + 12
        if kind == b"IEND":
            break
    require(dimensions is not None, "Missing screenshot header")
    width, height, depth, color, compression, filtering, interlace = dimensions
    require(0 < width * height <= 16 * 1024 * 1024 and depth == 8 and color in (2, 6)
            and compression == filtering == interlace == 0, "Unsupported/big screenshot encoding")
    channels = 3 if color == 2 else 4
    stride, rows = width * channels, []
    inflater = zlib.decompressobj()
    raw = inflater.decompress(compressed, (stride + 1) * height + 1)
    require(inflater.eof and len(raw) == (stride + 1) * height, "Invalid screenshot pixel payload")
    previous = bytearray(stride)
    for y in range(height):
        mode = raw[y * (stride + 1)]
        require(mode <= 4, "Unsupported PNG filter")
        row = bytearray(raw[y * (stride + 1) + 1:(y + 1) * (stride + 1)])
        for x in range(stride):
            left, up = (row[x - channels] if x >= channels else 0), previous[x]
            corner = previous[x - channels] if x >= channels else 0
            if mode == 1:
                prediction = left
            elif mode == 2:
                prediction = up
            elif mode == 3:
                prediction = (left + up) // 2
            elif mode == 4:
                p = left + up - corner
                a, b, c = abs(p - left), abs(p - up), abs(p - corner)
                prediction = left if a <= b and a <= c else up if b <= c else corner
            else:
                prediction = 0
            row[x] = (row[x] + prediction) & 255
        rows.append(row)
        previous = row
    canvas, dpr = report["canvas"], report["device_pixel_ratio"]
    require(canvas["visible"] and not canvas["renderer_error"], "RHI canvas unavailable/failed")
    cx, cy = (canvas["x"] + canvas["width"] / 2) * dpr, (canvas["y"] + canvas["height"] / 2) * dpr
    samples = []
    for index, dx in enumerate((-20, 0, 20)):
        x, y = round(cx + dx * canvas["zoom"]), round(cy)
        require(0 <= x < width and 0 <= y < height, "Canvas sample outside screenshot")
        pixel = list(rows[y][x * channels:x * channels + 3])
        require(pixel[index] >= 200 and all(pixel[other] <= 80 for other in range(3) if other != index),
                f"Actual canvas RGB sample {index} at {x},{y}: {pixel}")
        samples.append({"x": x, "y": y, "rgb": pixel})
    return {"width": width, "height": height, "canvas_samples": samples}


class Verification:
    def __init__(self, executable, work):
        self.executable, self.root = executable, work
        self.steps, self.active, self.number = [], [], 0

    def environment(self, config, gui=False, unsafe_renderer=False):
        env = {key: value for key, value in os.environ.items() if not key.startswith(("GLANCELOT_", "QSG_"))}
        for key in ("QT_QUICK_BACKEND", "QMLSCENE_DEVICE", "QT_QPA_PLATFORM_PLUGIN_PATH", "QT_QPA_PLATFORMTHEME", "QT_D3D_ADAPTER_INDEX"):
            env.pop(key, None)
        env["GLANCELOT_CONFIG_DIR"] = str(config)
        if gui:
            env["QT_QPA_PLATFORM"] = "windows" if os.name == "nt" else "xcb"
            if os.name != "nt":
                env.update(LIBGL_ALWAYS_SOFTWARE="1", GALLIUM_DRIVER="llvmpipe", QSG_RHI_BACKEND="opengl")
        else:
            # Any accidental QGuiApplication construction would fail immediately.
            env["QT_QPA_PLATFORM"] = "glancelot-invalid-recovery-test-platform"
        if unsafe_renderer:
            env.update(QT_QUICK_BACKEND="software", QMLSCENE_DEVICE="softwarecontext", QSG_RHI_BACKEND="null", QT_D3D_ADAPTER_INDEX="9999")
        return env

    def run(self, args, config, expected=0, gui=False, unsafe_renderer=False, json_result=True):
        self.number += 1
        stem = self.root / f"command-{self.number:03d}"
        env = self.environment(config, gui, unsafe_renderer)
        trace = stem.with_suffix(".trace.txt")
        env["GLANCELOT_TRACE_FILE"] = str(trace)
        command = [str(self.executable), *map(str, args)]
        started = time.monotonic()
        result = subprocess.run(command, env=env, stdout=subprocess.PIPE, stderr=subprocess.PIPE, timeout=25)
        stem.with_suffix(".stdout.txt").write_bytes(result.stdout)
        stem.with_suffix(".stderr.txt").write_bytes(result.stderr)
        self.steps.append({"command": command, "returncode": result.returncode, "seconds": round(time.monotonic() - started, 3), "evidence": str(stem)})
        require(result.returncode == expected, f"Expected exit {expected}, got {result.returncode}: {command}; stderr={result.stderr[-2000:]!r}")
        if not gui:
            require(not trace.exists(), "Headless path constructed a controller")
        require(len(result.stdout) <= 3 * 1024 * 1024, "Unbounded stdout report")
        return json.loads(result.stdout) if json_result else result

    def gui(self, config, image=None, safe=False, no_resume=False, screenshot=False):
        name = f"viewer-{self.number + 1:03d}"
        report, capture = self.root / (name + ".json"), self.root / (name + ".png")
        args = ["--quit-after", "2500", "--report", report]
        if screenshot:
            args += ["--screenshot", capture]
        if safe:
            args += ["--safe-mode", "--theme-mode", "dark"]
        if no_resume:
            args += ["--no-resume"]
        if image is not None:
            args += ["--", image]
        self.run(args, config, gui=True, unsafe_renderer=safe, json_result=False)
        result = read_json(report)
        require(not result["loading"], "Viewer still loading at exit")
        if no_resume:
            require(result["window_slot"] == 0, "No-resume acquired a window slot")
        if image is not None:
            require(Path(result["image_path"]) == image, "Explicit Unicode/-- image operand was lost")
        if safe:
            require(result["safe_mode"] and result["status"].startswith("Safe mode"), "Missing safe-mode state/feedback")
            require(Path(result["settings_directory"]) != config and not Path(result["settings_directory"]).exists(), "Private safe settings root was not cleaned")
            require(result["rhi"]["observed"] and result["rhi"]["software_detection"] != "not-detected", f"No actual software device evidence: {result['rhi']}")
            require(result["rhi"]["backend"] == (3 if os.name == "nt" else 2), f"Unexpected actual safe RHI backend: {result['rhi']}")
            require(result["frames"] > 0 and result["first_image_frame_ms"] >= 0, "No actual decoded-image frame swaps")
            require(result["width"] == 60 and result["height"] == 36, "Generated image was not decoded")
            if screenshot:
                write_json(self.root / (name + "-rgb.json"), screenshot_rgb(capture, result))
        return result

    def execute(self):
        images = self.root / "generated-media"
        first, second = images / "雪景-😀.png", images / "-explicit-配置.png"
        png(first)
        png(second, reverse=True)
        media_before = digest_tree(images)
        source, target = self.root / "source", self.root / "target"
        source.mkdir(); target.mkdir()
        write_json(source / "settings.json", {"schema": 1, "wheel": "pan", "unknown": {"future": True}})
        write_json(target / "settings.json", {"schema": 1, "wheel": "zoom", "unknown": None, "windows": {"2": {"future": 7}}})
        package = self.root / "sealed-配置-😀"
        require(self.run(["--settings-backup", package], source)["code"] == "Ok", "Backup did not seal")
        before = members(target)
        proposed = self.run(["--settings-diff", package, "--settings-scope", "viewing"], target)
        require(proposed["code"] == "Ok" and not proposed["truncated"], "Diff failed")
        require(any(row["path"] == ["wheel"] and row["treatment"] == "apply" for row in proposed["rows"]), "Missing applied diff row")
        require(any(row["path"] == ["unknown"] and row["treatment"] == "preserve" and row["had_before"] and row["before"] is None for row in proposed["rows"]), "Missing null-preserving diff row")
        restore = ["--settings-restore", package, "--settings-scope", "viewing", "--settings-preview"]
        self.run(restore + ["sf1:" + "0" * 64], target, 3)
        require(members(target) == before, "Conflict changed original members")
        applied = self.run(restore + [proposed["fingerprint"]], target)
        require(applied["code"] == "Committed", "Restore did not commit")
        require((Path(applied["preimage"]) / "settings.json").read_bytes() == before["settings.json"], "Preimage differs from raw original")
        require(read_json(target / "settings.json")["unknown"] is None, "Unknown target preference lost")
        self.run(restore + [proposed["fingerprint"]], target, 3)
        fresh = self.run(["--settings-diff", package, "--settings-scope", "viewing"], target)
        require(self.run(restore + [fresh["fingerprint"]], target)["code"] == "NoChange", "No-change restore wrote")
        (target / "settings.json.bak").write_bytes(b"changed backup only")
        self.run(restore + [fresh["fingerprint"]], target, 3)
        require(not (target / "sessions.json").exists(), "Global maintenance claimed a window")
        invalid = [["--settings-backup"], ["--settings-diff", package, "--safe-mode"], ["--safe-mode", "--reset-settings"],
                   ["--settings-restore", package], ["--settings-scope", "viewing"], ["--unknown"],
                   ["--settings-diff", package, "-platform", "offscreen"], ["--settings-diff", package, "--settings-preview", fresh["fingerprint"]]]
        before = members(target)
        for args in invalid:
            require(self.run(args, target, 2)["code"] == "ArgumentError", "Wrong argument-error code")
        for args in (["--help"], ["--version"]):
            self.run(args, target, json_result=False)
        require(members(target) == before, "Argument/help/version changed preferences")
        future = self.root / "future"
        write_json(future / "settings.json", {"schema": 2});write_json(future / "settings.json.bak", {"schema": 1})
        require(self.run(["--settings-diff", package], future, 5)["code"] == "Unsupported", "Future primary was downgraded")
        bad_source = self.root / "corrupt-source";bad_source.mkdir()
        (bad_source / "settings.json").write_bytes(b"corrupt primary")
        write_json(bad_source / "settings.json.bak", {"schema": 1, "wheel": "pan"})
        backup_package = self.root / "last-good-package"
        self.run(["--settings-backup", backup_package], bad_source)
        self.run(["--settings-diff", backup_package], target, 6)
        require(self.run(["--settings-diff", backup_package, "--settings-source", "last-good"], target)["code"] == "Ok", "Explicit last-good source failed")
        (package / "settings.json").write_bytes((package / "settings.json").read_bytes() + b"\n")
        require(self.run(["--settings-diff", package], target, 6)["code"] == "Corrupt", "Package tampering accepted")
        original = self.root / "safe-original";original.mkdir()
        write_json(original / "settings.json", {"schema": 1, "wheel": "pan", "layouts": {"1": {"version": 999}}})
        for name in ("settings.json.bak", "settings.json.damaged", "sessions.json"):
            (original / name).write_bytes(b"original diagnostic bytes\0" + name.encode())
        original_before = digest_tree(original)
        safe = self.gui(original, first, safe=True, screenshot=True)
        require(safe["wheel_mode"] == "navigate", "Safe mode loaded original preferences")
        require(digest_tree(original) == original_before, "Safe mode changed original namespace")
        self.gui(original, first, safe=True, no_resume=True, screenshot=True)
        require(digest_tree(original) == original_before, "Safe no-resume changed original namespace")
        normal = self.gui(original, first, no_resume=True)
        require(normal["wheel_mode"] == "pan" and not normal["safe_mode"], "Later normal launch did not see original preferences")
        require(digest_tree(original) == original_before, "No-resume launch changed original namespace")
        self.restart_and_offline(first, second)
        require(digest_tree(images) == media_before, "Viewing changed generated original media")

    def restart_and_offline(self, first, second):
        config, source = self.root / "restart-config", self.root / "position-source"
        config.mkdir()
        write_json(source / "settings.json", {"schema": 1, "windows": {"1": {"folder": str(second.parent), "image": str(second), "saved": 1}}})
        package = self.root / "position-package"
        self.run(["--settings-backup", package], source)
        args = [str(self.executable), "--quit-after", "30000", "--", str(first)]
        stdout = (self.root / "interrupted.stdout.txt").open("wb")
        stderr = (self.root / "interrupted.stderr.txt").open("wb")
        process = subprocess.Popen(args, env=self.environment(config, gui=True), stdout=stdout, stderr=stderr)
        self.active.append(process)
        try:
            deadline = time.monotonic() + 12
            old = None
            while time.monotonic() < deadline:
                require(process.poll() is None, "Viewer exited before interrupted checkpoint")
                try:
                    root = read_json(config / "settings.json")
                    old = root["recovery"]["windows"]["1"]
                    if old["state"] == "dirty" and int(old["sequence"]) > 0 and Path(root["windows"]["1"]["image"]) == first:
                        break
                except (OSError, ValueError, KeyError):
                    pass
                time.sleep(0.05)
            else:
                raise AssertionError("Successful dirty checkpoint was not observed before interruption")
            selection = ["--settings-diff", package, "--settings-scope", "position", "--settings-slot", "1"]
            require(self.run(selection, config, 7)["code"] == "Busy", "Offline restore allowed a live owner")
            process.kill();process.wait(timeout=5)
            write_json(self.root / "interruption.json", {"command": args, "pid": process.pid, "exit": process.returncode, "dirty_checkpoint": old})
        finally:
            if process.poll() is None:
                process.kill();process.wait(timeout=5)
            stdout.close();stderr.close()
        require(read_json(config / "settings.json")["recovery"]["windows"]["1"]["state"] == "dirty", "Interrupted process was marked clean")
        resumed = self.gui(config)
        require(Path(resumed["image_path"]) == first and resumed["width"] == 60, "Restart did not lazily restore successful checkpoint")
        record = read_json(config / "settings.json")["recovery"]["windows"]["1"]
        require(record["state"] == "clean" and record["claimNonce"] != old["claimNonce"], "Restart did not acquire/finish its own claim")
        proposed = self.run(selection, config)
        applied = self.run(["--settings-restore", package, "--settings-scope", "position", "--settings-slot", "1", "--settings-preview", proposed["fingerprint"]], config)
        require(applied["code"] == "Committed", "Offline position restore failed")
        record = read_json(config / "settings.json")["recovery"]["windows"]["1"]
        require(record["state"] == "clean" and record["claimNonce"] == "" and record["origin"] == "restore", "Offline restore created a live claim")
        restored = self.gui(config)
        require(Path(restored["image_path"]) == second, "Restored checkpoint did not resume")
        failed = self.gui(config, second.parent / "missing.png")
        require(failed["width"] == 0, "Failed explicit open was replaced by lazy resume")
        require(Path(read_json(config / "settings.json")["windows"]["1"]["image"]) == second, "Failed decode overwrote prior successful position")


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--executable", type=Path, required=True)
    parser.add_argument("--work-dir", type=Path, required=True)
    parser.add_argument("--xvfb", type=Path)
    args = parser.parse_args()
    args.work_dir.mkdir(parents=True, exist_ok=True)
    work = Path(tempfile.mkdtemp(prefix="run-", dir=args.work_dir.resolve()))
    verifier = Verification(args.executable.resolve(), work)
    server, server_log = None, None
    try:
        require(verifier.executable.is_file(), "Required viewer executable missing")
        if sys.platform.startswith("linux") and not os.environ.get("DISPLAY"):
            require(args.xvfb is not None and args.xvfb.is_file(), "An owned Xvfb is required for actual RHI proof")
            server_log = (work / "xvfb.stderr.txt").open("wb")
            server = subprocess.Popen([str(args.xvfb), "-displayfd", "1", "-screen", "0", "1280x900x24", "-nolisten", "tcp"], stdout=subprocess.PIPE, stderr=server_log)
            selector = selectors.DefaultSelector();selector.register(server.stdout, selectors.EVENT_READ)
            require(selector.select(8), "Owned Xvfb did not provide a display")
            display = server.stdout.readline().strip();selector.close()
            require(display.isdigit() and server.poll() is None, "Owned Xvfb failed")
            os.environ["DISPLAY"] = ":" + display.decode("ascii")
        verifier.execute()
        write_json(work / "result.json", {"schema": 1, "passed": True, "steps": verifier.steps, "physical_hardware": "unverified"})
        print(f"Recovery process verification passed ({len(verifier.steps)} completed commands); evidence: {work}")
        return 0
    except Exception as error:
        write_json(work / "result.json", {"schema": 1, "passed": False, "error": str(error), "steps": verifier.steps})
        print(f"Recovery process verification FAILED: {error}; evidence: {work}", file=sys.stderr)
        return 1
    finally:
        for process in verifier.active:
            if process.poll() is None:
                process.kill();process.wait(timeout=5)
        if server is not None:
            if server.poll() is None:
                server.terminate()
                try:
                    server.wait(timeout=3)
                except subprocess.TimeoutExpired:
                    server.kill();server.wait(timeout=3)
            server.stdout.close()
        if server_log is not None:
            server_log.close()


if __name__ == "__main__":
    sys.exit(main())
