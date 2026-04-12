#!/usr/bin/env bash
#
# build.sh — krkr2 unified build entry point
#
# Usage:
#   ./build.sh <platform> [options]
#   ./build.sh                          # Interactive platform selection
#
# Platforms:
#   android, ios, macos, linux
#
# Options:
#   debug|release       Build type (default: debug)
#   --abi=<abis>        Android only: target ABIs (default: arm64-v8a)
#   --jobs=<N>          Parallel build jobs (default: 8)
#   --clean             Clean build artifacts before building
#   --help, -h          Show this help message
#
# Examples:
#   ./build.sh android debug --abi=arm64-v8a
#   ./build.sh ios release
#   ./build.sh macos debug --jobs=16
#   ./build.sh --clean android release
#

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD_SCRIPTS_DIR="$SCRIPT_DIR/build"

# Colors
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[0;33m'
CYAN='\033[0;36m'
NC='\033[0m'

# ============================================================
# Help
# ============================================================
show_help() {
    echo ""
    echo -e "${CYAN}krkr2 Unified Build Script${NC}"
    echo ""
    echo "Usage:"
    echo "  ./build.sh <platform> [options]"
    echo "  ./build.sh                          # Interactive platform selection"
    echo ""
    echo "Platforms:"
    echo "  android    Build Android APK (Flutter + native engine)"
    echo "  ios        Build iOS app (C++ static lib + Flutter)"
    echo "  macos      Build macOS app (C++ dylib + Flutter)"
    echo "  linux      Build Linux x64 app (C++ .so + Flutter)"
    echo ""
    echo "Options:"
    echo "  debug|release       Build type (default: debug)"
    echo "  --abi=<abis>        Android only: comma-separated ABIs"
    echo "                      (arm64-v8a, armeabi-v7a, x86_64, x86)"
    echo "  --jobs=<N>          Parallel build jobs (default: 8)"
    echo "  --clean             Clean build artifacts before building"
    echo "  --help, -h          Show this help message"
    echo ""
    echo "Examples:"
    echo "  ./build.sh android debug --abi=arm64-v8a"
    echo "  ./build.sh ios release"
    echo "  ./build.sh macos debug --jobs=16"
    echo "  ./build.sh --clean android release"
    echo ""
}

# ============================================================
# Parse arguments
# ============================================================
PLATFORM=""
BUILD_TYPE=""
CLEAN=false
EXTRA_ARGS=()

for arg in "$@"; do
    case "$arg" in
        --help|-h)
            show_help
            exit 0
            ;;
        --clean)
            CLEAN=true
            ;;
        --jobs=*)
            export JOBS="${arg#*=}"
            ;;
        android|ios|macos|linux)
            PLATFORM="$arg"
            ;;
        debug|release|Debug|Release)
            BUILD_TYPE="$(echo "$arg" | tr '[:upper:]' '[:lower:]')"
            ;;
        --abi=*)
            EXTRA_ARGS+=("$arg")
            ;;
        *)
            echo -e "${YELLOW}[WARN]${NC} Unknown argument: $arg (passing through)"
            EXTRA_ARGS+=("$arg")
            ;;
    esac
done

# ============================================================
# Interactive platform selection (if not specified)
# ============================================================
if [[ -z "$PLATFORM" ]]; then
    echo ""
    echo -e "${CYAN}==============================${NC}"
    echo -e "${CYAN}  krkr2 Build System${NC}"
    echo -e "${CYAN}==============================${NC}"
    echo ""
    echo "Select target platform:"
    echo ""
    echo "  1) android"
    echo "  2) ios"
    echo "  3) macos"
    echo "  4) linux"
    echo ""
    read -rp "Enter choice [1-4]: " choice
    case "$choice" in
        1|android)  PLATFORM="android" ;;
        2|ios)      PLATFORM="ios" ;;
        3|macos)    PLATFORM="macos" ;;
        4|linux)    PLATFORM="linux" ;;
        *)
            echo -e "${RED}[ERROR]${NC} Invalid choice: $choice"
            exit 1
            ;;
    esac
    echo ""
fi

# Default build type
if [[ -z "$BUILD_TYPE" ]]; then
    BUILD_TYPE="debug"
fi

# ============================================================
# Validate platform script exists
# ============================================================
PLATFORM_SCRIPT="$BUILD_SCRIPTS_DIR/build_${PLATFORM}.sh"

if [[ ! -f "$PLATFORM_SCRIPT" ]]; then
    echo -e "${RED}[ERROR]${NC} Build script not found: $PLATFORM_SCRIPT"
    exit 1
fi

# ============================================================
# Clean (optional)
# ============================================================
if [[ "$CLEAN" == true ]]; then
    echo -e "${CYAN}Cleaning build artifacts for $PLATFORM...${NC}"
    case "$PLATFORM" in
        android)
            rm -rf "$SCRIPT_DIR/apps/flutter_app/build/app"
            rm -rf "$SCRIPT_DIR/apps/flutter_app/build/.cxx"
            echo -e "${GREEN}[INFO]${NC} Android build artifacts cleaned."
            ;;
        ios)
            rm -rf "$SCRIPT_DIR/out/ios/$BUILD_TYPE"
            rm -rf "$SCRIPT_DIR/apps/flutter_app/build/ios"
            echo -e "${GREEN}[INFO]${NC} iOS build artifacts cleaned."
            ;;
        macos)
            rm -rf "$SCRIPT_DIR/out/macos/$BUILD_TYPE"
            rm -rf "$SCRIPT_DIR/apps/flutter_app/build/macos"
            echo -e "${GREEN}[INFO]${NC} macOS build artifacts cleaned."
            ;;
    esac
    echo ""
fi

# ============================================================
# Run platform build script
# ============================================================
echo -e "${CYAN}==============================${NC}"
echo -e "${CYAN}  Building: $PLATFORM ($BUILD_TYPE)${NC}"
echo -e "${CYAN}==============================${NC}"
echo ""

# Ensure the script is executable
chmod +x "$PLATFORM_SCRIPT"

# Execute the platform-specific build script with arguments
exec bash "$PLATFORM_SCRIPT" "$BUILD_TYPE" ${EXTRA_ARGS[@]+"${EXTRA_ARGS[@]}"}
