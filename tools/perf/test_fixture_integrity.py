"""A generated index must describe the complete frozen dataset, not a valid subset."""
import copy
import json
import shutil
import sys
import tempfile
from pathlib import Path
from verify_fixtures import verify

source = Path(sys.argv[1]).resolve()
manifest = Path(__file__).with_name("fixtures-v1.json")
original = json.loads((source / "fixture-index.json").read_text())
if original["profile"] != "smoke":
    raise ValueError("use generated smoke fixtures for this bounded corruption test")
with tempfile.TemporaryDirectory(prefix="glancelot-fixture-integrity-") as directory:
    root = Path(directory) / "fixtures"
    shutil.copytree(source, root)
    index_path = root / "fixture-index.json"
    verify(root, manifest)

    def rejected(index):
        index_path.write_text(json.dumps(index), encoding="utf-8")
        try:
            verify(root, manifest)
        except ValueError:
            return
        raise RuntimeError("incomplete or altered fixture dataset was accepted")

    index = copy.deepcopy(original)
    index["mixed_folders"].pop()
    rejected(index)
    index = copy.deepcopy(original)
    index["files"] = [row for row in index["files"] if row["path"] != "images/24mp.png"]
    rejected(index)
    index = copy.deepcopy(original)
    index["files"] = [row for row in index["files"] if row["path"] != "nested/level-0/image.png"]
    rejected(index)
    index = copy.deepcopy(original)
    index["mixed_folders"][0]["requested_entries"] = 100
    rejected(index)
    index = copy.deepcopy(original)
    next(row for row in index["files"] if row["path"] == "tile.png")["width"] = 65
    rejected(index)
    index = copy.deepcopy(original)
    index["qt"] = "unverified"
    rejected(index)
    index_path.write_text(json.dumps(original), encoding="utf-8")
    extra = root / "images" / "unexpected.txt"
    extra.write_text("unexpected generated-fixture entry")
    rejected(original)
    extra.unlink()
    extra_dir = root / "unexpected-empty-folder"
    extra_dir.mkdir()
    rejected(original)
    extra_dir.rmdir()
    verify(root, manifest)
print("PASS complete manifest coverage, dimensions, toolchain and unexpected-file/directory rejection")
