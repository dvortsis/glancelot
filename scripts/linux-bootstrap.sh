#!/usr/bin/env bash
# One-command Linux setup for Glancelot (Ubuntu 24.04): system packages, Qt 6.8.3,
# configure, build and CTest. Mirrors the Dockerfile `qt` and `build` stages so a
# cloud or desktop session gets the same toolkit and image formats as the gate.
#
# Idempotent: installed packages and an existing Qt are detected and skipped, and
# CMake reuses the build directory. Only apt runs as root (through sudo when needed).
# See BUILDING.md (Linux) for options and docs/CI-GATE-RUNNER.md for the gate.
set -euo pipefail

QT_VERSION=6.8.3
QT_ARCH=linux_gcc_64
AQT_VERSION=3.3.0
# Keep in step with the Dockerfile `qt` stage (aqt module and archive arguments).
AQT_MODULES=(qtshadertools qtimageformats)
AQT_ARCHIVES=(qtbase qtdeclarative qtshadertools qtimageformats)
# Dockerfile `qt` stage, `build` stage (xvfb, libgl1-mesa-dri) and cmake/RecoveryTests.cmake (python3, Xvfb).
APT_PACKAGES=(
  ca-certificates python3 python3-venv build-essential cmake ninja-build
  libgl1-mesa-dev libgl1-mesa-dri libxkbcommon-dev libfontconfig1 libdbus-1-3
  libxcb-cursor0 libxcb-icccm4 libxcb-keysyms1 libxcb-shape0 libxcb-xinerama0
  libxcb-image0 libxcb-render-util0 libxkbcommon-x11-0 libegl1 libglib2.0-0t64 xvfb
)

ROOT=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
QT_PREFIX=${GLANCELOT_QT_PREFIX:-$HOME/Qt}
AQT_VENV=${GLANCELOT_AQT_VENV:-$HOME/.cache/glancelot/aqt-$AQT_VERSION}
QT_BASE=${GLANCELOT_QT_BASE:-}
BUILD_DIR=${GLANCELOT_BUILD_DIR:-$ROOT/build}
JOBS=${GLANCELOT_JOBS:-4}
BUILD_TYPE=Release
RUN_APT=1
RUN_BUILD=1
RUN_TESTS=1
CTEST_ARGS=()

usage() {
  cat <<EOF
Usage: scripts/linux-bootstrap.sh [options] [-- extra ctest arguments]

  --qt-prefix DIR   Qt install root (default \$GLANCELOT_QT_PREFIX or ~/Qt; Qt lands in DIR/$QT_VERSION/gcc_64)
  --qt-base URL     Qt mirror passed to aqt --base (default \$GLANCELOT_QT_BASE, else aqt's default)
  --build-dir DIR   CMake build directory (default \$GLANCELOT_BUILD_DIR or ./build)
  --jobs N          parallel compile jobs (default \$GLANCELOT_JOBS or 4)
  --debug           configure a Debug build instead of Release
  --skip-apt        do not install system packages
  --qt-only         install packages and Qt, then stop
  --no-tests        configure and build, skip CTest
  -h, --help        show this help

Tests run offscreen with 'ctest -L fast' when that label exists, else the full suite.
Arguments after -- are passed to ctest (for example: -- -R animation).
EOF
}

# An option that takes a value needs one, and it must not be the next option.
need_value() {
  if (($# < 2)) || [[ -z $2 || $2 == --* ]]; then
    echo "$1 needs a value" >&2
    usage >&2
    exit 2
  fi
}

while (($#)); do
  case $1 in
    --qt-prefix) need_value "$@"; QT_PREFIX=$2; shift 2 ;;
    --qt-base) need_value "$@"; QT_BASE=$2; shift 2 ;;
    --build-dir) need_value "$@"; BUILD_DIR=$2; shift 2 ;;
    --jobs) need_value "$@"; JOBS=$2; shift 2 ;;
    --debug) BUILD_TYPE=Debug; shift ;;
    --skip-apt) RUN_APT=0; shift ;;
    --qt-only) RUN_BUILD=0; RUN_TESTS=0; shift ;;
    --no-tests) RUN_TESTS=0; shift ;;
    -h|--help) usage; exit 0 ;;
    --) shift; CTEST_ARGS=("$@"); break ;;
    *) echo "unknown option: $1" >&2; usage >&2; exit 2 ;;
  esac
done

# aqt runs from inside the Qt prefix (see install_qt), so every path must be absolute
# before anything changes directory; relative values resolve against the caller's directory.
QT_PREFIX=$(realpath -m -- "$QT_PREFIX")
BUILD_DIR=$(realpath -m -- "$BUILD_DIR")
AQT_VENV=$(realpath -m -- "$AQT_VENV")
QT_DIR=$QT_PREFIX/$QT_VERSION/gcc_64
step() { printf '\n==> %s\n' "$*"; }

install_packages() {
  local missing=() pkg
  for pkg in "${APT_PACKAGES[@]}"; do
    # Here-strings, not pipes, into grep -q: under pipefail an early grep exit can fail the writer (141).
    grep -q 'install ok installed' <<<"$(dpkg-query -W -f='${Status}' "$pkg" 2>/dev/null)" || missing+=("$pkg")
  done
  if ((${#missing[@]} == 0)); then
    echo "all ${#APT_PACKAGES[@]} packages already installed"
    return
  fi
  echo "installing: ${missing[*]}"
  local sudo=()
  ((EUID == 0)) || sudo=(sudo)
  "${sudo[@]}" env DEBIAN_FRONTEND=noninteractive apt-get update
  "${sudo[@]}" env DEBIAN_FRONTEND=noninteractive apt-get install -y --no-install-recommends "${missing[@]}"
}

# Files whose presence proves each requested Qt part is installed.
qt_complete() {
  [[ -f $QT_DIR/lib/cmake/Qt6/Qt6Config.cmake \
  && -f $QT_DIR/lib/cmake/Qt6Quick/Qt6QuickConfig.cmake \
  && -f $QT_DIR/lib/cmake/Qt6ShaderTools/Qt6ShaderToolsConfig.cmake \
  && -f $QT_DIR/plugins/imageformats/libqwebp.so \
  && -f $QT_DIR/plugins/imageformats/libqtiff.so \
  && -f $QT_DIR/plugins/imageformats/libqtga.so ]] \
  && compgen -G "$QT_DIR/lib/libicui18n.so.*" >/dev/null
}

# The venv is usable when aqt runs and pip metadata names the pinned aqtinstall version
# (aqt prints its own version through logging on stderr, so its text is not checked).
aqt_venv_ok() {
  [[ -x $AQT_VENV/bin/aqt && -x $AQT_VENV/bin/python ]] || return 1
  "$AQT_VENV/bin/aqt" version >/dev/null 2>&1 || return 1
  local installed
  installed=$("$AQT_VENV/bin/python" -c 'import importlib.metadata as m; print(m.version("aqtinstall"))' 2>/dev/null) || return 1
  [[ $installed == "$AQT_VERSION" ]]
}

install_qt() {
  if qt_complete; then
    echo "Qt $QT_VERSION with ShaderTools, image formats and ICU already in $QT_DIR"
    return
  fi
  if ! aqt_venv_ok; then
    python3 -m venv "$AQT_VENV"
    "$AQT_VENV/bin/pip" install --quiet --disable-pip-version-check "aqtinstall==$AQT_VERSION"
  fi
  mkdir -p "$QT_PREFIX"
  local aqt=("$AQT_VENV/bin/aqt" install-qt linux desktop "$QT_VERSION" "$QT_ARCH" -O "$QT_PREFIX")
  [[ -z $QT_BASE ]] || aqt+=(--base "$QT_BASE")
  # aqt writes aqtinstall.log into the working directory; keep it beside the install.
  if ! (cd "$QT_PREFIX" && "${aqt[@]}" -m "${AQT_MODULES[@]}" --archives "${AQT_ARCHIVES[@]}" && "${aqt[@]}" --archives icu); then
    echo "Qt download failed (log: $QT_PREFIX/aqtinstall.log). aqt needs HTTPS access to download.qt.io or a mirror" >&2
    echo "given with --qt-base. A Claude Code cloud environment whose network policy denies download.qt.io needs it" >&2
    echo "under the environment's Allowed domains; a session started after that change can rerun this script." >&2
    exit 1
  fi
  qt_complete || { echo "Qt install in $QT_DIR is incomplete" >&2; exit 1; }
}

probe_docker() {
  step "Docker probe"
  if ! command -v docker >/dev/null 2>&1; then
    echo "docker: not installed"
  elif docker version >/dev/null 2>&1; then
    echo "docker: daemon reachable ($(docker version --format '{{.Server.Version}}' 2>/dev/null))"
    echo "gate path: 'docker build .' works here as an advisory check only. On the lead's Windows PC a local docker build"
    echo "must run inside scripts/gate-lock.ps1 -Run (machine gate lock). The authoritative gate (lead-windows-gate or the"
    echo "lead's local tester) runs the Docker build and decides done. See docs/CI-GATE-RUNNER.md."
    return
  else
    echo "docker: client present, daemon unreachable"
  fi
  echo "gate path: offscreen CTest here only; the Docker build runs in lead-windows-gate (authoritative). See docs/CI-GATE-RUNNER.md."
}

probe_docker
if ((RUN_APT)); then step "System packages (Ubuntu 24.04)"; install_packages; fi
step "Qt $QT_VERSION $QT_ARCH -> $QT_DIR"
install_qt

if ((RUN_BUILD)); then
  step "Configure $BUILD_DIR ($BUILD_TYPE)"
  cmake -S "$ROOT" -B "$BUILD_DIR" -G Ninja -DCMAKE_BUILD_TYPE="$BUILD_TYPE" -DBUILD_TESTING=ON -DCMAKE_PREFIX_PATH="$QT_DIR"
  step "Build ($JOBS jobs)"
  cmake --build "$BUILD_DIR" --parallel "$JOBS"
fi

status=0
if ((RUN_TESTS)); then
  ctest_cmd=(ctest --test-dir "$BUILD_DIR" --output-on-failure)
  if ((${#CTEST_ARGS[@]})); then
    ctest_cmd+=("${CTEST_ARGS[@]}")
    step "CTest (offscreen): ${CTEST_ARGS[*]}"
  elif grep -qx '  fast' <<<"$(ctest --test-dir "$BUILD_DIR" --print-labels 2>/dev/null)"; then
    ctest_cmd+=(-L fast)
    step "CTest (offscreen): -L fast"
  else
    step "CTest (offscreen): full suite (no 'fast' label defined yet)"
  fi
  QT_QPA_PLATFORM=offscreen "${ctest_cmd[@]}" || status=$?
fi

exit "$status"
