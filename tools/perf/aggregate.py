"""Aggregate raw duration samples; never merge cross-process timestamp origins or percentiles."""
import argparse
import hashlib
import json
from pathlib import Path
from validate_report import STAGES, SPLITS, validate


def metrics(samples):
    if not samples:
        return dict.fromkeys(("count", "min_ns", "p50_ns", "p95_ns", "max_ns", "total_ns", "bytes"), 0)
    values = sorted(sample[0] for sample in samples)
    return {"count": len(values), "min_ns": values[0], "p50_ns": values[(len(values) * 50 + 99) // 100 - 1],
            "p95_ns": values[(len(values) * 95 + 99) // 100 - 1], "max_ns": values[-1],
            "total_ns": min(2**64 - 1, sum(values)), "bytes": min(2**64 - 1, sum(sample[1] for sample in samples))}


def aggregate(paths):
    if not 1 <= len(paths) <= 200:
        raise ValueError("aggregation requires 1..200 captures")
    groups = {name: {"all": [], **{split: [] for split in SPLITS}} for name in STAGES}
    context = None
    hashes = []
    count = 0
    for path in paths:
        path = Path(path)
        if path.stat().st_size > 64 * 1024 * 1024:
            raise ValueError("capture exceeds bounded report size")
        raw = path.read_bytes()
        report = validate(json.loads(raw))
        if report["dropped"] or "spans" not in report:
            raise ValueError("raw, untruncated samples required; percentiles cannot be averaged")
        if context is None:
            context = report["context"]
        elif context != report["context"]:
            raise ValueError("capture build/conditions differ")
        hashes.append(hashlib.sha256(raw).hexdigest())
        count += len(report["spans"])
        if count > 2000000:
            raise ValueError("aggregation span bound exceeded")
        for span in report["spans"]:
            sample = (span["end_ns"] - span["start_ns"], span["bytes"])
            selected = groups[span["stage"]]
            selected["all"].append(sample)
            flags = span["flags"]
            selected["hit" if flags & 1 else "miss"].append(sample)
            selected["cold" if flags & 8 else "warm"].append(sample)
            for flag, key in ((2, "cancelled"), (4, "failed"), (16, "stale")):
                if flags & flag:
                    selected[key].append(sample)
            if not flags & (2 | 4 | 16):
                selected["useful"].append(sample)
    result = {"schema": 1, "clock": "steady_process_relative_ns", "percentile": "nearest_rank",
              "context": context, "dropped": 0,
              "stages": {name: {"all": metrics(group["all"]),
                                  "splits": {split: metrics(group[split]) for split in SPLITS}}
                         for name, group in groups.items()}}
    validate(result)
    metadata = {"schema": 1, "capture_count": len(paths), "span_count": count, "capture_sha256": hashes,
                "method": "raw_duration_samples_no_cross_process_timestamps"}
    return result, metadata


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("reports", nargs="+", type=Path)
    parser.add_argument("--output", required=True, type=Path)
    args = parser.parse_args()
    report, metadata = aggregate(args.reports)
    with args.output.open("x", encoding="utf-8") as output:
        json.dump(report, output, indent=2)
    with args.output.with_suffix(".aggregation.json").open("x", encoding="utf-8") as output:
        json.dump(metadata, output, indent=2)
    print("PASS raw-sample aggregation captures", metadata["capture_count"], "spans", metadata["span_count"])


if __name__ == "__main__":
    main()
