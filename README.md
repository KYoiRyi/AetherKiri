<p align="center">
  <h1 align="center">KrKr2 Next</h1>
  <p align="center">基于 Flutter 重构的下一代 KiriKiri2 跨平台模拟器</p>
</p>

<p align="center">
  <img src="https://img.shields.io/badge/status-In%20Development-orange" alt="Status">
  <img src="https://img.shields.io/badge/engine-KiriKiri2-blue" alt="Engine">
  <img src="https://img.shields.io/badge/framework-Flutter-02569B" alt="Flutter">
  <img src="https://img.shields.io/badge/graphics-ANGLE-red" alt="ANGLE">
  <img src="https://img.shields.io/badge/license-GPL--3.0-blue" alt="License">
</p>

---

**语言 / Language**: 中文 | [English](README_EN.md)

> 🙏 本项目基于 [krkr2](https://github.com/2468785842/krkr2) 重构，感谢原作者的贡献。

## 简介

**KrKr2 Next** 是 [KiriKiri2 (吉里吉里2)](https://zh.wikipedia.org/wiki/%E5%90%89%E9%87%8C%E5%90%89%E9%87%8C2) 视觉小说引擎的现代化跨平台运行环境。它完全兼容原版游戏脚本，使用现代图形接口进行硬件加速渲染，并在渲染性能和脚本执行效率上做了大量优化。基于 Flutter 构建统一的跨平台界面，支持 macOS · iOS · Windows · Linux · Android 五大平台。

下图为当前在 macOS 上通过 Metal 后端运行的实际效果：

<p align="center">
  <img src="doc/1.png" alt="macOS Metal 后端运行截图" width="800">
</p>

## 架构

<p align="center">
  <img src="doc/architecture.png" alt="技术架构图" width="700">
</p>

**渲染管线**：引擎通过 ANGLE 的 EGL Pbuffer Surface 进行离屏渲染（OpenGL ES 2.0），渲染结果通过平台原生纹理共享机制（macOS → IOSurface、Windows → D3D11 Texture、Linux → DMA-BUF）零拷贝传递给 Flutter Texture Widget 显示。


## 开发进度

> ⚠️ 本项目处于活跃开发阶段，尚未发布稳定版本。macOS 平台进度领先。

| 模块 | 状态 | 说明 |
|------|------|------|
| C++ 引擎核心编译 | ✅ 完成 | KiriKiri2 核心引擎全平台可编译 |
| ANGLE 渲染层迁移 | ✅ 基本完成 | 替代原 Cocos2d-x + GLFW 渲染管线，使用 EGL/GLES 离屏渲染 |
| engine_api 桥接层 | ✅ 完成 | 导出 `engine_create` / `engine_tick` / `engine_destroy` 等 C API |
| Flutter Plugin | ✅ 基本完成 | Platform Channel 通信、Texture 纹理桥接 |
| Texture 零拷贝渲染 | ✅ 基本完成 | 通过平台原生纹理共享机制零拷贝传递引擎渲染帧到 Flutter |
| Flutter 调试 UI | ✅ 基本完成 | FPS 控制、引擎生命周期管理、渲染状态监控 |
| 输入事件转发 | ✅ 基本完成 | 鼠标 / 触控事件坐标映射转发到引擎 |
| 引擎性能优化 | 🔨 进行中 | SIMD 像素混合、GPU 合成管线、VM 解释器优化等 |
| 游戏兼容性优化 | 🔨 进行中 | 补全解析引擎、添加插件，阶段目标与 Z 大闭源版兼容性持平 |
| 原有 krkr2 模拟器功能移植 | 📋 规划中 | 将原有 krkr2 模拟器功能逐步移植到新架构 |

## 平台支持状态

| 平台 | 状态 | 图形后端 | 纹理共享机制 |
|------|------|----------|-------------|
| macOS | ✅ 基本完成 | Metal | IOSurface |
| iOS | 🔨 流程打通，正在优化和修复 OpenGL 渲染 | Metal | IOSurface |
| Windows | 📋 计划中 | Direct3D 11 | D3D11 Texture |
| Linux | 📋 计划中 | Vulkan / Desktop GL | DMA-BUF |
| Android | 🔨 流程跑通，优化中 | OpenGL ES / Vulkan | HardwareBuffer |

## 引擎性能优化

| 优先级 | 任务 | 状态 |
|--------|------|------|
| P0 | 像素混合 SIMD 化 ([Highway](https://github.com/google/highway)) | ✅ 完成 |
| P0 | 全 GPU 合成渲染管线 | 🔨 进行中 |
| P0 | TJS2 VM 解释器优化 (computed goto) | 📋 计划中 |
## 许可证

本项目基于 GNU General Public License v3.0 (GPL-3.0) 开源，详见 [LICENSE](./LICENSE)。

