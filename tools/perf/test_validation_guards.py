"""Integrity checks must still reject invalid input when Python assertions are disabled."""
import json
import subprocess
import sys
import tempfile
from pathlib import Path

script = Path(__file__).with_name("verify_fixtures.py")
with tempfile.TemporaryDirectory(prefix="glancelot-perf-verifier-") as directory:
    root = Path(directory)
    (root / "fixture-index.json").write_text(json.dumps({"schema": 2}), encoding="utf-8")
    result = subprocess.run([sys.executable, "-O", str(script), str(root)], capture_output=True, text=True, check=False)
    if result.returncode == 0 or "ValueError" not in result.stderr:
        raise RuntimeError("optimized Python accepted an invalid fixture schema")
print("PASS optimized-Python rejection of invalid fixture schema")
