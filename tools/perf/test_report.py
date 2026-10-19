"""Check the actual C++ reporter output with an independent JSON parser."""
import copy
import json
import subprocess
import sys
from pathlib import Path
from validate_report import validate

report = validate(json.loads(subprocess.check_output([str(Path(sys.argv[1]).resolve()), "--json"], text=True)))
lookup = report["stages"]["cache_lookup"]
assert lookup["all"] == {"count": 100, "min_ns": 1, "p50_ns": 50, "p95_ns": 95, "max_ns": 100, "total_ns": 5050, "bytes": 0}
assert lookup["splits"]["hit"]["count"] == lookup["splits"]["miss"]["count"] == 50
assert lookup["splits"]["cold"]["count"] == lookup["splits"]["warm"]["count"] == 50
assert report["stages"]["decode"]["splits"]["useful"]["count"] == 0
assert report["stages"]["decode"]["splits"]["stale"]["count"] == 1
assert report["dropped"] == 7
for field, value in (("schema", 2), ("clock", "wall_time"), ("dropped", -1)):
    invalid = copy.deepcopy(report)
    invalid[field] = value
    try:
        validate(invalid)
    except ValueError:
        continue
    raise AssertionError("accepted invalid " + field)
invalid = copy.deepcopy(report)
invalid["context"]["fixture"] = "C:/private/photo.png"
try:
    validate(invalid)
except ValueError:
    pass
else:
    raise AssertionError("accepted private fixture path")
print("PASS independent JSON parse, exact percentiles, hit/miss/cold/warm splits, stale exclusion and invalid-schema rejection")
