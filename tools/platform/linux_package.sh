#!/bin/sh
set -eu
repo=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
output= epoch= source_commit= source_dirty=
attempt=default
qt_image=${GLANCELOT_QT_IMAGE:-glancelot-qt:codex-perf-measure}
while [ "$#" -gt 0 ]; do
    [ "$#" -ge 2 ] || { echo 'Missing package option value' >&2; exit 2; }
    case "$1" in
        --output) output=$2 ;;
        --epoch) epoch=$2 ;;
        --source-commit) source_commit=$2 ;;
        --source-dirty) source_dirty=$2 ;;
        --attempt) attempt=$2 ;;
        *) echo 'Unknown package option' >&2; exit 2 ;;
    esac
    shift 2
done
[ -n "$output" ] && [ -n "$epoch" ] || { echo 'usage: linux_package.sh --output NEW_OUTPUT --epoch SOURCE_DATE_EPOCH [--source-commit SHA --source-dirty true|false]' >&2; exit 2; }
output=$(python3 - "$output" <<'PY'
from pathlib import Path
import sys
p=Path(sys.argv[1]).absolute()
if p.exists() or p.is_symlink(): raise SystemExit('Package output must be new')
print(p.resolve())
PY
)
if [ -z "$source_commit" ]; then
    source_commit=$(git -C "$repo" rev-parse HEAD)
    if [ -n "$(git -C "$repo" status --porcelain --untracked-files=normal)" ]; then source_dirty=true; else source_dirty=false; fi
fi
[ -n "$source_dirty" ] || { echo 'Explicit source identity needs dirty state' >&2; exit 2; }
python3 - "$source_commit" "$source_dirty" "$epoch" "$attempt" <<'PY'
import re,sys
if not re.fullmatch('[0-9a-f]{40}',sys.argv[1]) or sys.argv[2] not in ('true','false'):
    raise SystemExit('Invalid source identity')
if not re.fullmatch('[0-9]+',sys.argv[3]) or not 315532800<=int(sys.argv[3])<=4354819198:
    raise SystemExit('Invalid source epoch')
if not re.fullmatch('[A-Za-z0-9_.-]{1,64}',sys.argv[4]): raise SystemExit('Invalid build attempt identifier')
PY
toolchain_id=$(docker image inspect --format '{{.Id}}' "$qt_image")
docker build --file "$repo/tools/platform/Dockerfile.package" --target artifact \
    --build-arg "QT_IMAGE=$qt_image" --build-arg "TOOLCHAIN_IMAGE_ID=$toolchain_id" \
    --build-arg "SOURCE_COMMIT=$source_commit" --build-arg "SOURCE_DIRTY=$source_dirty" \
    --build-arg "SOURCE_DATE_EPOCH=$epoch" --build-arg "BUILD_ATTEMPT=$attempt" --output "type=local,dest=$output" "$repo"
python3 "$repo/tools/platform/artifacts.py" verify-archive "$output/Glancelot-linux-x86_64.tar.gz"
printf '%s\n' 'PASS Linux GUI archive, full build tests, independent clean-runtime dependencies and image smoke'
