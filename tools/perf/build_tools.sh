#!/bin/sh
set -eu
repo=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
build=${1:-"$repo/build-codex-perf-measure"}
cmake -S "$repo/tools/perf" -B "$build/perf-tools" -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build "$build/perf-tools" --parallel 2
