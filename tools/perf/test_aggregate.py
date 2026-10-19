"""Validate aggregation against exact raw samples and reject incompatible/truncated inputs."""
import copy
import json
import subprocess
import sys
import tempfile
from pathlib import Path
from aggregate import aggregate

sample = json.loads(subprocess.check_output([str(Path(sys.argv[1]).resolve()), "--json"], text=True))
sample["dropped"] = 0  # synthetic complete capture with known raw distributions
with tempfile.TemporaryDirectory() as directory:
    first, second = Path(directory) / "first.json", Path(directory) / "second.json"
    first.write_text(json.dumps(sample))
    second.write_text(json.dumps(sample))
    report, meta = aggregate([first, second])
    metrics = report["stages"]["cache_lookup"]["all"]
    assert metrics["count"] == 200 and metrics["p50_ns"] == 50 and metrics["p95_ns"] == 95 and metrics["total_ns"] == 10100
    assert meta["capture_count"] == 2 and meta["span_count"] == 202 and "spans" not in report
    for mutate in (lambda p: p.pop("spans"), lambda p: p.update(dropped=1),
                   lambda p: p["context"].update(fixture="different")):
        bad = copy.deepcopy(sample)
        mutate(bad)
        second.write_text(json.dumps(bad))
        try:
            aggregate([first, second])
        except ValueError:
            continue
        raise AssertionError("accepted incompatible aggregation")
print("PASS exact raw-distribution aggregation; no percentile averaging or cross-process timeline; incompatible/truncated capture rejection")
