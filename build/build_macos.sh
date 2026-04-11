#!/usr/bin/env bash
#
# build_macos.sh — One-step build script for krkr2 macOS (Flutter only)
#
# Usage:
#   ./build_macos.sh [debug|release]
#
# Output: Flutter macOS .app with bundled engine
#
# This script will:
#   1. Build the C++ engine library (libengine_api.dylib) via CMake/Ninja
#   2. Build the Flutter macOS application
#   3. Bundle the engine dylib into the Flutter .app
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

CMAKE_CONFIG_PRESET="MacOS ${BUILD_TYPE_CAP} Config"
CMAKE_BUILD_PRESET="MacOS ${BUILD_TYPE_CAP} Build"
CMAKE_BUILD_DIR="$PROJECT_ROOT/out/macos/$BUILD_TYPE_LOWER"

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
# Step 1: Build C++ engine
# ============================================================
log_step "Step 1/3: Building C++ engine"

export VCPKG_ROOT

# Always re-run configure so preset cache variables stay in sync with the
# current macOS build expectations (for example BUILD_TOOLS=OFF).
log_info "Running CMake configure..."
cmake --preset "$CMAKE_CONFIG_PRESET"

# Build
log_info "Building C++ engine with $PARALLEL_JOBS parallel jobs..."
cmake --build --preset "$CMAKE_BUILD_PRESET" -- -j"$PARALLEL_JOBS"

# Verify the dylib was built
ENGINE_DYLIB="$CMAKE_BUILD_DIR/bridge/engine_api/libengine_api.dylib"
if [[ ! -f "$ENGINE_DYLIB" ]]; then
    log_error "Engine dylib not found at: $ENGINE_DYLIB"
    log_error "C++ engine build may have failed."
    exit 1
fi

log_info "Engine dylib built: $ENGINE_DYLIB"

# ============================================================
# Step 2: Build Flutter macOS app
# ============================================================
log_step "Step 2/3: Building Flutter macOS app"

export PATH="$FLUTTER_SDK/bin:$PATH"

log_info "Running flutter pub get..."
(cd "$FLUTTER_APP_DIR" && "$FLUTTER_BIN" pub get)

FLUTTER_BUILD_MODE="$BUILD_TYPE_LOWER"
log_info "Building Flutter app ($FLUTTER_BUILD_MODE)..."
(cd "$FLUTTER_APP_DIR" && "$FLUTTER_BIN" build macos --"$FLUTTER_BUILD_MODE")

# Locate the built .app bundle
if [[ "$FLUTTER_BUILD_MODE" == "release" ]]; then
    FLUTTER_BUILD_SUBDIR="Release"
else
    FLUTTER_BUILD_SUBDIR="Debug"
fi
APP_BUNDLE="$FLUTTER_APP_DIR/build/macos/Build/Products/$FLUTTER_BUILD_SUBDIR/AetherKiri.app"

if [[ ! -d "$APP_BUNDLE" ]]; then
    # Try alternate path patterns
    APP_BUNDLE="$(find "$FLUTTER_APP_DIR/build/macos" -name "*.app" -type d 2>/dev/null | head -1)"
    if [[ -z "$APP_BUNDLE" || ! -d "$APP_BUNDLE" ]]; then
        log_error "Flutter .app bundle not found."
        log_error "Expected at: $FLUTTER_APP_DIR/build/macos/Build/Products/$FLUTTER_BUILD_SUBDIR/"
        exit 1
    fi
fi

log_info "Flutter app built: $APP_BUNDLE"

# ============================================================
# Step 3: Bundle engine dylib into .app
# ============================================================
log_step "Step 3/3: Bundling engine dylib into .app"

FRAMEWORKS_DIR="$APP_BUNDLE/Contents/Frameworks"
mkdir -p "$FRAMEWORKS_DIR"

cp -f "$ENGINE_DYLIB" "$FRAMEWORKS_DIR/libengine_api.dylib"
log_info "Copied libengine_api.dylib -> $FRAMEWORKS_DIR/"

# Fix the dylib's install name so the app can find it at runtime
install_name_tool -id "@executable_path/../Frameworks/libengine_api.dylib" \
    "$FRAMEWORKS_DIR/libengine_api.dylib" 2>/dev/null || true

# Re-sign the .app bundle (ad-hoc signing for local development)
# Must specify --entitlements to preserve sandbox permissions (e.g. file picker)
log_info "Re-signing the .app bundle..."
if [[ "$FLUTTER_BUILD_MODE" == "release" ]]; then
    ENTITLEMENTS_FILE="$FLUTTER_APP_DIR/macos/Runner/Release.entitlements"
else
    ENTITLEMENTS_FILE="$FLUTTER_APP_DIR/macos/Runner/DebugProfile.entitlements"
fi
codesign --force --deep --sign - --entitlements "$ENTITLEMENTS_FILE" "$APP_BUNDLE" 2>/dev/null || {
    log_warn "codesign failed (non-fatal for local development)"
}

# ============================================================
# Done
# ============================================================
log_step "Build complete!"

APP_NAME="$(basename "$APP_BUNDLE")"
log_info "App bundle:  $APP_BUNDLE"
log_info "Engine dylib: $FRAMEWORKS_DIR/libengine_api.dylib"
echo ""
log_info "To run the app:"
echo "  open \"$APP_BUNDLE\""
echo ""
log_info "Or launch from terminal:"
echo "  \"$APP_BUNDLE/Contents/MacOS/AetherKiri\""
echo ""
