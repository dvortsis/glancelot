"""Compare equivalent schema-1 local traces; exit 0 pass, 1 regression, 2 incomparable."""
import argparse
import json
import math
from pathlib import Path
from validate_report import STAGES, SPLITS, validate


def compare(baseline, changed, percent=10.0, thresholds=None, minimum=50, split="useful", noise_floor=0):
    validate(baseline)
    validate(changed)
    if not math.isfinite(percent) or percent < 0 or type(minimum) is not int or minimum < 1 or type(noise_floor) is not int or noise_floor < 0:
        raise ValueError("invalid threshold, sample minimum or noise floor")
    thresholds = thresholds or {}
    if not isinstance(thresholds, dict):
        raise ValueError("thresholds must be an object")
    if set(thresholds) - set(STAGES) or any(type(v) not in (int, float) or not math.isfinite(v) or v < 0 for v in thresholds.values()):
        raise ValueError("invalid per-stage thresholds")
    if split not in SPLITS:
        raise ValueError("invalid condition split")
    for report in (baseline, changed):
        if any(report["context"][key] in ("", "unspecified", "redacted", "qt-api-0") for key in ("fixture", "platform", "qt_backend")):
            raise ValueError("missing measured conditions")
        if report["context"]["build"].endswith("-unversioned"):
            raise ValueError("unversioned build")
    for condition in ("fixture", "platform", "qt_backend"):
        if baseline["context"][condition] != changed["context"][condition]:
            raise ValueError("different " + condition + ": reports are incomparable")
    if baseline["dropped"] or changed["dropped"]:
        raise ValueError("truncated traces are incomparable")
    rows = {}
    regression = False
    inconclusive = False
    measured = 0
    for stage in STAGES:
        before = baseline["stages"][stage]["splits"][split]
        after = changed["stages"][stage]["splits"][split]
        limit = thresholds.get(stage, percent)
        row = {"baseline_count": before["count"], "changed_count": after["count"], "threshold_percent": limit}
        if not before["count"] and not after["count"]:
            row["status"] = "no_samples"
        elif before["count"] != after["count"]:
            row["status"] = "sample_count_mismatch"
            inconclusive = True
        elif before["count"] < minimum:
            row["status"] = "insufficient_samples"
            inconclusive = True
        else:
            measured += 1
            old, new = before["p95_ns"], after["p95_ns"]
            delta = new - old
            change = 100.0 * delta / old if old else (0.0 if not new else None)
            failed = delta > noise_floor and (change is None or change > limit)
            row.update(baseline_p95_ns=old, changed_p95_ns=new, delta_ns=delta, change_percent=change,
                       status="regression" if failed else "pass")
            regression = regression or failed
        rows[stage] = row
    code = 1 if regression else 2 if inconclusive or not measured else 0
    return code, {"schema": 1, "split": split, "minimum_samples": minimum, "noise_floor_ns": noise_floor,
                  "status": "regression" if regression else "incomparable" if code == 2 else "pass", "stages": rows}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("baseline", type=Path)
    parser.add_argument("changed", type=Path)
    parser.add_argument("--percent", type=float, default=10)
    parser.add_argument("--thresholds", type=Path, help="JSON object mapping stage names to allowed p95 regression percentages")
    parser.add_argument("--min-samples", type=int, default=50)
    parser.add_argument("--split", choices=SPLITS, default="useful")
    parser.add_argument("--noise-floor-ns", type=int, default=0)
    args = parser.parse_args()
    try:
        code, report = compare(json.loads(args.baseline.read_text(encoding="utf-8")),
                               json.loads(args.changed.read_text(encoding="utf-8")), args.percent,
                               json.loads(args.thresholds.read_text(encoding="utf-8")) if args.thresholds else None,
                               args.min_samples, args.split, args.noise_floor_ns)
    except (ValueError, OSError, TypeError, KeyError) as error:
        # Do not print exception strings from filenames or JSON parse errors containing local paths.
        print(json.dumps({"schema": 1, "status": "incomparable", "error": type(error).__name__}))
        return 2
    print(json.dumps(report, indent=2, allow_nan=False))
    return code


if __name__ == "__main__":
    raise SystemExit(main())
