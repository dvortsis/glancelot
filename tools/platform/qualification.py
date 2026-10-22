"""Build an evidence-bound matrix without certifying physical hardware from software smoke."""
import argparse
import json
import re
from pathlib import Path
from artifacts import MANIFEST, MAX_MANIFEST, compare, digest_file, encoded, read_limited, require, safe_path, verify_archive, verify_tree

PHYSICAL = {
    "CANVAS-UI.physical-dpi": "Operator must measure 100/125/150/200 percent and cross-monitor transitions on named displays.",
    "CANVAS-UI.physical-refresh": "Measure input/presentation traces on named 60/144 Hz displays; 240 Hz remains conditional.",
    "COLOR-01.icc-hdr": "Use a calibrated reference and named profiles/output modes; software RGB counts do not establish color accuracy.",
    "DECODE-01.R1": "Measure versioned 12/24/45/60 MP fixtures on the actual 16 GiB/NVMe/iGPU reference configuration.",
    "DECODE-01.R2": "Measure actual 8 GiB/HDD or explicitly throttled storage conditions.",
    "DECODE-01.R3": "Measure named discrete GPU and mixed-DPI/display conditions.",
    "DECODE-01.R4": "Measure controlled SMB storage, including disconnect/recovery and source integrity.",
    "PLATFORM.native-linux": "Record native X11/Wayland compositor/driver evidence; a container software-GL run is insufficient.",
    "PLATFORM.clean-windows": "Run without development tools on an independently provisioned Windows machine and record runtime prerequisites.",
}


def qualify(package, smoke_path, output, dependencies=None, repeat_path=None, reference="unqualified"):
    package, smoke_path, output = Path(package).resolve(), Path(smoke_path).resolve(), Path(output).absolute()
    require(not output.exists(), "qualification output already exists")
    manifest = verify_tree(package)
    smoke = json.loads(read_limited(smoke_path, MAX_MANIFEST))
    require(smoke["schema"] == 1 and smoke["passed"] is True and smoke["package_inventory_unchanged"] is True, "incomplete package smoke")
    require(smoke["source"] == manifest["source"] and smoke["target"] == manifest["target"]
            and smoke["manifest_sha256"] == digest_file(package / MANIFEST), "smoke evidence belongs to a different package")
    executables = {row["path"]: row["sha256"] for row in manifest["files"] if row["executable"]}
    require(smoke["executable_hashes"] == executables, "smoke executable identity differs")
    for key, dimensions in (("positive", [1024, 768]), ("jpeg", [64, 48])):
        capture = smoke[key]
        require(capture["qt"] == "6.8.3" and capture["version"] == manifest["build"]["app_version"]
                and capture["decoded_dimensions"] == dimensions and capture["first_image_frame_ms"] >= 0
                and capture["frames"] > 0 and capture["qt_modules_from_package"] is True
                and capture["development_qt_removed_from_path"] is True, "decode/module evidence incomplete")
    counts = smoke["positive"]["screenshot"]["source_color_pixels"]
    require(set(counts) == {"red", "green", "blue"} and all(type(n) is int and n >= 100 for n in counts.values()), "actual screenshot source colors absent")
    require(smoke["negative"]["missing_decoder_diagnosed"] is True, "missing-decoder failure evidence absent")
    for filename, key in (("generated-quadrants.png", "source_media_sha256_unchanged"), ("generated-smoke.jpg", "generated_jpeg_sha256_unchanged")):
        require(re.fullmatch(r"[0-9a-f]{64}", smoke[key]) is not None and digest_file(smoke_path.parent / filename) == smoke[key], "source media evidence differs")
    references = [{"role": "package_smoke", "name": smoke_path.name, "sha256": digest_file(smoke_path)}]
    rows = [{"id": name, "status": "passed", "evidence": ["package_smoke"]} for name in
            ("package.integrity", "package.qt-isolation", "decode.generated-png", "decode.generated-jpeg", "render.source-colors", "decoder.missing-sibling", "source.no-write")]
    if dependencies:
        dependencies = Path(dependencies).resolve()
        audit = json.loads(read_limited(dependencies, MAX_MANIFEST))
        require(audit["schema"] == 1 and audit["passed"] is True and audit["target"] == manifest["target"], "dependency evidence invalid")
        files = {row["path"]: row for row in manifest["files"]}
        require(audit["elf_files"], "dependency inventory empty")
        expected = set()
        for name, row in files.items():
            with (package / name).open("rb") as source:
                if source.read(4) == b"\x7fELF": expected.add(row["sha256"])
        reported = [row["sha256"] for row in audit["elf_files"]]
        require(len(reported) == len(set(reported)) and set(reported) == expected, "dependency audit omitted or duplicated ELF content")
        for row in audit["elf_files"]:
            name = safe_path(row["path"])
            require(name in files and row["sha256"] == files[name]["sha256"], "dependency audit refers to different binary")
            for dependency in row["dependencies"]:
                if dependency["soname"].startswith(("libQt6", "libicu")):
                    require(dependency["origin"] == "package", "dependency escaped package")
        references.append({"role": "dependencies", "name": dependencies.name, "sha256": digest_file(dependencies)})
        rows.append({"id": "package.elf-closure", "status": "passed", "evidence": ["dependencies"]})
    else:
        rows.append({"id": "package.elf-closure", "status": "unverified", "reason": "No independent ELF dependency report supplied."})
    if repeat_path:
        repeat_path = Path(repeat_path).resolve()
        saved = json.loads(read_limited(repeat_path, MAX_MANIFEST))
        require(saved["independent_builds"] == 2 and saved["source_commit"] == manifest["source"]["commit"], "repeat build conditions differ")
        artifacts = []
        require(len(saved["artifacts"]) == 2, "two repeat artifacts required")
        for artifact in saved["artifacts"]:
            path = (repeat_path.parent / safe_path(artifact["path"])).resolve()
            require(path.is_relative_to(repeat_path.parent) and digest_file(path) == artifact["sha256"], "repeat artifact identity differs")
            repeated = verify_archive(path)
            require(all(repeated[key] == manifest[key] for key in ("source", "target", "build", "source_date_epoch")), "repeat package build conditions differ")
            artifacts.append(path)
        actual = compare(*artifacts)
        require(all(saved[key] == actual[key] for key in actual), "comparison report disagrees with actual archives")
        references.append({"role": "repeat_builds", "name": repeat_path.name, "sha256": digest_file(repeat_path)})
        for key in ("executables_equal", "archive_bytes_equal"):
            rows.append({"id": "reproducibility." + key, "status": "passed" if actual[key] else "failed", "evidence": ["repeat_builds"]})
    else:
        rows.append({"id": "reproducibility.independent-builds", "status": "unverified", "reason": "Two independent compiled/package artifacts not supplied."})
    rows.extend({"id": key, "status": "unverified", "reason": reason} for key, reason in PHYSICAL.items())
    result = {"schema": 1, "source": manifest["source"], "target": manifest["target"],
        "manifest_sha256": digest_file(package / MANIFEST), "declared_reference_case": reference,
        "observations": {"graphics_api": smoke["positive"]["graphics_api"], "device_pixel_ratio": smoke["positive"]["device_pixel_ratio"],
                         "first_image_frame_ms": smoke["positive"]["first_image_frame_ms"], "environment": smoke.get("environment", {})},
        "evidence": references, "checks": rows,
        "physical_gates_closed": False}
    with output.open("xb") as destination: destination.write(encoded(result))
    print("PASS evidence-bound qualification matrix; physical/hardware gates remain unverified")
    return result


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--package", type=Path, required=True); parser.add_argument("--smoke", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True); parser.add_argument("--dependencies", type=Path)
    parser.add_argument("--repeat", type=Path)
    parser.add_argument("--case", choices=("unqualified", "W-NATIVE", "L-X11", "L-WAYLAND", "C-CPU", "C-GPU", "U-ARRAY", "U-POOL", "R1", "R2", "R3", "R4"), default="unqualified")
    args = parser.parse_args()
    qualify(args.package, args.smoke, args.output, args.dependencies, args.repeat, args.case)
