#!/usr/bin/env bash
#
# build_android.sh — One-step build script for krkr2 Android (Flutter)
#
# Usage:
#   ./build_android.sh [debug|release] [--abi=arm64-v8a,x86_64]
#
# Output: Flutter Android APK with bundled native engine
#
# This script will:
#   1. Pre-install vcpkg dependencies for the target Android ABIs
#   2. Build the Flutter Android APK (which triggers CMake native build via Gradle)
#   3. Verify and report the output APK
#

set -euo pipefail

# ============================================================
# Configuration
# ============================================================
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"

BUILD_TYPE="${1:-debug}"
BUILD_TYPE_LOWER="$(echo "$BUILD_TYPE" | tr '[:upper:]' '[:lower:]')"
SKIP_ANDROID_VCPKG_INSTALL="${SKIP_ANDROID_VCPKG_INSTALL:-0}"

# Parse optional --abi argument
TARGET_ABIS="arm64-v8a"
for arg in "$@"; do
    case $arg in
        --abi=*)
            TARGET_ABIS="${arg#*=}"
            ;;
    esac
done

if [[ "$BUILD_TYPE_LOWER" != "debug" && "$BUILD_TYPE_LOWER" != "release" ]]; then
    echo "Error: Invalid build type '$BUILD_TYPE'. Use 'debug' or 'release'."
    exit 1
fi

BUILD_TYPE_CAP="$(echo "${BUILD_TYPE_LOWER:0:1}" | tr '[:lower:]' '[:upper:]')${BUILD_TYPE_LOWER:1}"

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

recreate_local_vcpkg() {
    echo "[INFO] Recreating local vcpkg in .devtools/vcpkg..."
    rm -rf "$PROJECT_ROOT/.devtools/vcpkg"
    mkdir -p "$PROJECT_ROOT/.devtools"
    git clone https://github.com/microsoft/vcpkg.git "$PROJECT_ROOT/.devtools/vcpkg"
    (cd "$PROJECT_ROOT/.devtools/vcpkg" && ./bootstrap-vcpkg.sh -disableMetrics)
}

if [[ -d "$PROJECT_ROOT/.devtools/vcpkg" ]]; then
    VCPKG_ROOT="$PROJECT_ROOT/.devtools/vcpkg"
elif [[ -n "${VCPKG_ROOT:-}" && -f "$VCPKG_ROOT/.vcpkg-root" ]]; then
    :
elif [[ "$SKIP_ANDROID_VCPKG_INSTALL" == "1" ]]; then
    echo "[ERROR] Prebuilt vcpkg root not found."
    echo "[INFO] Expected path: $PROJECT_ROOT/.devtools/vcpkg"
    exit 1
else
    echo "[INFO] vcpkg not found. Automatically setting up vcpkg in .devtools/vcpkg..."
    recreate_local_vcpkg
    VCPKG_ROOT="$PROJECT_ROOT/.devtools/vcpkg"
fi

VCPKG_BIN="$VCPKG_ROOT/vcpkg"

if [[ "$SKIP_ANDROID_VCPKG_INSTALL" == "1" ]]; then
    if [[ ! -f "$VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake" ]]; then
        echo "[ERROR] Prebuilt vcpkg toolchain file missing."
        echo "[INFO] Expected path: $VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake"
        exit 1
    fi
elif [[ ! -x "$VCPKG_BIN" ]]; then
    if [[ -x "$VCPKG_ROOT/bootstrap-vcpkg.sh" ]]; then
        echo "[WARN] vcpkg binary missing, bootstrapping existing tree..."
        (cd "$VCPKG_ROOT" && ./bootstrap-vcpkg.sh -disableMetrics)
    else
        echo "[WARN] vcpkg tree is incomplete, recreating..."
        recreate_local_vcpkg
    fi
fi

VCPKG_BIN="$VCPKG_ROOT/vcpkg"

# Android SDK/NDK paths (auto-detect common locations)
if [[ -z "${ANDROID_HOME:-}" ]]; then
    if [[ -d "$HOME/Library/Android/sdk" ]]; then
        ANDROID_HOME="$HOME/Library/Android/sdk"
    elif [[ -d "$HOME/Android/Sdk" ]]; then
        ANDROID_HOME="$HOME/Android/Sdk"
    else
        ANDROID_HOME=""
    fi
fi

# Find NDK (prefer ANDROID_NDK_HOME, then auto-detect latest in SDK)
if [[ -z "${ANDROID_NDK_HOME:-}" ]]; then
    if [[ -n "$ANDROID_HOME" && -d "$ANDROID_HOME/ndk" ]]; then
        # Pick the latest NDK version
        ANDROID_NDK_HOME="$(find "$ANDROID_HOME/ndk" -maxdepth 1 -mindepth 1 -type d | sort -V | tail -1)"
    fi
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

# Map Android ABI to vcpkg triplet
abi_to_triplet() {
    case "$1" in
        arm64-v8a)    echo "arm64-android" ;;
        armeabi-v7a)  echo "arm-android" ;;
        x86_64)       echo "x64-android" ;;
        x86)          echo "x86-android" ;;
        *)
            log_error "Unknown ABI: $1"
            exit 1
            ;;
    esac
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

if [[ -z "${ANDROID_HOME:-}" ]]; then
    log_error "ANDROID_HOME not set and could not be auto-detected."
    log_info "Please set ANDROID_HOME to your Android SDK directory."
    log_info "  export ANDROID_HOME=\$HOME/Library/Android/sdk"
    exit 1
fi

if [[ -z "${ANDROID_NDK_HOME:-}" || ! -d "${ANDROID_NDK_HOME:-}" ]]; then
    log_error "ANDROID_NDK_HOME not set or not found."
    log_info "Please install Android NDK via Android Studio or sdkmanager."
    log_info "  export ANDROID_NDK_HOME=\$ANDROID_HOME/ndk/<version>"
    exit 1
fi

log_info "Build type:     $BUILD_TYPE_CAP"
log_info "Target ABIs:    $TARGET_ABIS"
log_info "Project root:   $PROJECT_ROOT"
log_info "Flutter SDK:    $FLUTTER_SDK"
log_info "vcpkg root:     $VCPKG_ROOT"
log_info "Android SDK:    $ANDROID_HOME"
log_info "Android NDK:    $ANDROID_NDK_HOME"
log_info "Parallel jobs:  $PARALLEL_JOBS"

# ============================================================
# Step 1: Install vcpkg dependencies for Android
# ============================================================
export VCPKG_ROOT
export ANDROID_NDK_HOME
export ANDROID_HOME

# Parse comma-separated ABIs
IFS=',' read -ra ABI_ARRAY <<< "$TARGET_ABIS"

if [[ "$SKIP_ANDROID_VCPKG_INSTALL" == "1" ]]; then
    log_step "Step 1/3: Using prebuilt Android vcpkg dependencies"
    for ABI in "${ABI_ARRAY[@]}"; do
        TRIPLET="$(abi_to_triplet "$ABI")"
        if [[ ! -d "$VCPKG_ROOT/installed/$TRIPLET" ]]; then
            log_error "Prebuilt vcpkg triplet missing: $TRIPLET"
            log_info "Expected directory: $VCPKG_ROOT/installed/$TRIPLET"
            exit 1
        fi
        log_info "Using prebuilt vcpkg packages for $TRIPLET ✓"
    done
else
    log_step "Step 1/3: Installing vcpkg dependencies for Android"

    VCPKG_LOCK_FILE="$VCPKG_ROOT/.vcpkg-root"

    # Wait for vcpkg filesystem lock to be released (handles background binary cache submissions)
    wait_for_vcpkg_lock() {
        local max_retries=30
        local retry_interval=2
        local attempt=0

        while (( attempt < max_retries )); do
            # Try a vcpkg command that actually acquires the filesystem lock.
            # 'vcpkg list' needs the lock, unlike 'vcpkg version' which does not.
            if (cd "$PROJECT_ROOT" && "$VCPKG_BIN" list --x-install-root="$VCPKG_ROOT/installed" &>/dev/null); then
                return 0
            fi
            attempt=$((attempt + 1))
            log_info "Waiting for vcpkg lock to be released... (${attempt}/${max_retries})"
            sleep "$retry_interval"
        done

        log_warn "vcpkg lock wait timed out after $((max_retries * retry_interval))s, proceeding anyway..."
        return 0
    }

    OVERLAY_PORTS="$PROJECT_ROOT/vcpkg/ports"
    OVERLAY_TRIPLETS="$PROJECT_ROOT/vcpkg/triplets"

    for ABI in "${ABI_ARRAY[@]}"; do
        TRIPLET="$(abi_to_triplet "$ABI")"
        log_info "Installing vcpkg packages for triplet: $TRIPLET (ABI: $ABI)..."

        # Wait for any previous vcpkg lock to be released
        wait_for_vcpkg_lock

        # Clean stale ANGLE build cache to ensure overlay port changes take effect
        if [[ -d "$VCPKG_ROOT/buildtrees/angle" ]]; then
            log_info "Cleaning ANGLE build cache for rebuild..."
            rm -rf "$VCPKG_ROOT/buildtrees/angle"
            rm -rf "$VCPKG_ROOT/packages/angle_${TRIPLET}"
        fi

        # Use manifest mode: vcpkg install from project root where vcpkg.json lives
        # Only build Release libraries to save time and cache space
        (cd "$PROJECT_ROOT" && VCPKG_BUILD_TYPE=release "$VCPKG_BIN" install \
            --triplet "$TRIPLET" \
            --x-install-root="$VCPKG_ROOT/installed" \
            --x-manifest-root="$PROJECT_ROOT" \
            --overlay-ports="$OVERLAY_PORTS" \
            --overlay-triplets="$OVERLAY_TRIPLETS" \
        ) || {
            log_error "vcpkg install failed for triplet: $TRIPLET"
            log_info "You can try running manually:"
            log_info "  cd $PROJECT_ROOT && $VCPKG_BIN install --triplet $TRIPLET --overlay-ports=$OVERLAY_PORTS --overlay-triplets=$OVERLAY_TRIPLETS"
            exit 1
        }

        log_info "vcpkg packages installed for $TRIPLET ✓"
    done
fi

# ============================================================
# Step 2: Build Flutter Android APK
# ============================================================
log_step "Step 2/3: Building Flutter Android APK ($BUILD_TYPE_CAP)"

export PATH="$FLUTTER_SDK/bin:$PATH"

log_info "Running flutter pub get..."
(cd "$FLUTTER_APP_DIR" && "$FLUTTER_BIN" pub get)

# Clean CMake cache to avoid stale configurations
if [[ -d "$FLUTTER_APP_DIR/build/.cxx" ]]; then
    log_info "Cleaning CMake build cache..."
    rm -rf "$FLUTTER_APP_DIR/build/.cxx"
fi

FLUTTER_BUILD_MODE="$BUILD_TYPE_LOWER"

# Map TARGET_ABIS to Flutter --target-platform values
FLUTTER_TARGET_PLATFORMS=""
for ABI in "${ABI_ARRAY[@]}"; do
    case "$ABI" in
        arm64-v8a)    PLATFORM="android-arm64" ;;
        armeabi-v7a)  PLATFORM="android-arm" ;;
        x86_64)       PLATFORM="android-x64" ;;
        *)            PLATFORM="" ;;
    esac
    if [[ -n "$PLATFORM" ]]; then
        if [[ -n "$FLUTTER_TARGET_PLATFORMS" ]]; then
            FLUTTER_TARGET_PLATFORMS="$FLUTTER_TARGET_PLATFORMS,$PLATFORM"
        else
            FLUTTER_TARGET_PLATFORMS="$PLATFORM"
        fi
    fi
done

log_info "Building Flutter APK ($FLUTTER_BUILD_MODE) for platforms: $FLUTTER_TARGET_PLATFORMS..."
(cd "$FLUTTER_APP_DIR" && "$FLUTTER_BIN" build apk --"$FLUTTER_BUILD_MODE" --target-platform "$FLUTTER_TARGET_PLATFORMS" -v)

# ============================================================
# Step 3: Verify output APK
# ============================================================
log_step "Step 3/3: Verifying output APK"

if [[ "$FLUTTER_BUILD_MODE" == "release" ]]; then
    APK_PATH="$FLUTTER_APP_DIR/build/app/outputs/flutter-apk/app-release.apk"
elif [[ "$FLUTTER_BUILD_MODE" == "debug" ]]; then
    APK_PATH="$FLUTTER_APP_DIR/build/app/outputs/flutter-apk/app-debug.apk"
else
    APK_PATH="$FLUTTER_APP_DIR/build/app/outputs/flutter-apk/app-$FLUTTER_BUILD_MODE.apk"
fi

if [[ ! -f "$APK_PATH" ]]; then
    # Try to find any APK
    APK_PATH="$(find "$FLUTTER_APP_DIR/build/app/outputs" -name "*.apk" -type f 2>/dev/null | head -1)"
fi

if [[ -z "${APK_PATH:-}" || ! -f "${APK_PATH:-}" ]]; then
    log_error "APK not found! Build may have failed."
    exit 1
fi

APK_SIZE="$(du -sh "$APK_PATH" | cut -f1)"
log_info "APK built successfully: $APK_PATH"
log_info "APK size: $APK_SIZE"

# Verify native libraries are bundled
if command -v unzip &>/dev/null; then
    log_info "Checking bundled native libraries..."
    NATIVE_LIBS="$(unzip -l "$APK_PATH" 2>/dev/null | grep '\.so$' | awk '{print $4}' || true)"
    if [[ -n "$NATIVE_LIBS" ]]; then
        echo "$NATIVE_LIBS" | while read -r lib; do
            echo "  ✓ $lib"
        done
    else
        log_warn "No native .so files found in APK. Something may be wrong."
    fi
fi

# ============================================================
# Done
# ============================================================
log_step "Build complete!"

log_info "APK:  $APK_PATH"
log_info "Size: $APK_SIZE"
echo ""
log_info "To install on a connected device:"
echo "  adb install \"$APK_PATH\""
echo ""
log_info "To install and run:"
echo "  adb install \"$APK_PATH\" && adb shell am start -n org.github.krkr2.flutter_app/.MainActivity"
echo ""
