"""Seal a real Qt deployment into a new portable Windows archive; never alter its inputs."""
import argparse
import shutil
from pathlib import Path
from artifacts import MARKER, archive, digest_file, encoded, require, scan, seal, verify_tree
from collect_build import collect


def stage(repo, build, qt, deployed, output, epoch):
    repo, build, qt, deployed, output = (Path(p).resolve() for p in (repo, build, qt, deployed, output))
    require(not output.exists() and not output.is_relative_to(deployed), "package output must be new and outside deployment")
    info = collect(repo, build, qt, "windows-x86_64")
    require(deployed.is_dir(), "Qt deployment missing")
    require(not deployed.is_symlink(), "Qt deployment root cannot be a link")
    for name in ("glancelot.exe", "glancelot-decoder.exe"):
        require(digest_file(build / name) == digest_file(deployed / "bin" / name), "deployed executable differs from this build")
    for name in ("LICENSE", "README.md", "THIRD_PARTY_NOTICES.md", "Geist-OFL.txt"):
        require((deployed / name).is_file(), "required package notice absent")
    require(any((deployed / "qt-sbom").glob("*.spdx.json")), "Qt SBOM absent")
    scan(deployed, set())  # Reject unsupported entries/bounds before copying.
    output.mkdir(parents=True, exist_ok=False)
    root = output / "Glancelot"
    shutil.copytree(deployed, root)
    (root / MARKER).write_bytes(b"1\n")
    (output / "build-info.json").write_bytes(encoded(info))
    manifest = seal(root, repo, info, "windows-x86_64", epoch, ["bin/glancelot.exe", "bin/glancelot-decoder.exe"])
    verify_tree(root)
    artifact = output / "Glancelot-windows-x86_64.zip"
    archive(root, artifact, "zip")
    print("PASS sealed Windows deployment and independently verified portable archive")
    return manifest


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ("repo", "build", "qt", "deployed", "output"):
        parser.add_argument("--" + name, type=Path, required=True)
    parser.add_argument("--epoch", type=int, required=True)
    args = parser.parse_args()
    stage(args.repo, args.build, args.qt, args.deployed, args.output, args.epoch)


if __name__ == "__main__":
    main()
