#!/bin/sh
set -eu
script=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
exec "${PYTHON:-python3}" "$script/replay.py" "$@"
