"""Build an offline X11 GUI package from the configured pinned Qt toolchain."""
import argparse
import os
import re
import shutil
import subprocess
from pathlib import Path
from artifacts import MARKER, archive, encoded, require, seal, verify_tree
from collect_build import collect


def elf(path):
    with path.open("rb") as source:
        return source.read(4) == b"\x7fELF"


def stage(repo, build, qt, output, epoch, source_commit, source_dirty, toolchain_image):
    repo, build, qt, output = (Path(value).resolve() for value in (repo, build, qt, output))
    require(not output.exists(), "Linux package output must be new")
    require(re.fullmatch(r"[0-9a-f]{40}", source_commit) is not None and source_dirty in ("true", "false"), "invalid declared source identity")
    require(re.fullmatch(r"sha256:[0-9a-f]{64}", toolchain_image) is not None, "missing exact toolchain image identity")
    info = collect(repo, build, qt, "linux-x86_64")
    output.mkdir(parents=True)
    root = output / "Glancelot"
    (root / "bin").mkdir(parents=True)
    (root / "qt/lib").mkdir(parents=True)
    (root / "qt/plugins/platforms").mkdir(parents=True)
    for name in ("glancelot", "glancelot-decoder"):
        shutil.copy2(build / name, root / "bin" / name)
    (root / "bin/qt.conf").write_bytes(b"[Paths]\nPrefix=../qt\n")
    (root / "Glancelot.sh").write_bytes(b'''#!/bin/sh
set -eu
here=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
export LD_LIBRARY_PATH="$here/qt/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
exec "$here/bin/glancelot" "$@"
''')
    (root / "Glancelot.sh").chmod(0o755)
    # Ship the X11/default and diagnostic platform paths. This toolkit does not
    # contain QtWayland; do not claim native Wayland support from an X11 smoke.
    for name in ("libqxcb.so", "libqoffscreen.so", "libqminimal.so"):
        shutil.copy2(qt / "plugins/platforms" / name, root / "qt/plugins/platforms" / name)
    for folder in ("imageformats", "xcbglintegrations"):
        shutil.copytree(qt / "plugins" / folder, root / "qt/plugins" / folder)
    shutil.copytree(qt / "qml", root / "qt/qml", symlinks=False)
    shutil.copytree(qt / "sbom", root / "qt-sbom", symlinks=False)
    require(any((root / "qt-sbom").glob("*.spdx.json")), "Qt SPDX inventory absent")
    for name in ("README.md", "LICENSE", "THIRD_PARTY_NOTICES.md"):
        shutil.copy2(repo / name, root / name)
    shutil.copy2(repo / "assets/fonts/geist/OFL.txt", root / "Geist-OFL.txt")
    # ldd enumerates the transitive dependency closure. Copy only dependencies
    # resolved inside the pinned Qt installation, materializing each SONAME.
    env = os.environ.copy(); env["LD_LIBRARY_PATH"] = str(qt / "lib")
    libraries = {}
    for path in sorted(root.rglob("*")):
        if not path.is_file() or not elf(path): continue
        result = subprocess.run(["ldd", str(path)], env=env, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True, timeout=30)
        require(result.returncode == 0 and "not found" not in result.stdout, "configured ELF dependency closure incomplete")
        for line in result.stdout.splitlines():
            match = re.match(r"\s*(\S+) => (/\S+) \(", line)
            if not match: continue
            name, resolved = match[1], Path(match[2])
            if resolved.resolve().is_relative_to(qt):
                require("/" not in name and "\\" not in name, "unsafe dependency SONAME")
                require(name not in libraries or libraries[name].resolve() == resolved.resolve(), "colliding dependency SONAME")
                libraries[name] = resolved
    require("libQt6Core.so.6" in libraries and "libQt6Quick.so.6" in libraries, "viewer Qt dependency closure missing")
    for name, path in sorted(libraries.items()):
        shutil.copyfile(path, root / "qt/lib" / name)
    (root / "BUILD-ENVIRONMENT.json").write_bytes(encoded({"schema": 1, "toolchain_image": toolchain_image,
        "runtime_baseline": "ubuntu-24.04-x86_64-x11", "qt_deployment": "materialized_transitive_sonames",
        "native_wayland": "not_in_this_toolkit"}))
    shutil.copy2(repo / "tools/platform/LINUX-PACKAGE.md", root / "LINUX-PACKAGE.md")
    (root / MARKER).write_bytes(b"1\n")
    (output / "build-info.json").write_bytes(encoded(info))
    seal(root, None, info, "linux-x86_64", epoch, ["bin/glancelot", "bin/glancelot-decoder", "Glancelot.sh"],
         {"commit": source_commit, "dirty": source_dirty == "true", "origin": "declared"})
    verify_tree(root)
    archive(root, output / "Glancelot-linux-x86_64.tar.gz", "tar.gz")
    print("PASS sealed Linux X11 GUI package and independently verified archive")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ("repo", "build", "qt", "output"):
        parser.add_argument("--" + name, type=Path, required=True)
    for name in ("source-commit", "source-dirty", "toolchain-image"):
        parser.add_argument("--" + name, required=True)
    parser.add_argument("--epoch", type=int, required=True)
    args = parser.parse_args()
    stage(args.repo, args.build, args.qt, args.output, args.epoch, args.source_commit, args.source_dirty, args.toolchain_image)
