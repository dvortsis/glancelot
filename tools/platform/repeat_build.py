"""Run two independent package builds at one clean source revision and compare actual artifacts."""
import argparse
import json
import os
import re
import shutil
import subprocess
from pathlib import Path
from artifacts import compare, digest_file, encoded, require


def source_state(repo):
    commit = subprocess.check_output(["git", "-C", str(repo), "rev-parse", "HEAD"], text=True, timeout=30).strip()
    status = subprocess.check_output(["git", "-C", str(repo), "status", "--porcelain", "--untracked-files=normal"], text=True, timeout=30)
    require(not status, "controlled repeat-build experiment requires clean source")
    return commit


def repeat(repo, output, target, epoch):
    repo, output = Path(repo).resolve(), Path(output).absolute()
    require(not output.exists() and not output.is_symlink(), "repeat experiment output must be new")
    require(315532800 <= epoch <= 4354819198, "source epoch outside supported range")
    commit = source_state(repo)
    label = re.sub(r"[^A-Za-z0-9_.-]", "-", output.name)[:40] + "-" + commit[:12]
    builds = [repo / "build-codex-platform-01" / "repeat" / label / name for name in ("a", "b")]
    if target == "windows":
        require(os.name == "nt" and all(not path.exists() for path in builds), "Windows repeat needs two fresh assigned build directories")
    output.mkdir(parents=True)
    archives = []
    for index, name in enumerate(("a", "b")):
        require(source_state(repo) == commit, "source revision changed during repeat experiment")
        destination = output / name
        if os.name == "nt":
            shell = shutil.which("pwsh") or shutil.which("powershell")
            require(shell is not None, "PowerShell required for Windows-host package builds")
            script = "windows_package.ps1" if target == "windows" else "linux_package.ps1"
            command = [shell, "-NoProfile", "-File", str(repo / "tools/platform" / script), "-Output", str(destination), "-Epoch", str(epoch)]
            if target == "windows": command += ["-BuildDir", str(builds[index])]
            else: command += ["-Attempt", label + "-" + name]
        else:
            require(target == "linux", "Windows packaging requires a Windows host")
            command = ["sh", str(repo / "tools/platform/linux_package.sh"), "--output", str(destination), "--epoch", str(epoch), "--attempt", label + "-" + name]
        print("Starting independent", target, "build", name, "at", commit, flush=True)
        subprocess.run(command, cwd=repo, check=True, timeout=3600)
        require(source_state(repo) == commit, "source changed while package was building")
        artifact = destination / ("Glancelot-windows-x86_64.zip" if target == "windows" else "Glancelot-linux-x86_64.tar.gz")
        archives.append(artifact)
    result = compare(*archives)
    result.update(source_commit=commit, source_date_epoch=epoch, target=target,
                  independent_builds=2, status="identical" if result["archive_bytes_equal"] else "different")
    result["artifacts"] = [{"path": artifact.relative_to(output.resolve()).as_posix(), "sha256": digest_file(artifact)} for artifact in archives]
    require(result["metadata_equal"], "repeat builds used different provenance/conditions")
    (output / "comparison.json").write_bytes(encoded(result))
    print(json.dumps(result, indent=2), flush=True)
    return result


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--repo", type=Path, default=Path(__file__).resolve().parents[2])
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--target", choices=("windows", "linux"), required=True)
    parser.add_argument("--epoch", type=int, required=True)
    args = parser.parse_args()
    result = repeat(args.repo, args.output, args.target, args.epoch)
    raise SystemExit(0 if result["archive_bytes_equal"] else 1)
