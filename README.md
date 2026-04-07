<p align="center">
  <h1 align="center">AetherKiri</h1>
  <p align="center">Next-Generation KiriKiri2 Cross-Platform Runtime — Pushing the Engine to Its Limits</p>
</p>

<p align="center">
  <img src="https://img.shields.io/badge/status-In%20Development-orange" alt="Status">
  <img src="https://img.shields.io/badge/engine-KiriKiri2-blue" alt="Engine">
  <img src="https://img.shields.io/badge/framework-Flutter-02569B" alt="Flutter">
  <img src="https://img.shields.io/badge/graphics-ANGLE-red" alt="ANGLE">
  <img src="https://img.shields.io/badge/license-GPL--3.0-blue" alt="License">
</p>

---

> AetherKiri is a fork of [KrKr2-Next](https://github.com/reAAAq/KrKr2-Next) with the goal of **maximizing the capability** of the KiriKiri2 visual novel engine on modern hardware and platforms.

## What is AetherKiri?

AetherKiri is a modern, cross-platform runtime for the [KiriKiri2](https://en.wikipedia.org/wiki/KiriKiri) visual novel engine. It runs original KiriKiri2 game scripts unmodified while delivering significantly better performance, broader platform support, and a contemporary user experience.

The project reimagines the entire rendering and UI stack — replacing the legacy Cocos2d-x + GLFW pipeline with an **ANGLE-based offscreen rendering pipeline** and a **Flutter-based UI** — to bring KiriKiri2 games to five major platforms: **macOS, iOS, Windows, Linux, and Android**.

<p align="center">
  <img src="doc/1.png" alt="Running on macOS with Metal backend" width="800">
</p>

## Key Highlights

### Zero-Copy Hardware-Accelerated Rendering
The engine renders offscreen via ANGLE's EGL Pbuffer Surface (OpenGL ES 2.0), then delivers frames to the Flutter Texture Widget through **platform-native zero-copy texture sharing** — no pixel buffers are copied across the engine/UI boundary:

| Platform | Graphics API | Zero-Copy Mechanism |
|----------|-------------|-------------------|
| macOS / iOS | Metal | IOSurface |
| Windows | Direct3D 11 | D3D11 Texture |
| Linux | Vulkan / Desktop GL | DMA-BUF |
| Android | OpenGL ES / Vulkan | HardwareBuffer |

### Full Game Script Compatibility
AetherKiri runs original KiriKiri2 game scripts (TJS2) without modification. It faithfully implements the complete KiriKiri2 virtual machine, plugin system, and resource archive (XP3) handling.

### SIMD-Accelerated Pixel Operations
Pixel blending and compositing are accelerated using the [Highway](https://github.com/google/highway) library for cross-platform SIMD (SSE/AVX/NEON), yielding significant speedups in software rendering paths.

### Modern Flutter UI
A unified Flutter-based interface replaces platform-specific native UI code, providing a consistent experience across desktop and mobile with built-in debugging tools (FPS monitor, engine lifecycle management, rendering state inspection).

### Modular Plugin Architecture
Games relying on custom native plugins are supported through a stub/mock system that gracefully degrades missing plugins, improving compatibility across a wider range of titles.

## Architecture

<p align="center">
  <img src="doc/architecture.png" alt="Architecture" width="700">
</p>

```
┌─────────────────────────────────────────────────┐
│                 Flutter UI Layer                 │
│          (Texture Widget, Input, Debug)          │
├─────────────────────────────────────────────────┤
│              engine_api C Bridge                 │
│    engine_create / engine_tick / engine_destroy  │
├─────────────────────────────────────────────────┤
│              C++ Engine Core                     │
│  ┌─────────┬──────────┬──────────┬───────────┐  │
│  │  TJS2   │ Visual   │  Sound   │  Plugins  │  │
│  │   VM    │ Renderer │  System  │  & Ext.   │  │
│  └─────────┴──────────┴──────────┴───────────┘  │
├─────────────────────────────────────────────────┤
│              ANGLE (EGL / GLES 2.0)             │
│         Offscreen Pbuffer Rendering             │
├─────────────────────────────────────────────────┤
│        Platform Graphics (Metal / D3D11 /        │
│           Vulkan / GL / HW Buffer)              │
└─────────────────────────────────────────────────┘
```

## Platform Support

| Platform | Status | Notes |
|----------|--------|-------|
| macOS | Mostly Done | Metal backend via IOSurface; most mature platform |
| iOS | In Progress | Pipeline working; OpenGL rendering being optimized |
| Android | In Progress | Pipeline working; actively being optimized |
| Windows | Planned | Direct3D 11 backend via D3D11 Texture |
| Linux | Planned | Vulkan / Desktop GL backend via DMA-BUF |

> This project is under active development. No stable release is available yet.

## Development Progress

| Module | Status | Notes |
|--------|--------|-------|
| C++ Engine Core | Done | KiriKiri2 core engine compiles on all platforms |
| ANGLE Rendering Migration | Mostly Done | Replaced legacy Cocos2d-x + GLFW with EGL/GLES offscreen rendering |
| engine_api Bridge Layer | Done | Exports `engine_create` / `engine_tick` / `engine_destroy` C APIs |
| Flutter Plugin | Mostly Done | Platform Channel communication, Texture bridge |
| Zero-Copy Texture Rendering | Mostly Done | Platform-native texture sharing to Flutter |
| Flutter Debug UI | Mostly Done | FPS control, engine lifecycle, rendering status |
| Input Event Forwarding | Mostly Done | Mouse / touch coordinate mapping to engine |
| Plugin System Completion | In Progress | Expanding game-specific plugin coverage and stub accuracy |
| Game Compatibility | In Progress | Parser completion, plugin stubs, font handling improvements |
| Original krkr2 Feature Porting | Planned | Gradual migration of original emulator features |

## Roadmap

| Priority | Task | Status |
|----------|------|--------|
| P0 | Complete the plugin system (game-specific plugin reimplementation & stub coverage) | In Progress |
| P0 | SIMD pixel blending ([Highway](https://github.com/google/highway)) | Done |
| P1 | TJS2 VM interpreter optimization (computed goto) | Planned |
| P1 | OpenMP parallelization for CPU-intensive paths | In Progress |

## Contributing

**We are actively looking for reverse engineers.**

Many KiriKiri2 games ship with custom native plugins (`.dll` / `.so`) that are critical for gameplay. Reverse engineering these plugins and re-implementing them as native TJS2 extensions or compatible stubs is the biggest bottleneck for game compatibility right now. If you have experience with binary reverse engineering, Win32 API, or Japanese visual novel internals, your help would make a direct impact.

See the `cpp/plugins/` directory for existing plugin implementations and stubs to get started.

General contributions (bug fixes, new platform ports, plugin stubs, test coverage) are also welcome — feel free to open issues or pull requests.

## Building

### Prerequisites

- CMake 3.28+
- Flutter SDK
- vcpkg (configured via `vcpkg-configuration.json`)
- Platform-specific toolchains:
  - **macOS/iOS**: Xcode, Metal SDK
  - **Android**: Android NDK
  - **Windows**: Visual Studio 2019+, Windows SDK
  - **Linux**: Vulkan headers, Mesa

### Build

```bash
# Clone with submodules
git clone --recursive https://github.com/<your-repo>/AetherKiri.git
cd AetherKiri

# Build using the unified script
./build.sh
```

Platform-specific build scripts are also available:
- `build_android_windows.ps1` — Build Android from Windows
- `build_release.bat` — Windows release build

## Acknowledgements

- **[KrKr2-Next](https://github.com/reAAAq/KrKr2-Next)** — The original project this was forked from
- **[KiriKiri2](https://en.wikipedia.org/wiki/KiriKiri)** — The visual novel engine by w.dee
- **[ANGLE](https://chromium.googlesource.com/angle/angle)** — OpenGL ES implementation on top of platform graphics APIs
- **[Highway](https://github.com/google/highway)** — Cross-platform SIMD library
- **[Flutter](https://flutter.dev)** — Cross-platform UI framework

## License

This project is licensed under the **GNU General Public License v3.0 (GPL-3.0)**. See [LICENSE](./LICENSE) for details.
