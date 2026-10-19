"""Independently verify generated fixture hashes, image dimensions and folder counts."""
import argparse
import hashlib
import json
import struct
from pathlib import Path, PurePosixPath


def require(condition, message="fixture integrity check failed"):
    if not condition:
        raise ValueError(message)


def sha256(path):
    with path.open("rb") as source:
        result = hashlib.sha256()
        for block in iter(lambda: source.read(1024 * 1024), b""):
            result.update(block)
        return result.hexdigest()


def dimensions(path):
    with path.open("rb") as source:
        header = source.read(24)
        if header.startswith(b"\x89PNG\r\n\x1a\n"):
            return struct.unpack(">II", header[16:24])
        source.seek(0)
        if source.read(2) != b"\xff\xd8":
            raise ValueError("unsupported generated image")
        for _ in range(1000):
            if source.read(1) != b"\xff":
                raise ValueError("invalid JPEG marker")
            marker = source.read(1)
            while marker == b"\xff":
                marker = source.read(1)
            if not marker:
                break
            code = marker[0]
            if code in (0xD8, 0xD9, 0x01) or 0xD0 <= code <= 0xD7:
                continue
            size = struct.unpack(">H", source.read(2))[0]
            if size < 2:
                raise ValueError("invalid JPEG segment")
            segment = source.read(size - 2)
            if code in (0xC0, 0xC1, 0xC2, 0xC3, 0xC5, 0xC6, 0xC7, 0xC9, 0xCA, 0xCB, 0xCD, 0xCE, 0xCF):
                height, width = struct.unpack(">HH", segment[1:5])
                return width, height
    raise ValueError("missing JPEG dimensions")


def verify(root, manifest):
    root = root.resolve()
    require((root / "fixture-index.json").stat().st_size <= 64 * 1024 * 1024)
    raw_index = (root / "fixture-index.json").read_bytes()
    index = json.loads(raw_index)
    spec = json.loads(manifest.read_text(encoding="utf-8"))
    require(index["schema"] == spec["schema"] == 1)
    require(index["manifest_sha256"] == sha256(manifest))
    require(index["profile"] in ("smoke", "full"))
    require(index["qt"] == "6.8.3")
    require(index["id"] == spec["id"] + ("-smoke" if index["profile"] == "smoke" else ""))
    require((root / ".glancelot-perf-generated").read_text().strip() == index["id"])
    require(len(index["files"]) <= 170000)
    smoke = index["profile"] == "smoke"
    expected = {"tile.png": (64, 48)}
    for item in spec["images"]:
        size = (64, 48) if smoke else (item["width"], item["height"])
        for format_name in spec["formats"]:
            require(format_name in ("jpeg", "png"))
            suffix = "jpg" if format_name == "jpeg" else "png"
            expected["images/" + item["id"] + "." + suffix] = size
    groups = []
    for requested in spec["mixed_counts"]:
        require(type(requested) is int and 0 < requested <= 100000)
        actual = min(100, requested) if smoke else requested
        require(len(expected) + actual <= 170000)
        folder = "mixed-%d" % requested
        groups.append({"path": folder, "requested_entries": requested, "actual_entries": actual})
        for i in range(actual):
            image = i % 10 == 0
            expected[folder + ("/image-%06d.png" if image else "/note-%06d.txt") % i] = (64, 48) if image else None
    require(index["mixed_folders"] == groups)
    depth, branches = spec["tree"]["depth"], spec["tree"]["branching"]
    require(type(depth) is int and 0 <= depth <= 8 and type(branches) is int and 1 <= branches <= 4)
    def nested(folder, remaining):
        require(len(expected) < 170000)
        expected[folder + "/image.png"] = (64, 48)
        if remaining:
            for child in range(branches):
                nested(folder + "/level-%d" % child, remaining - 1)
    nested("nested", min(2, depth) if smoke else depth)
    require(len(expected) <= 170000 and len(index["files"]) == len(expected))
    paths = set()
    counts = {}
    for entry in index["files"]:
        name = PurePosixPath(entry["path"])
        require(not name.is_absolute() and ".." not in name.parts and name.as_posix() not in paths)
        paths.add(name.as_posix())
        require(name.as_posix() in expected)
        path = root.joinpath(*name.parts)
        require(path.resolve().is_relative_to(root) and not path.is_symlink())
        require(sha256(path) == entry["sha256"])
        size = expected[name.as_posix()]
        if size is not None:
            require((entry.get("width"), entry.get("height")) == size and dimensions(path) == size)
        else:
            require("width" not in entry and "height" not in entry)
        counts[str(name.parent)] = counts.get(str(name.parent), 0) + 1
    require(paths == set(expected))
    expected_files = paths | {"fixture-index.json", ".glancelot-perf-generated"}
    expected_dirs = {str(parent) for name in paths for parent in PurePosixPath(name).parents if str(parent) != "."}
    actual_files, actual_dirs = set(), set()
    for path in root.rglob("*"):
        require(not path.is_symlink() and path.resolve().is_relative_to(root))
        relative = path.relative_to(root).as_posix()
        if path.is_file():
            actual_files.add(relative)
        else:
            require(path.is_dir())
            actual_dirs.add(relative)
        require(len(actual_files) <= len(expected_files) and len(actual_dirs) <= len(expected_dirs))
    require(actual_files == expected_files and actual_dirs == expected_dirs)
    for folder in index["mixed_folders"]:
        actual = min(100, folder["requested_entries"]) if index["profile"] == "smoke" else folder["requested_entries"]
        require(folder["actual_entries"] == actual == counts[folder["path"]])
        require(sum(1 for _ in (root / folder["path"]).iterdir()) == actual)
    digest = hashlib.sha256(raw_index).hexdigest()
    print("PASS fixture", index["id"], "files", len(paths), "profile", index["profile"], "index_sha256", digest)
    return index


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("root", type=Path)
    parser.add_argument("--manifest", type=Path, default=Path(__file__).with_name("fixtures-v1.json"))
    args = parser.parse_args()
    verify(args.root, args.manifest)


if __name__ == "__main__":
    main()
