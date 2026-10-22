"""Replay existing viewer workflows on verified generated media; never use user photos."""
import argparse
import hashlib
import json
import os
import re
import subprocess
import sys
from pathlib import Path
from validate_report import validate
from aggregate import aggregate
from verify_fixtures import verify


def digest(path):
    with path.open("rb") as source:
        value = hashlib.sha256()
        for block in iter(lambda: source.read(1024 * 1024), b""):
            value.update(block)
        return value.hexdigest()


def provenance(exe, declared_commit=None):
    if declared_commit is not None:
        if re.fullmatch(r"[0-9a-f]{40}", declared_commit) is None:
            raise ValueError("declared source commit must be 40 lowercase hexadecimal digits")
        return "declared-" + declared_commit + "-exe-" + digest(exe)[:16]
    repo = Path(__file__).resolve().parents[2]
    commit = subprocess.check_output(["git", "-C", str(repo), "rev-parse", "HEAD"], text=True).strip()
    dirty = subprocess.run(["git", "-C", str(repo), "diff", "--quiet", "HEAD"], check=False).returncode
    if dirty not in (0, 1):
        raise ValueError("cannot inspect source provenance")
    return commit + ("-dirty" if dirty else "") + "-exe-" + digest(exe)[:16]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--exe", type=Path, required=True)
    parser.add_argument("--fixtures", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--source-commit", help="explicit declared source commit for packaged/container runs without Git; binary SHA is always measured")
    parser.add_argument("--runs", type=int, default=1)
    parser.add_argument("--steps", type=int, default=5)
    parser.add_argument("--cycles", type=int, default=2)
    parser.add_argument("--require-render", action="store_true", help="require native/RHI upload, render and startup spans")
    args = parser.parse_args()
    if not 1 <= args.runs <= 50 or not 1 <= args.steps <= 200 or not 1 <= args.cycles <= 20:
        parser.error("runs, steps or cycles outside replay bounds")
    exe, root, output = args.exe.resolve(), args.fixtures.resolve(), args.output.resolve()
    if output.is_relative_to(root) or (output.exists() and any(output.iterdir())):
        parser.error("output must be new/empty and outside the fixture root")
    fixture = verify(root, Path(__file__).with_name("fixtures-v1.json"))
    initial = root / fixture["mixed_folders"][0]["path"] / "image-000000.png"
    if not initial.is_file():
        raise ValueError("initial generated image missing")
    fixture_hash = digest(root / "fixture-index.json")
    fixture_id = fixture["id"] + "-" + fixture_hash[:16] + "-standard-v1-s%d-c%d-1180x780-slate-dark" % (args.steps, args.cycles)
    build = provenance(exe, args.source_commit)
    output.mkdir(parents=True, exist_ok=True)
    (output / ".glancelot-perf-replay-generated").write_text("standard-v1\n", encoding="utf-8")
    fault = output / "corrupt.png"
    fault.write_bytes(b"Glancelot deliberately invalid PNG fixture v1\n")
    verdicts = []
    for run in range(args.runs):
        folder = output / ("run-%03d" % run)
        folder.mkdir()
        env = os.environ.copy()
        env.update(GLANCELOT_CONFIG_DIR=str(folder / "config"), GLANCELOT_PERF_REPLAY="standard-v1",
                   GLANCELOT_PERF_REPLAY_STEPS=str(args.steps), GLANCELOT_PERF_REPLAY_CYCLES=str(args.cycles),
                   GLANCELOT_PERF_REPLAY_RESULT=str(folder / "verdict.json"), GLANCELOT_PERF_REPLAY_FAILURE=str(fault),
                   GLANCELOT_PERF_FIXTURE_ID=fixture_id, GLANCELOT_PERF_BUILD=build)
        with (folder / "viewer.log").open("wb") as log:
            process = subprocess.run([str(exe), "--no-resume", "--perf-report", str(folder / "perf.json"), "--theme", "slate-ice", "--theme-mode", "dark", "--window-size", "1180x780", str(initial)],
                                     env=env, stdout=log, stderr=subprocess.STDOUT, timeout=330, check=False)
        if process.returncode != 0:
            raise RuntimeError("replay process failed; inspect the local run log and verdict")
        verdict = json.loads((folder / "verdict.json").read_text(encoding="utf-8"))
        report = validate(json.loads((folder / "perf.json").read_text(encoding="utf-8")))
        if not verdict["passed"] or verdict["reason"] != "ok" or report["dropped"]:
            raise RuntimeError("replay failed or trace truncated")
        actions = verdict["actions"]
        expected_steps = min(args.steps, fixture["mixed_folders"][0]["actual_entries"] // 10 - 1) * args.cycles
        for name in ("next", "previous"):
            if actions.get(name) != expected_steps:
                raise RuntimeError("navigation action count mismatch")
        for name, expected in {"open": 1, "thumbnail_probe": 2, "raise_root": 1, "next_folder": 1,
                               "previous_folder": 1, "grid": 1, "grid_scroll": 6, "combined": 1,
                               "combined_selection": 1, "preview": 1, "burst_open": 2,
                               "corrupt_open": 1, "recovery_open": 1}.items():
            if actions.get(name) != expected:
                raise RuntimeError("replay workflow coverage incomplete: " + name)
        stages = report["stages"]
        if not stages["cache_lookup"]["splits"]["hit"]["count"] or not stages["cache_lookup"]["splits"]["miss"]["count"]:
            raise RuntimeError("genuine cache hit/miss coverage missing")
        if not stages["decode"]["splits"]["cold"]["count"] or not stages["decode"]["splits"]["warm"]["count"]:
            raise RuntimeError("process-first/warm decode coverage missing")
        for flag in ("cancelled", "stale", "failed"):
            if not sum(stage["splits"][flag]["count"] for stage in stages.values()):
                raise RuntimeError("fault/cancellation coverage missing: " + flag)
        if not stages["publish"]["all"]["count"] or stages["mutation"]["all"]["count"]:
            raise RuntimeError("grid publication missing or mutation unexpectedly reached")
        if args.require_render and any(not stages[stage]["all"]["count"] for stage in ("upload", "present", "startup")):
            raise RuntimeError("native render coverage missing")
        verdicts.append({"run": run, "passed": True, "actions": actions, "elapsed_ms": verdict["elapsed_ms"]})
        print("PASS standard-v1 run", run, "next/previous", expected_steps, "cache hit/miss, grid, combined, cancellation, corrupt decode and recovery", flush=True)
    verify(root, Path(__file__).with_name("fixtures-v1.json"))
    if digest(root / "fixture-index.json") != fixture_hash:
        raise RuntimeError("fixture manifest changed during replay")
    report, aggregation = aggregate([output / ("run-%03d" % run) / "perf.json" for run in range(args.runs)])
    (output / "aggregate.json").write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    (output / "aggregate.aggregation.json").write_text(json.dumps(aggregation, indent=2) + "\n", encoding="utf-8")
    summary = {"schema": 1, "scenario": "standard-v1", "fixture": fixture_id, "build": build,
               "require_render": args.require_render, "source_hashes_unchanged": True, "runs": verdicts}
    (output / "replay-summary.json").write_text(json.dumps(summary, indent=2) + "\n", encoding="utf-8")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
