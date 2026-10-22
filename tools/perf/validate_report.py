"""Validate a local PERF-MEASURE schema-1 report without third-party packages."""
import argparse
import json
import re
from pathlib import Path

STAGES = ("enumerate", "sort", "publish", "thumbnail_queue", "thumbnail_decode",
          "decode_queue", "decode", "upload", "present", "cache_lookup", "mutation", "startup")
SPLITS = ("hit", "miss", "cold", "warm", "cancelled", "failed", "stale", "useful")
METRICS = ("count", "min_ns", "p50_ns", "p95_ns", "max_ns", "total_ns", "bytes")


def require(condition, message):
    if not condition:
        raise ValueError(message)


def integer(value):
    return type(value) is int and value >= 0


def metrics(value):
    require(isinstance(value, dict) and set(value) == set(METRICS), "invalid metric keys")
    require(all(integer(value[key]) for key in METRICS), "metrics must be nonnegative integers")
    require(value["min_ns"] <= value["p50_ns"] <= value["p95_ns"] <= value["max_ns"], "unordered percentiles")
    if not value["count"]:
        require(not any(value.values()), "empty sample has nonzero metrics")
    else:
        require(value["total_ns"] >= value["max_ns"], "total less than maximum")


def raw_metrics(samples):
    if not samples:
        return dict.fromkeys(METRICS, 0)
    durations = sorted(end - start for start, end, _ in samples)
    return {"count": len(samples), "min_ns": durations[0],
            "p50_ns": durations[(len(samples) + 1) // 2 - 1],
            "p95_ns": durations[(95 * len(samples) + 99) // 100 - 1],
            "max_ns": durations[-1], "total_ns": min(2**64 - 1, sum(durations)),
            "bytes": min(2**64 - 1, sum(size for _, _, size in samples))}


def validate(report):
    require(isinstance(report, dict), "report must be an object")
    require(set(report) in ({"schema", "clock", "percentile", "context", "dropped", "stages"},
                           {"schema", "clock", "percentile", "context", "dropped", "stages", "spans"}), "invalid report keys")
    require(type(report["schema"]) is int and report["schema"] == 1, "unsupported schema")
    require(report["clock"] == "steady_process_relative_ns", "unsupported clock")
    require(report["percentile"] == "nearest_rank", "unsupported percentile method")
    require(integer(report["dropped"]), "invalid dropped count")
    require(isinstance(report["context"], dict) and set(report["context"]) == {"build", "platform", "qt_backend", "fixture"}, "invalid context")
    for value in report["context"].values():
        require(isinstance(value, str) and len(value) <= 256 and re.fullmatch(r"[A-Za-z0-9_.+ -]*", value) is not None, "path-like or invalid context")
    require(isinstance(report["stages"], dict) and set(report["stages"]) == set(STAGES), "invalid stages")
    for name in STAGES:
        stage = report["stages"][name]
        require(isinstance(stage, dict) and set(stage) == {"all", "splits"}, "invalid stage fields")
        metrics(stage["all"])
        require(isinstance(stage["splits"], dict) and set(stage["splits"]) == set(SPLITS), "invalid split fields")
        for split in stage["splits"].values():
            metrics(split)
            require(split["count"] <= stage["all"]["count"], "split exceeds all samples")
        for first, second in (("hit", "miss"), ("cold", "warm")):
            require(stage["splits"][first]["count"] + stage["splits"][second]["count"] == stage["all"]["count"], "incomplete split")
    if "spans" in report:
        require(isinstance(report["spans"], list), "invalid raw spans")
        groups = {name: {"all": [], **{split: [] for split in SPLITS}} for name in STAGES}
        for span in report["spans"]:
            require(isinstance(span, dict) and set(span) == {"stage", "flags", "thread", "generation", "start_ns", "end_ns", "bytes"}, "invalid span fields")
            require(span["stage"] in STAGES, "unknown raw stage")
            require(all(integer(v) for k, v in span.items() if k != "stage"), "invalid raw numeric value")
            require(span["end_ns"] >= span["start_ns"] and span["flags"] <= 31, "invalid span duration or flags")
            selected = groups[span["stage"]]
            sample = (span["start_ns"], span["end_ns"], span["bytes"])
            selected["all"].append(sample)
            flags = span["flags"]
            selected["hit" if flags & 1 else "miss"].append(sample)
            selected["cold" if flags & 8 else "warm"].append(sample)
            for flag, key in ((2, "cancelled"), (4, "failed"), (16, "stale")):
                if flags & flag:
                    selected[key].append(sample)
            if not flags & (2 | 4 | 16):
                selected["useful"].append(sample)
        for name, group in groups.items():
            stage = report["stages"][name]
            require(stage["all"] == raw_metrics(group["all"]), "raw/aggregate metrics mismatch")
            for split in SPLITS:
                require(stage["splits"][split] == raw_metrics(group[split]), "raw/split metrics mismatch")
    return report


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("report", type=Path)
    args = parser.parse_args()
    validate(json.loads(args.report.read_text(encoding="utf-8")))
    print("PASS schema-1 metrics, splits, redacted context and raw/aggregate counts")


if __name__ == "__main__":
    main()
