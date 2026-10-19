"""Build an offline X11 GUI package from the configured pinned Qt toolchain."""
import argparse
import json
import os
import re
import shutil
import subprocess
import tempfile
from pathlib import Path
from artifacts import MARKER, archive, encoded, require, seal, verify_tree
from collect_build import collect


def elf(path):
    with path.open("rb") as source:
        return source.read(4) == b"\x7fELF"


def copy_qml(repo, qt, destination):
    env = os.environ.copy(); env.update(LANG="C.UTF-8", LC_ALL="C.UTF-8")
    with tempfile.TemporaryDirectory(prefix="glancelot-qml-imports-") as temporary:
        basic = Path(temporary) / "BasicStyle.qml"
        basic.write_text("import QtQuick\nimport QtQuick.Controls.Basic\nItem {}\n", encoding="utf-8")
        result = subprocess.run([str(qt / "libexec/qmlimportscanner"), "-rootPath", str(repo / "ui"),
            "-importPath", str(qt / "qml"), "-qmlFiles", str(basic)], env=env,
            stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True, timeout=60)
    require(result.returncode == 0 and len(result.stdout) <= 8 * 1024**2, "QML import resolution failed")
    imports = json.loads(result.stdout)
    require(isinstance(imports, list) and len(imports) <= 512, "QML import count exceeds bound")
    modules, unavailable_styles = {}, []
    for entry in imports:
        if entry.get("type") != "module": continue
        if not entry.get("path"):
            name = entry.get("name")
            if name in ("QtQuick.Controls.Windows", "QtQuick.Controls.macOS", "QtQuick.Controls.iOS"):
                unavailable_styles.append(name)
                continue
            if name != "Glancelot": print("Unresolved QML import:", name, flush=True)
            require(name == "Glancelot", "required QML module unresolved")
            continue
        path = Path(entry["path"]).resolve()
        require(path.is_relative_to(qt / "qml") and path.is_dir(), "QML module outside pinned toolkit")
        modules[path.relative_to(qt / "qml").as_posix()] = entry["name"]
    require("QtQuick" in modules and "QtQuick/Controls/Basic" in modules, "viewer/Basic QML imports absent")
    for relative in sorted(modules):
        module = qt / "qml" / relative
        for folder, directories, files in os.walk(module):
            base = Path(folder)
            # Child modules have their own scanner record. Do not accidentally
            # include unrelated SDK modules by recursively copying their parent.
            directories[:] = [name for name in directories if name != "designer" and not (base / name / "qmldir").exists()]
            for name in files:
                source = base / name
                require(source.resolve().is_relative_to(qt), "QML resource escaped toolkit")
                target = destination / source.relative_to(qt / "qml")
                target.parent.mkdir(parents=True, exist_ok=True)
                shutil.copyfile(source, target)
    return {"modules": [{"name": modules[path], "relative_path": path} for path in sorted(modules)],
            "fixed_application_style": "Basic", "unavailable_foreign_platform_styles": sorted(unavailable_styles)}


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
    qml_imports = copy_qml(repo, qt, root / "qt/qml")
    (root / "QML-IMPORTS.json").write_bytes(encoded({"schema": 1, **qml_imports}))
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
        if result.returncode != 0 or "not found" in result.stdout:
            print("Unresolved packaged ELF:", path.relative_to(root).as_posix(), flush=True)
            for line in result.stdout.splitlines():
                if "not found" in line: print(line.strip(), flush=True)
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
