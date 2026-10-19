"""Offline package provenance, deterministic archives and independent integrity checks."""
import argparse
import gzip
import hashlib
import json
import re
import stat
import struct
import subprocess
import tarfile
import time
import zipfile
from pathlib import Path, PurePosixPath

MANIFEST = "provenance.json"
MARKER = ".glancelot-package-staging"
MAX_FILES = 50000
MAX_FILE = 2 * 1024**3
MAX_TOTAL = 10 * 1024**3
MAX_MANIFEST = 8 * 1024**2
INFO_KEYS = {"app_version", "qt_version", "compiler_id", "compiler_version",
             "cmake_version", "build_type", "cxx_standard", "flags"}


def require(condition, message):
    if not condition:
        raise ValueError(message)


def encoded(value):
    return (json.dumps(value, sort_keys=True, indent=2, ensure_ascii=True) + "\n").encode("utf-8")


def read_limited(path, limit):
    require(Path(path).stat().st_size <= limit, "metadata exceeds size bound")
    with Path(path).open("rb") as source:
        raw = source.read(limit + 1)
    require(len(raw) <= limit, "metadata changed beyond size bound")
    return raw


def digest_stream(stream, expected=None, limit=MAX_FILE):
    digest = hashlib.sha256()
    size = 0
    while True:
        block = stream.read(1024 * 1024)
        if not block:
            break
        size += len(block)
        require(size <= limit and (expected is None or size <= expected), "payload exceeds bound")
        digest.update(block)
    if expected is not None:
        require(size == expected, "payload size differs")
    return size, digest.hexdigest()


def digest_file(path):
    with Path(path).open("rb") as stream:
        return digest_stream(stream, limit=MAX_TOTAL + 128 * 1024**2)[1]


def safe_path(value):
    require(isinstance(value, str) and 0 < len(value) <= 1024, "invalid relative path")
    require("\\" not in value and ":" not in value and not any(ord(c) < 32 for c in value), "unsafe path characters")
    path = PurePosixPath(value)
    require(bool(path.parts) and not path.is_absolute() and path.as_posix() == value and not any(p in (".", "..") for p in path.parts), "unsafe relative path")
    require(all(not part.endswith((".", " ")) for part in path.parts), "nonportable path suffix")
    return value


def build_info(value):
    require(isinstance(value, dict) and set(value) == INFO_KEYS, "invalid build identity fields")
    for key in INFO_KEYS - {"flags", "cxx_standard"}:
        require(isinstance(value[key], str) and re.fullmatch(r"[A-Za-z0-9_.+ -]{1,128}", value[key]), "invalid build identifier")
    require(value["qt_version"] == "6.8.3" and value["cxx_standard"] == 20, "wrong pinned Qt or C++ standard")
    require(isinstance(value["flags"], list) and len(value["flags"]) <= 128, "invalid flags")
    for flag in value["flags"]:
        require(isinstance(flag, str) and len(flag) <= 128 and re.fullmatch(r"(?:[-/][A-Za-z0-9_+=.,:+-]+|[A-Za-z0-9_+=.,]+)", flag), "flags must be path-free tokens")
    return value


def scan(root, executable):
    root = Path(root).resolve()
    files, directories = [], []
    seen = set()
    total = 0
    paths = []
    for path in root.rglob("*"):
        require(len(paths) < MAX_FILES + 1, "too many package entries")
        require(not path.is_symlink() and path.resolve().is_relative_to(root), "package links are not materialized")
        paths.append(path)
    for path in sorted(paths):
        name = safe_path(path.relative_to(root).as_posix())
        require(not path.is_symlink() and path.resolve().is_relative_to(root), "package links are not materialized")
        require(name.casefold() not in seen, "case-colliding package entries")
        seen.add(name.casefold())
        if path.is_dir():
            directories.append(name)
        else:
            require(path.is_file(), "unsupported package entry")
            if name == MANIFEST:
                continue
            require(path.stat().st_size <= MAX_FILE, "package file exceeds bound")
            with path.open("rb") as stream:
                size, digest = digest_stream(stream)
            total += size
            require(total <= MAX_TOTAL, "package exceeds total bound")
            files.append({"path": name, "size": size, "sha256": digest, "executable": name in executable})
        require(len(files) + len(directories) <= MAX_FILES, "too many package entries")
    require(executable <= {row["path"] for row in files}, "executable absent from package")
    return files, directories


def validate_manifest(value):
    require(isinstance(value, dict) and set(value) == {"schema", "source", "target", "build", "source_date_epoch", "directories", "files"}, "invalid provenance fields")
    require(type(value["schema"]) is int and value["schema"] == 1, "unsupported provenance schema")
    source = value["source"]
    require(isinstance(source, dict) and set(source) == {"commit", "dirty", "origin"}, "invalid source identity")
    require(isinstance(source["commit"], str) and re.fullmatch(r"[0-9a-f]{40}", source["commit"]) and type(source["dirty"]) is bool, "invalid source revision")
    require(source["origin"] in ("git", "declared"), "invalid source provenance origin")
    require(value["target"] in ("windows-x86_64", "linux-x86_64"), "unsupported target")
    build_info(value["build"])
    epoch = value["source_date_epoch"]
    require(type(epoch) is int and 315532800 <= epoch <= 4354819198, "epoch outside ZIP timestamp range")
    require(isinstance(value["files"], list) and isinstance(value["directories"], list)
            and len(value["files"]) + len(value["directories"]) <= MAX_FILES, "invalid package entry count")
    names = {MANIFEST.casefold()}
    directories = set()
    for name in value["directories"]:
        safe_path(name)
        require(name.casefold() not in names, "duplicate directory")
        names.add(name.casefold()); directories.add(name)
    total = 0
    for row in value["files"]:
        require(isinstance(row, dict) and set(row) == {"path", "size", "sha256", "executable"}, "invalid file record")
        name = safe_path(row["path"])
        require(name.casefold() not in names, "duplicate file")
        names.add(name.casefold())
        require(type(row["size"]) is int and 0 <= row["size"] <= MAX_FILE and type(row["executable"]) is bool, "invalid file size or mode")
        require(isinstance(row["sha256"], str) and re.fullmatch(r"[0-9a-f]{64}", row["sha256"]), "invalid file digest")
        total += row["size"]
    require(total <= MAX_TOTAL, "package exceeds total bound")
    for name in [*value["directories"], *(row["path"] for row in value["files"])]:
        require(all(str(parent) in directories for parent in PurePosixPath(name).parents if str(parent) != "."), "missing parent directory")
    require(any(row["path"] == MARKER for row in value["files"]), "generated package marker absent")
    return value


def read_manifest(raw):
    require(len(raw) <= MAX_MANIFEST, "manifest exceeds bound")
    return validate_manifest(json.loads(raw))


def seal(root, repo, info, target, epoch, executable, declared_source=None):
    require(not Path(root).is_symlink(), "staging root must not be a link")
    root = Path(root).resolve()
    require(read_limited(root / MARKER, 2) == b"1\n", "not a generated package staging root")
    require(not (root / MANIFEST).exists(), "package already sealed")
    require((repo is None) != (declared_source is None), "choose Git or declared provenance")
    if repo is not None:
        repo = Path(repo).resolve()
        commit = subprocess.check_output(["git", "-C", str(repo), "rev-parse", "HEAD"], text=True, timeout=30).strip()
        status = subprocess.check_output(["git", "-C", str(repo), "status", "--porcelain", "--untracked-files=normal"], text=True, timeout=30)
        source = {"commit": commit, "dirty": bool(status), "origin": "git"}
    else:
        require(isinstance(declared_source, dict) and declared_source.get("origin") == "declared", "container provenance must be marked declared")
        source = declared_source
    files, directories = scan(root, {safe_path(name) for name in executable})
    value = validate_manifest({"schema": 1, "source": source,
        "target": target, "build": build_info(info), "source_date_epoch": epoch,
        "directories": directories, "files": files})
    raw = encoded(value)
    require(len(raw) <= MAX_MANIFEST, "manifest exceeds bound")
    with (root / MANIFEST).open("xb") as output:
        output.write(raw)
    return value


def verify_tree(root):
    root = Path(root).resolve()
    value = read_manifest(read_limited(root / MANIFEST, MAX_MANIFEST))
    files, directories = scan(root, {row["path"] for row in value["files"] if row["executable"]})
    require(files == value["files"] and directories == value["directories"], "package inventory differs")
    return value


def archive(root, output, kind):
    root, output = Path(root).resolve(), Path(output).resolve()
    require(not output.is_relative_to(root), "archive must be outside staging root")
    value = verify_tree(root)
    manifest_raw = read_limited(root / MANIFEST, MAX_MANIFEST)
    timestamp = time.gmtime(value["source_date_epoch"] - value["source_date_epoch"] % 2)[:6]
    rows = {row["path"]: row for row in value["files"]}
    directories = set(value["directories"])
    entries = sorted([*directories, *rows, MANIFEST])
    sidecar = output.with_name(output.name + ".json")
    require(not output.exists() and not sidecar.exists(), "archive output already exists")
    with output.open("xb") as destination:
        if kind == "zip":
            with zipfile.ZipFile(destination, "w", compression=zipfile.ZIP_STORED) as target:
                for name in entries:
                    directory = name in directories
                    item = zipfile.ZipInfo(name + "/" if directory else name, timestamp)
                    item.create_system = 3
                    mode = 0o755 if directory or rows.get(name, {}).get("executable") else 0o644
                    item.external_attr = ((stat.S_IFDIR if directory else stat.S_IFREG) | mode) << 16
                    if directory:
                        target.writestr(item, b"")
                    else:
                        with (root / name).open("rb") as source, target.open(item, "w", force_zip64=True) as sink:
                            while block := source.read(1024 * 1024):
                                sink.write(block)
        elif kind == "tar.gz":
            with gzip.GzipFile(filename="", mode="wb", fileobj=destination, mtime=value["source_date_epoch"]) as compressed:
                with tarfile.open(fileobj=compressed, mode="w", format=tarfile.USTAR_FORMAT) as target:
                    for name in entries:
                        directory = name in directories
                        item = tarfile.TarInfo(name)
                        item.mtime = value["source_date_epoch"]
                        item.mode = 0o755 if directory or rows.get(name, {}).get("executable") else 0o644
                        item.type = tarfile.DIRTYPE if directory else tarfile.REGTYPE
                        if directory:
                            target.addfile(item)
                        else:
                            item.size = (root / name).stat().st_size
                            with (root / name).open("rb") as source:
                                target.addfile(item, source)
        else:
            raise ValueError("unsupported archive format")
    require(read_limited(root / MANIFEST, MAX_MANIFEST) == manifest_raw, "manifest changed during packaging")
    verify_tree(root)
    side = {"schema": 1, "archive": output.name, "format": kind, "sha256": digest_file(output),
            "manifest_sha256": hashlib.sha256(manifest_raw).hexdigest(), "source_commit": value["source"]["commit"]}
    with sidecar.open("xb") as target:
        target.write(encoded(side))
    verify_archive(output)
    return side


def zip_bounds(path):
    # Our generated ZIPs have no comments. Check central-directory allocation before
    # ZipFile parses it, including ZIP64 offsets for archives larger than 4 GiB.
    with path.open("rb") as source:
        size = source.seek(0, 2)
        require(size >= 22, "truncated ZIP")
        source.seek(-22, 2)
        signature, disk, central_disk, disk_count, count, central_size, offset, comment = struct.unpack("<4s4H2IH", source.read(22))
        require(signature == b"PK\x05\x06" and disk == central_disk == comment == 0, "unsupported ZIP layout")
        if count == 65535 or central_size == 2**32 - 1 or offset == 2**32 - 1:
            require(size >= 98, "truncated ZIP64")
            source.seek(size - 42)
            locator, zip_disk, record_offset, disks = struct.unpack("<4sIQI", source.read(20))
            require(locator == b"PK\x06\x07" and zip_disk == 0 and disks == 1 and record_offset <= size - 98, "invalid ZIP64 locator")
            source.seek(record_offset)
            values = struct.unpack("<4sQ2H2I4Q", source.read(56))
            require(values[0] == b"PK\x06\x06" and 44 <= values[1] <= 4096 and values[4] == values[5] == 0, "invalid ZIP64 record")
            disk_count, count, central_size, offset = values[6:]
        require(count == disk_count and count <= MAX_FILES + 1 and central_size <= (MAX_FILES + 1) * 2048
                and offset + central_size <= size - 22, "ZIP central-directory bound exceeded")


class PackageTarInfo(tarfile.TarInfo):
    def _proc_member(self, container):
        # Refuse PAX/GNU extension payloads before tarfile can allocate their contents.
        require(self.type in (tarfile.REGTYPE, tarfile.AREGTYPE, tarfile.DIRTYPE) and 0 <= self.size <= MAX_FILE, "unsupported TAR header")
        container._glancelot_count = getattr(container, "_glancelot_count", 0) + 1
        container._glancelot_total = getattr(container, "_glancelot_total", 0) + self.size
        require(container._glancelot_count <= MAX_FILES + 1 and container._glancelot_total <= MAX_TOTAL + MAX_MANIFEST, "TAR allocation bound exceeded")
        safe_path(self.name.rstrip("/") if self.isdir() else self.name)
        return super()._proc_member(container)


def verify_archive(path):
    path = Path(path)
    side_path = path.with_name(path.name + ".json")
    side = json.loads(read_limited(side_path, 8192))
    require(set(side) == {"schema", "archive", "format", "sha256", "manifest_sha256", "source_commit"}
            and type(side["schema"]) is int and side["schema"] == 1 and side["archive"] == path.name, "invalid archive sidecar")
    require(side["sha256"] == digest_file(path), "archive digest differs")
    if side["format"] == "zip":
        zip_bounds(path)
        container = zipfile.ZipFile(path)
        entries = [(item.filename, item.is_dir(), item.file_size, item) for item in container.infolist()]
        require(all(stat.S_IFMT(item.external_attr >> 16) in (stat.S_IFDIR, stat.S_IFREG) for _, _, _, item in entries), "unsupported ZIP entry type")
        def open_entry(item):
            return container.open(item)
    elif side["format"] == "tar.gz":
        container = tarfile.open(path, "r:gz", tarinfo=PackageTarInfo)
        entries = []
        for item in container:
            require(len(entries) < MAX_FILES + 1, "archive entry bound exceeded")
            entries.append((item.name + "/" if item.isdir() else item.name, item.isdir(), item.size, item))
        require(all(item.isdir() or item.isfile() for _, _, _, item in entries), "unsupported TAR entry type")
        def open_entry(item):
            return container.extractfile(item)
    else:
        raise ValueError("unknown archive format")
    with container:
        require(len(entries) <= MAX_FILES + 1, "archive entry bound exceeded")
        records = {}
        seen = set()
        for name, directory, size, item in entries:
            name = safe_path(name[:-1] if directory else name)
            require(name.casefold() not in seen, "duplicate archive entry")
            seen.add(name.casefold())
            require(0 <= size <= MAX_FILE and (not directory or size == 0), "invalid archive payload size")
            records[name] = (directory, size, item)
        require(MANIFEST in records and records[MANIFEST][1] <= MAX_MANIFEST, "manifest absent or oversized")
        with open_entry(records[MANIFEST][2]) as source:
            raw = source.read(MAX_MANIFEST + 1)
        value = read_manifest(raw)
        require(hashlib.sha256(raw).hexdigest() == side["manifest_sha256"] and value["source"]["commit"] == side["source_commit"], "manifest identity differs")
        expected = {row["path"]: row for row in value["files"]}
        directories = set(value["directories"])
        require(set(records) == set(expected) | directories | {MANIFEST}, "archive inventory differs")
        for name, (directory, size, item) in records.items():
            require(directory == (name in directories), "archive entry kind differs")
            mode = 0o755 if directory or expected.get(name, {}).get("executable") else 0o644
            if side["format"] == "zip":
                timestamp = time.gmtime(value["source_date_epoch"] - value["source_date_epoch"] % 2)[:6]
                require((item.external_attr >> 16) & 0o7777 == mode and item.date_time == timestamp
                        and stat.S_IFMT(item.external_attr >> 16) == (stat.S_IFDIR if directory else stat.S_IFREG), "ZIP mode/timestamp differs")
            else:
                require(item.mode == mode and item.mtime == value["source_date_epoch"] and item.uid == item.gid == 0
                        and item.uname == item.gname == "", "TAR metadata differs")
            if name in expected:
                row = expected[name]
                require(size == row["size"], "archive file size differs")
                with open_entry(item) as source:
                    require(digest_stream(source, size)[1] == row["sha256"], "archive file digest differs")
        return value


def compare(first, second):
    left, right = verify_archive(first), verify_archive(second)
    a = {row["path"]: row for row in left["files"]}
    b = {row["path"]: row for row in right["files"]}
    return {"schema": 1, "archive_bytes_equal": digest_file(first) == digest_file(second),
            "payloads_equal": a == b,
            "executables_equal": {name: row for name, row in a.items() if row["executable"]} == {name: row for name, row in b.items() if row["executable"]},
            "metadata_equal": {key: val for key, val in left.items() if key != "files"} == {key: val for key, val in right.items() if key != "files"},
            "added": sorted(b.keys() - a.keys()), "removed": sorted(a.keys() - b.keys()),
            "changed": sorted(name for name in a.keys() & b.keys() if a[name] != b[name])}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    commands = parser.add_subparsers(dest="command", required=True)
    seal_parser = commands.add_parser("seal")
    seal_parser.add_argument("root", type=Path)
    provenance = seal_parser.add_mutually_exclusive_group(required=True)
    provenance.add_argument("--repo", type=Path)
    provenance.add_argument("--source-identity", type=Path, help="container source metadata with origin=declared")
    seal_parser.add_argument("--build-info", type=Path, required=True)
    seal_parser.add_argument("--target", choices=("windows-x86_64", "linux-x86_64"), required=True)
    seal_parser.add_argument("--epoch", type=int, required=True)
    seal_parser.add_argument("--executable", action="append", default=[])
    tree_parser = commands.add_parser("verify-tree"); tree_parser.add_argument("root", type=Path)
    pack_parser = commands.add_parser("archive"); pack_parser.add_argument("root", type=Path)
    pack_parser.add_argument("output", type=Path); pack_parser.add_argument("--format", choices=("zip", "tar.gz"), required=True)
    verify_parser = commands.add_parser("verify-archive"); verify_parser.add_argument("archive", type=Path)
    compare_parser = commands.add_parser("compare"); compare_parser.add_argument("first", type=Path); compare_parser.add_argument("second", type=Path)
    args = parser.parse_args()
    if args.command == "seal":
        seal(args.root, args.repo, json.loads(read_limited(args.build_info, 65536)), args.target, args.epoch, args.executable,
             json.loads(read_limited(args.source_identity, 8192)) if args.source_identity else None)
    elif args.command == "verify-tree":
        verify_tree(args.root)
    elif args.command == "archive":
        archive(args.root, args.output, args.format)
    elif args.command == "verify-archive":
        verify_archive(args.archive)
    else:
        result = compare(args.first, args.second)
        print(json.dumps(result, indent=2))
        return 0 if result["archive_bytes_equal"] else 1
    print("PASS", args.command)
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (ValueError, OSError, KeyError, TypeError, struct.error, subprocess.SubprocessError, json.JSONDecodeError, zipfile.BadZipFile, tarfile.TarError):
        raise SystemExit("Package validation failed; inspect generated staging/artifacts locally")
