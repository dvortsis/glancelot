#!/usr/bin/env python3
"""REL-01: print the `docker build` provenance arguments for this checkout, one token per line.

The Docker context has no .git, so the image learns its commit, version and build time from build args
(docs/RELEASING.md "Build provenance"). This prints them from the root CMakeLists.txt project() version, the
given pre-release suffix, `git rev-parse HEAD` and one UTC instant, which is passed both as SOURCE_DATE_EPOCH (the
binary's build time) and as the created label, so the two agree. The Dockerfile checks the labels against the binary.

    docker build $(python3 scripts/docker_build_args.py --kind release --image glancelot-browser) .
    docker build @(python scripts/docker_build_args.py --suffix=-rc.1 --image glancelot-browser) .   # PowerShell

--image NAME adds `-t NAME:<version>` and `-t NAME:sha-<full SHA>` tags.
"""
from __future__ import annotations

import argparse
import datetime
import os
import re
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
SUFFIX = re.compile(r"^-(rc|dev)\.(0|[1-9][0-9]*)$")
FULL_SHA = re.compile(r"^([0-9a-f]{40}|[0-9a-f]{64})$")


def project_version(cmakelists: Path) -> str:
    match = re.search(r"project\(\s*Glancelot\s+VERSION\s+(\d+\.\d+\.\d+)\b", cmakelists.read_text(encoding="utf-8"))
    if not match:
        raise SystemExit(f"{cmakelists}: no project(Glancelot VERSION X.Y.Z)")
    return match.group(1)


def head_sha(root: Path) -> str | None:
    try:
        top = subprocess.run(["git", "-C", str(root), "rev-parse", "--show-toplevel"], capture_output=True, text=True, check=True)
        if Path(top.stdout.strip()).resolve() != root.resolve():
            return None
        head = subprocess.run(["git", "-C", str(root), "rev-parse", "--verify", "HEAD^{commit}"], capture_output=True, text=True, check=True)
    except (OSError, subprocess.CalledProcessError):
        return None
    sha = head.stdout.strip().lower()
    return sha if FULL_SHA.match(sha) else None


def build_args(suffix: str, kind: str, epoch: int, sha: str | None, version: str) -> list[tuple[str, str]]:
    created = datetime.datetime.fromtimestamp(epoch, tz=datetime.timezone.utc).strftime("%Y-%m-%dT%H:%M:%SZ")
    return [
        ("GLANCELOT_VERSION", version + suffix),
        ("GLANCELOT_VERSION_SUFFIX", suffix),
        ("GLANCELOT_BUILD_KIND", kind),
        ("GLANCELOT_REVISION", sha or ""),
        ("GLANCELOT_CREATED", created),
        ("SOURCE_DATE_EPOCH", str(epoch)),
    ]


def main(argv: list[str]) -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--suffix", default="", help="empty, -rc.N or -dev.N; write it as --suffix=-rc.N")
    parser.add_argument("--kind", default="", choices=["", "release", "rc", "dev", "local"], help="empty infers it from the suffix")
    parser.add_argument("--epoch", type=int, help="build time in seconds since 1970 UTC (default: SOURCE_DATE_EPOCH, else now)")
    parser.add_argument("--image", help="also print -t NAME:<version> and -t NAME:sha-<full SHA>")
    args = parser.parse_args(argv)
    if args.suffix and not SUFFIX.match(args.suffix):
        parser.error(f"--suffix {args.suffix!r} must be empty, -rc.N or -dev.N")
    kind = args.kind or ("rc" if args.suffix.startswith("-rc.") else "dev" if args.suffix.startswith("-dev.") else "local")
    sha = head_sha(ROOT)
    if kind in ("release", "rc") and not sha:
        parser.error(f"this {kind} image needs this checkout's Git SHA, and git rev-parse HEAD failed")
    epoch = args.epoch
    if epoch is None:
        env = os.environ.get("SOURCE_DATE_EPOCH", "")
        epoch = int(env) if env.isdigit() else int(datetime.datetime.now(datetime.timezone.utc).timestamp())
    values = build_args(args.suffix, args.kind, epoch, sha, project_version(ROOT / "CMakeLists.txt"))
    for name, value in values:
        print("--build-arg")
        print(f"{name}={value}")
    if args.image:
        print("-t")
        print(f"{args.image}:{values[0][1]}")
        if sha:
            print("-t")
            print(f"{args.image}:sha-{sha}")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
