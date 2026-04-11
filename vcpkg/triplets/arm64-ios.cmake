set(VCPKG_BUILD_TYPE release)

set(VCPKG_TARGET_ARCHITECTURE arm64)
set(VCPKG_CRT_LINKAGE dynamic)
set(VCPKG_LIBRARY_LINKAGE static)
set(VCPKG_CMAKE_SYSTEM_NAME iOS)
set(VCPKG_OSX_DEPLOYMENT_TARGET 15.0)
set(VCPKG_OSX_ARCHITECTURES arm64)

# Fix autotools cross-compilation detection for iOS
# Without this, --host and --build are both aarch64-apple-darwin on Apple Silicon,
# causing configure to think it's not cross-compiling and try to run iOS binaries.
set(VCPKG_MAKE_BUILD_TRIPLET "--host=aarch64-apple-ios")
