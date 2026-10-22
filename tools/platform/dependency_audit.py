"""Independently resolve packaged ELF dependencies with development Qt unavailable."""
import argparse
import os
import re
import subprocess
from pathlib import Path
from artifacts import digest_file, encoded, require, verify_tree


def audit(package, output):
    package, output = Path(package).resolve(), Path(output).resolve()
    manifest = verify_tree(package)
    require(not output.exists(), "dependency report already exists")
    env = os.environ.copy()
    env["PATH"] = "/usr/bin:/bin"; env["LD_LIBRARY_PATH"] = str(package / "qt/lib")
    seen, reports = set(), []
    for row in manifest["files"]:
        path = package / row["path"]
        with path.open("rb") as source:
            if source.read(4) != b"\x7fELF": continue
        if row["sha256"] in seen: continue
        seen.add(row["sha256"])
        process = subprocess.run(["ldd", str(path)], env=env, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True, timeout=30)
        if process.returncode != 0 or "not found" in process.stdout:
            print("Unresolved packaged ELF:", row["path"], flush=True)
            for line in process.stdout.splitlines():
                if "not found" in line: print(line.strip(), flush=True)
        require(process.returncode == 0 and "not found" not in process.stdout, "packaged ELF dependency missing")
        dependencies = []
        for line in process.stdout.splitlines():
            match = re.match(r"\s*(\S+) => (/\S+) \(", line)
            if not match: continue
            name, resolved = match[1], Path(match[2]).resolve()
            require("/" not in name and "\\" not in name, "unsafe SONAME")
            inside = resolved.is_relative_to(package)
            if name.startswith(("libQt6", "libicu")):
                require(inside, "Qt/ICU resolved outside package")
            dependencies.append({"soname": name, "origin": "package" if inside else "system"})
        reports.append({"path": row["path"], "sha256": digest_file(path), "dependencies": dependencies})
    require(reports and (package / "bin/glancelot").is_file(), "no packaged ELF executables")
    output.write_bytes(encoded({"schema": 1, "passed": True, "target": manifest["target"], "elf_files": reports}))
    print("PASS independent ELF dependency closure; Qt/ICU resolve exclusively inside package")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("package", type=Path); parser.add_argument("output", type=Path)
    args = parser.parse_args(); audit(args.package, args.output)
