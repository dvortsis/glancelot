"""Seal a real Qt deployment into a new portable Windows archive; never alter its inputs."""
import argparse
import json
import os
import re
import shutil
import subprocess
from pathlib import Path
from artifacts import MARKER, MAX_MANIFEST, archive, digest_file, encoded, read_limited, require, seal, verify_tree
from collect_build import cache_values, collect


def stage(repo, build, qt, deployed, output, epoch, source_identity=None):
    require(not Path(output).is_symlink(), "package output cannot be a link")
    repo, build, qt, output = (Path(p).resolve() for p in (repo, build, qt, output))
    deployed = Path(deployed).resolve() if deployed else None
    require(not output.exists() and (deployed is None or not output.is_relative_to(deployed)), "package output must be new and outside deployment")
    info = collect(repo, build, qt, "windows-x86_64")
    identity = None
    if source_identity:
        identity = json.loads(read_limited(Path(source_identity), MAX_MANIFEST))
        require(isinstance(identity, dict) and set(identity) == {"commit", "dirty", "origin"}
                and identity["origin"] == "declared", "recorded-build source identity must be explicitly declared")
        require(isinstance(identity["commit"], str) and re.fullmatch(r"[0-9a-f]{40}", identity["commit"])
                and type(identity["dirty"]) is bool, "recorded-build source revision invalid")
    if deployed:
        require(deployed.is_dir(), "Qt deployment missing")
        for name in ("glancelot.exe", "glancelot-decoder.exe"):
            require(digest_file(build / name) == digest_file(deployed / "bin" / name), "deployed executable differs from this build")
    output.mkdir(parents=True, exist_ok=False)
    root = output / "Glancelot"
    # Install into a fresh prefix. The legacy dist directory is reused by build.ps1
    # and can retain obsolete plugins or nested SBOM copies across package attempts.
    cmake = cache_values(build / "CMakeCache.txt")["CMAKE_COMMAND"]
    env = os.environ.copy()
    env["PATH"] = str(qt / "bin") + os.pathsep + env.get("PATH", "")
    subprocess.run([cmake, "--install", str(build), "--prefix", str(root)], env=env, check=True, timeout=300)
    for name in ("glancelot.exe", "glancelot-decoder.exe"):
        require(digest_file(build / name) == digest_file(root / "bin" / name), "fresh deployment executable differs from this build")
    for name in ("LICENSE", "README.md", "THIRD_PARTY_NOTICES.md"):
        shutil.copy2(repo / name, root / name)
    shutil.copy2(repo / "assets/fonts/geist/OFL.txt", root / "Geist-OFL.txt")
    shutil.copytree(qt / "sbom", root / "qt-sbom")
    require(any((root / "qt-sbom").glob("*.spdx.json")), "Qt SBOM absent")
    (root / MARKER).write_bytes(b"1\n")
    (output / "build-info.json").write_bytes(encoded(info))
    manifest = seal(root, None if identity else repo, info, "windows-x86_64", epoch,
                    ["bin/glancelot.exe", "bin/glancelot-decoder.exe"], identity)
    verify_tree(root)
    artifact = output / "Glancelot-windows-x86_64.zip"
    archive(root, artifact, "zip")
    print("PASS sealed Windows deployment and independently verified portable archive")
    return manifest


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ("repo", "build", "qt", "output"):
        parser.add_argument("--" + name, type=Path, required=True)
    parser.add_argument("--deployed", type=Path)
    parser.add_argument("--source-identity", type=Path)
    parser.add_argument("--epoch", type=int, required=True)
    args = parser.parse_args()
    stage(args.repo, args.build, args.qt, args.deployed, args.output, args.epoch, args.source_identity)


if __name__ == "__main__":
    main()
