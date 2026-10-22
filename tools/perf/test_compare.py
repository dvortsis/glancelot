"""Exercise real reporter output, meaningful regressions and incomparable conditions."""
import copy
import json
import subprocess
import sys
from pathlib import Path
from compare import compare

baseline = json.loads(subprocess.check_output([str(Path(sys.argv[1]).resolve()), "--json"], text=True))
baseline.pop("spans")
baseline["dropped"] = 0  # synthetic complete input for comparison tests
# This known reporter fixture has 100 cache samples and one deliberately stale decode.
assert compare(baseline, baseline)[0] == 0
changed = copy.deepcopy(baseline)
for metric in (changed["stages"]["cache_lookup"]["all"], *changed["stages"]["cache_lookup"]["splits"].values()):
    for field in ("min_ns", "p50_ns", "p95_ns", "max_ns", "total_ns"):
        metric[field] *= 2
assert compare(baseline, changed)[0] == 1
assert compare(baseline, changed, thresholds={"cache_lookup": 100})[0] == 0
assert compare(baseline, changed, noise_floor=100)[0] == 0
assert compare(baseline, baseline, minimum=101)[0] == 2
for mutate in (lambda p: p["context"].update(fixture="different-v1"), lambda p: p.update(dropped=1)):
    changed = copy.deepcopy(baseline)
    mutate(changed)
    try:
        compare(baseline, changed)
    except ValueError:
        continue
    raise AssertionError("accepted incomparable conditions")
for kwargs in ({"percent": float("nan")}, {"thresholds": {"unknown": 10}}, {"minimum": 0}):
    try:
        compare(baseline, baseline, **kwargs)
    except ValueError:
        continue
    raise AssertionError("accepted invalid comparison options")
print("PASS identical, 100-percent regression, per-stage threshold, noise floor, insufficient samples, fixture mismatch, dropped spans and invalid options")
