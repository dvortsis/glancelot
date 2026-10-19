"""Package integrity and byte reproducibility contracts on disposable generated files."""
import copy
import gzip
import json
import os
import stat
import tarfile
import tempfile
import unittest
import zipfile
from pathlib import Path
import artifacts as subject


class ArtifactsTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory(prefix="glancelot-package-test-")
        self.addCleanup(self.temporary.cleanup)
        self.parent = Path(self.temporary.name)
        self.root = self.parent / "stage"
        (self.root / "bin").mkdir(parents=True)
        (self.root / "empty").mkdir()
        (self.root / subject.MARKER).write_bytes(b"1\n")
        (self.root / "bin" / "viewer").write_bytes(b"generated executable fixture\x00\xff")
        (self.root / "NOTICE.txt").write_bytes(b"Disposable test package notice\n")
        info = {"app_version": "0.1.0-test", "qt_version": "6.8.3", "compiler_id": "TestCompiler",
                "compiler_version": "1.0", "cmake_version": "4.4.4", "build_type": "Release",
                "cxx_standard": 20, "flags": ["-O2", "/DNDEBUG"]}
        self.manifest = subject.seal(self.root, None, info, "linux-x86_64", 1700000001, ["bin/viewer"],
                                     {"commit": "a" * 40, "dirty": False, "origin": "declared"})

    def test_repeat_archives_and_independent_verification(self):
        for kind in ("zip", "tar.gz"):
            first, second = self.parent / ("first." + kind), self.parent / ("second." + kind)
            subject.archive(self.root, first, kind)
            # Source filesystem times/modes are deliberately not archive metadata.
            os.utime(self.root / "NOTICE.txt", (1800000000, 1800000000))
            subject.archive(self.root, second, kind)
            self.assertEqual(subject.verify_archive(first), self.manifest)
            result = subject.compare(first, second)
            self.assertTrue(result["archive_bytes_equal"])
            self.assertTrue(result["metadata_equal"])
            self.assertEqual(result["changed"], [])
            with self.assertRaises(ValueError):
                subject.archive(self.root, first, kind)
        self.assertFalse((self.root / "first.zip").exists())
        with self.assertRaises(ValueError):
            subject.archive(self.root, self.root / "nested.zip", "zip")

    def test_tree_mutation_extra_missing_and_link_rejection(self):
        path = self.root / "NOTICE.txt"
        original = path.read_bytes()
        path.write_bytes(b"altered")
        with self.assertRaises(ValueError): subject.verify_tree(self.root)
        path.write_bytes(original)
        path.unlink()
        with self.assertRaises(ValueError): subject.verify_tree(self.root)
        path.write_bytes(original)
        extra = self.root / "unexpected"
        extra.mkdir()
        with self.assertRaises(ValueError): subject.verify_tree(self.root)
        extra.rmdir()
        subject.verify_tree(self.root)
        if os.name != "nt":
            extra.symlink_to(self.parent, target_is_directory=True)
            with self.assertRaises(ValueError): subject.verify_tree(self.root)
            extra.unlink()

    def test_comparison_reports_real_binary_difference(self):
        first, second = self.parent / "first.zip", self.parent / "second.zip"
        subject.archive(self.root, first, "zip")
        (self.root / subject.MANIFEST).unlink()
        (self.root / "bin" / "viewer").write_bytes(b"different generated executable bytes")
        subject.seal(self.root, None, self.manifest["build"], "linux-x86_64", 1700000001,
                     ["bin/viewer"], self.manifest["source"])
        subject.archive(self.root, second, "zip")
        result = subject.compare(first, second)
        self.assertFalse(result["archive_bytes_equal"])
        self.assertFalse(result["executables_equal"])
        self.assertFalse(result["payloads_equal"])
        self.assertTrue(result["metadata_equal"])
        self.assertEqual(result["changed"], ["bin/viewer"])

    def test_manifest_guards(self):
        for mutate in (lambda d: d["files"].append(copy.deepcopy(d["files"][0])),
                       lambda d: d["files"][0].update(path="../outside"),
                       lambda d: d["files"][0].update(path="C:/private"),
                       lambda d: d["directories"].append("BIN"),
                       lambda d: d["build"].update(compiler_id="C:/private/tool"),
                       lambda d: d["build"].update(qt_version="6.9.0"),
                       lambda d: d["source"].update(commit="unversioned"),
                       lambda d: d.update(source_date_epoch=-1)):
            invalid = copy.deepcopy(self.manifest); mutate(invalid)
            with self.assertRaises(ValueError): subject.validate_manifest(invalid)

    def test_archive_tampering(self):
        original = self.parent / "original.zip"
        subject.archive(self.root, original, "zip")
        cases = ("duplicate", "traversal", "payload", "symlink", "mode", "missing", "timestamp")
        for case in cases:
            changed = self.parent / (case + ".zip")
            with zipfile.ZipFile(original) as source, zipfile.ZipFile(changed, "w") as target:
                for item in source.infolist():
                    if case == "missing" and item.filename == "NOTICE.txt":
                        continue
                    data = source.read(item)
                    if item.filename == "bin/viewer":
                        if case == "payload": data = bytes([data[0] ^ 1]) + data[1:]
                        elif case == "symlink": item.external_attr = (stat.S_IFLNK | 0o755) << 16
                        elif case == "mode": item.external_attr = (stat.S_IFREG | 0o644) << 16
                        elif case == "timestamp": item.date_time = (2025, 1, 1, 0, 0, 0)
                    target.writestr(item, data)
                if case in ("duplicate", "traversal"):
                    item = zipfile.ZipInfo("NOTICE.txt" if case == "duplicate" else "../outside")
                    item.external_attr = (stat.S_IFREG | 0o644) << 16
                    target.writestr(item, b"unexpected")
            side = json.loads(original.with_name(original.name + ".json").read_bytes())
            side.update(archive=changed.name, sha256=subject.digest_file(changed))
            changed.with_name(changed.name + ".json").write_bytes(subject.encoded(side))
            with self.subTest(case=case), self.assertRaises(ValueError):
                subject.verify_archive(changed)
        side_path = original.with_name(original.name + ".json")
        side = json.loads(side_path.read_bytes()); side["sha256"] = "0" * 64
        side_path.write_bytes(subject.encoded(side))
        with self.assertRaises(ValueError): subject.verify_archive(original)

    def test_tar_extension_and_size_bounds(self):
        for name, kind, size in (("extension", tarfile.XHDTYPE, 0),
                                 ("link", tarfile.SYMTYPE, 0),
                                 ("oversized", tarfile.REGTYPE, subject.MAX_FILE + 1)):
            path = self.parent / (name + ".tar.gz")
            header = tarfile.TarInfo("unexpected")
            header.type, header.size = kind, size
            with gzip.open(path, "wb") as target:
                target.write(header.tobuf()); target.write(b"\0" * 1024)
            side = {"schema": 1, "archive": path.name, "format": "tar.gz",
                    "sha256": subject.digest_file(path), "manifest_sha256": "0" * 64,
                    "source_commit": "a" * 40}
            path.with_name(path.name + ".json").write_bytes(subject.encoded(side))
            with self.subTest(name=name), self.assertRaises(ValueError):
                subject.verify_archive(path)


if __name__ == "__main__":
    unittest.main()
