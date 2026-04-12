#!/usr/bin/env bash
#
# build_linux.sh — One-step build script for krkr2 Linux x64 (Flutter)
#
# Usage:
#   ./build_linux.sh [debug|release]
#
# Output: Flutter Linux desktop bundle with bundled native engine
#
# This script will:
#   1. Build the C++ engine shared library (libengine_api.so) via CMake/Ninja
#   2. Build the Flutter Linux application
#   3. Bundle the engine .so into the Flutter bundle
#

set -euo pipefail

# ============================================================
# Configuration
# ============================================================
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"

BUILD_TYPE="${1:-debug}"
BUILD_TYPE_LOWER="$(echo "$BUILD_TYPE" | tr '[:upper:]' '[:lower:]')"

if [[ "$BUILD_TYPE_LOWER" != "debug" && "$BUILD_TYPE_LOWER" != "release" ]]; then
    echo "Error: Invalid build type '$BUILD_TYPE'. Use 'debug' or 'release'."
    exit 1
fi

# Capitalize for CMake preset names
BUILD_TYPE_CAP="$(echo "${BUILD_TYPE_LOWER:0:1}" | tr '[:lower:]' '[:upper:]')${BUILD_TYPE_LOWER:1}"

CMAKE_CONFIG_PRESET="Linux ${BUILD_TYPE_CAP} Config"
CMAKE_BUILD_PRESET="Linux ${BUILD_TYPE_CAP} Build"
CMAKE_BUILD_DIR="$PROJECT_ROOT/out/linux/$BUILD_TYPE_LOWER"

if [[ -d "$PROJECT_ROOT/.devtools/flutter" ]]; then
    FLUTTER_SDK="$PROJECT_ROOT/.devtools/flutter"
    FLUTTER_BIN="$FLUTTER_SDK/bin/flutter"
elif command -v flutter >/dev/null 2>&1; then
    FLUTTER_BIN="$(command -v flutter)"
    if command -v realpath >/dev/null 2>&1; then
        RESOLVED_BIN="$(realpath "$FLUTTER_BIN")"
    elif command -v python3 >/dev/null 2>&1; then
        RESOLVED_BIN="$(python3 -c "import os, sys; print(os.path.realpath(sys.argv[1]))" "$FLUTTER_BIN")"
    else
        RESOLVED_BIN="$FLUTTER_BIN"
    fi
    FLUTTER_SDK="$(dirname "$(dirname "$RESOLVED_BIN")")"
else
    echo "Error: Flutter SDK not found in .devtools and not in PATH."
    exit 1
fi

FLUTTER_APP_DIR="$PROJECT_ROOT/apps/flutter_app"

if [[ -d "$PROJECT_ROOT/.devtools/vcpkg/.git" ]]; then
    VCPKG_ROOT="$PROJECT_ROOT/.devtools/vcpkg"
elif [[ -n "${VCPKG_ROOT:-}" && -f "$VCPKG_ROOT/.vcpkg-root" ]]; then
    # Keep the environment VCPKG_ROOT if set
    :
else
    echo "[INFO] vcpkg not found. Automatically setting up vcpkg in .devtools/vcpkg..."
    mkdir -p "$PROJECT_ROOT/.devtools"
    git clone https://github.com/microsoft/vcpkg.git "$PROJECT_ROOT/.devtools/vcpkg"
    (cd "$PROJECT_ROOT/.devtools/vcpkg" && ./bootstrap-vcpkg.sh -disableMetrics)
    VCPKG_ROOT="$PROJECT_ROOT/.devtools/vcpkg"
fi

PARALLEL_JOBS="${JOBS:-8}"

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[0;33m'
CYAN='\033[0;36m'
NC='\033[0m' # No Color

# ============================================================
# Helper functions
# ============================================================
log_step() {
    echo ""
    echo -e "${CYAN}========================================${NC}"
    echo -e "${CYAN}  $1${NC}"
    echo -e "${CYAN}========================================${NC}"
}

log_info() {
    echo -e "${GREEN}[INFO]${NC} $1"
}

log_warn() {
    echo -e "${YELLOW}[WARN]${NC} $1"
}

log_error() {
    echo -e "${RED}[ERROR]${NC} $1"
}

check_command() {
    if ! command -v "$1" &>/dev/null; then
        log_error "'$1' is not installed or not in PATH."
        exit 1
    fi
}

# ============================================================
# Pre-flight checks
# ============================================================
log_step "Pre-flight checks"

check_command cmake
check_command ninja

if [[ ! -x "$FLUTTER_BIN" ]]; then
    log_error "Flutter SDK not found at: $FLUTTER_SDK"
    log_info "Expected path: $FLUTTER_BIN"
    exit 1
fi

if [[ ! -d "$VCPKG_ROOT" ]]; then
    log_error "vcpkg not found at: $VCPKG_ROOT"
    exit 1
fi

log_info "Build type:    $BUILD_TYPE_CAP"
log_info "Project root:  $PROJECT_ROOT"
log_info "CMake preset:  $CMAKE_BUILD_PRESET"
log_info "Flutter SDK:   $FLUTTER_SDK"
log_info "Parallel jobs: $PARALLEL_JOBS"

# ============================================================
# Step 1: Build C++ engine (shared library for Linux)
# ============================================================
log_step "Step 1/3: Building C++ engine"

export VCPKG_ROOT

# Always run a fresh configure so cached package/library paths do not drift
log_info "Running fresh CMake configure..."
cmake --preset "$CMAKE_CONFIG_PRESET" --fresh

# Build
log_info "Building C++ engine with $PARALLEL_JOBS parallel jobs..."
cmake --build --preset "$CMAKE_BUILD_PRESET" -- -j"$PARALLEL_JOBS"

# Verify the shared library was built
ENGINE_SO="$CMAKE_BUILD_DIR/bridge/engine_api/libengine_api.so"
if [[ ! -f "$ENGINE_SO" ]]; then
    log_error "Engine shared library not found at: $ENGINE_SO"
    log_error "C++ engine build may have failed."
    exit 1
fi

log_info "Engine shared library built: $ENGINE_SO"

# ============================================================
# Step 2: Build Flutter Linux app
# ============================================================
log_step "Step 2/3: Building Flutter Linux app"

export PATH="$FLUTTER_SDK/bin:$PATH"

log_info "Running flutter pub get..."
(cd "$FLUTTER_APP_DIR" && "$FLUTTER_BIN" pub get)

FLUTTER_BUILD_MODE="$BUILD_TYPE_LOWER"
log_info "Building Flutter Linux app ($FLUTTER_BUILD_MODE)..."
(cd "$FLUTTER_APP_DIR" && "$FLUTTER_BIN" build linux --"$FLUTTER_BUILD_MODE")

# Locate the bundle output directory
BUNDLE_DIR="$FLUTTER_APP_DIR/build/linux/x64/$FLUTTER_BUILD_MODE/bundle"
if [[ ! -d "$BUNDLE_DIR" ]]; then
    log_error "Flutter Linux bundle not found at: $BUNDLE_DIR"
    exit 1
fi

log_info "Flutter Linux bundle built: $BUNDLE_DIR"

# ============================================================
# Step 3: Bundle engine .so into Flutter bundle
# ============================================================
log_step "Step 3/3: Bundling engine .so into Flutter bundle"

LIB_DIR="$BUNDLE_DIR/lib"
mkdir -p "$LIB_DIR"

cp -f "$ENGINE_SO" "$LIB_DIR/libengine_api.so"
log_info "Copied libengine_api.so -> $LIB_DIR/"

# Also copy any vcpkg shared libs that weren't statically linked
VCPKG_LIB_DIR="$CMAKE_BUILD_DIR/vcpkg_installed/x64-linux/lib"
if [[ -d "$VCPKG_LIB_DIR" ]]; then
    while IFS= read -r -d '' sofile; do
        cp -f "$sofile" "$LIB_DIR/"
        log_info "  Bundled vcpkg .so: $(basename "$sofile")"
    done < <(find "$VCPKG_LIB_DIR" -maxdepth 1 -name "*.so*" -type f -print0 2>/dev/null)
fi

# ============================================================
# Done
# ============================================================
log_step "Build complete!"

log_info "Bundle: $BUNDLE_DIR"
log_info "Engine: $LIB_DIR/libengine_api.so"
echo ""
log_info "To run the app:"
echo "  \"$BUNDLE_DIR/aetherkiri\""
echo ""
