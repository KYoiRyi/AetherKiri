# AetherKiri `motion` / `emoteplayer` 迁移适配方案

## 1. 结论先行

不建议在 AetherKiri 现有 `cpp/plugins/motionplayer` 基础上继续“补接口”式演进。

更稳妥的路径是：

1. 以 `../kirikiroid2-web/cpp/plugins/motionplayer` 为主线实现整体移植。
2. AetherKiri 只保留自身平台相关边界层，重点适配插件装载、图层/渲染桥接、PSB 资源通路。
3. 先做到 `motionplayer.dll` 完整可用，再把 `emoteplayer.dll` 从“别名/空壳”升级为真实入口与真实类注入。

原因很直接：

- `kirikiroid2-web` 的 `motionplayer` 已经是按 `libkrkr2.so` 逆向重建的完整实现。
- AetherKiri 当前版本仍以占位、兼容桩、简化逻辑为主，无法承载完整 E-mote 行为。

## 2. 当前调查结果

### 2.1 krkr plugin 装载模型

两个项目都不是传统“外置 DLL 真动态装载”模式，而是：

- 插件源码静态编进引擎。
- 通过 `ncbind` 的内部模块表，把源码注册成 `xxx.dll` 名义上的模块。
- `Plugins.link("foo.dll")` 最终走 `ncbAutoRegister::LoadModule("foo.dll")`。

对应代码：

- AetherKiri 的内部插件装载总入口在 [cpp/core/plugin/PluginImpl.cpp](/root/emote1/AetherKiri/cpp/core/plugin/PluginImpl.cpp#L480) 和 [cpp/core/plugin/PluginImpl.cpp](/root/emote1/AetherKiri/cpp/core/plugin/PluginImpl.cpp#L562)
- `ncbind` 的内部模块表机制在 [cpp/core/plugin/ncbind.cpp](/root/emote1/AetherKiri/cpp/core/plugin/ncbind.cpp) 和 [cpp/core/plugin/ncbind.hpp](/root/emote1/AetherKiri/cpp/core/plugin/ncbind.hpp)
- web 版同样通过内部模块预注册与加载，在 [cpp/core/plugin/PluginImpl.cpp](/root/emote1/kirikiroid2-web/cpp/core/plugin/PluginImpl.cpp#L132)

这意味着迁移目标不是“复制 DLL 文件”，而是把 web 版的模块注册表、类暴露、运行时逻辑、底层能力一起迁过来。

### 2.2 AetherKiri 当前 `motion/emote` 状态

现状可以概括为“能糊住脚本链接，但还不是完整播放器”：

- `emoteplayer.dll` 目前只是空注册桩，见 [cpp/plugins/dummy_plugin_stubs.cpp](/root/emote1/AetherKiri/cpp/plugins/dummy_plugin_stubs.cpp#L7)
- `Plugins.link("emoteplayer.dll")` 在加载时直接被改写成 `motionplayer.dll`，见 [cpp/core/plugin/PluginImpl.cpp](/root/emote1/AetherKiri/cpp/core/plugin/PluginImpl.cpp#L483)
- AetherKiri 的 `Motion.Player` 只暴露了一个精简 API 面，且保留大量 `dummy/missing/mock` 行为，见 [cpp/plugins/motionplayer/main.cpp](/root/emote1/AetherKiri/cpp/plugins/motionplayer/main.cpp#L568)
- `Motion.EmotePlayer` 当前只剩一个 `useD3D` 属性和缺失方法兜底，见 [cpp/plugins/motionplayer/main.cpp](/root/emote1/AetherKiri/cpp/plugins/motionplayer/main.cpp#L603)
- `EmotePlayer` 类本体几乎为空，见 [cpp/plugins/motionplayer/EmotePlayer.h](/root/emote1/AetherKiri/cpp/plugins/motionplayer/EmotePlayer.h)
- `Player` 的核心逻辑更接近“PSB 静态图层拼贴器”，而不是完整 motion runtime，见 [cpp/plugins/motionplayer/Player.h](/root/emote1/AetherKiri/cpp/plugins/motionplayer/Player.h)

实现规模也说明了这一点：

- AetherKiri `cpp/plugins/motionplayer` 总量约 1893 行
- web 版 `cpp/plugins/motionplayer` 总量约 16983 行

### 2.3 kirikiriroid2-web 当前 `motion/emote` 状态

web 版已经具备完整迁移价值：

- 模块拆分完整，见 [cpp/plugins/motionplayer/CMakeLists.txt](/root/emote1/kirikiroid2-web/cpp/plugins/motionplayer/CMakeLists.txt#L6)
- NCB 注册面对齐 `libkrkr2.so`，见 [cpp/plugins/motionplayer/main.cpp](/root/emote1/kirikiroid2-web/cpp/plugins/motionplayer/main.cpp#L27)
- 具备 `Point/Circle/Rect/Quad/LayerGetter/Player/ResourceManager/SeparateLayerAdaptor/D3DAdaptor/EmotePlayer/D3DEmotePlayer`
- 具备运行时拆分实现：`PlayerCore/PlayerQuery/PlayerRender/PlayerUpdateLayers/RuntimeSupport/NodeTree`
- 具备逆向分析文档和插件级单测，见 [analysis/MotionPlayer_NCB_Registration.md](/root/emote1/kirikiroid2-web/analysis/MotionPlayer_NCB_Registration.md), [analysis/EmotePlayer_Internal_Implementation.md](/root/emote1/kirikiroid2-web/analysis/EmotePlayer_Internal_Implementation.md), [tests/unit-tests/plugins/motionplayer-dll.cpp](/root/emote1/kirikiroid2-web/tests/unit-tests/plugins/motionplayer-dll.cpp)

另外，web 版已经显式把 `motionplayer.dll` 与 `emoteplayer.dll` 都作为内部模块预加载，见 [cpp/core/plugin/PluginImpl.cpp](/root/emote1/kirikiroid2-web/cpp/core/plugin/PluginImpl.cpp#L132)。

## 3. 关键差异与迁移缺口

### 3.1 模块入口差异

AetherKiri 现在的 `emoteplayer.dll` 是：

- 一个空桩模块名
- 一个到 `motionplayer.dll` 的别名

web 版则是：

- `motionplayer.dll` 先注册 `Motion` 命名空间及其子类
- `emoteplayer.dll` 再向 `Motion` 命名空间注入 `EmotePlayer`
- 同时向 `Motion.ResourceManager` 注入 `setEmotePSBDecryptSeed` / `setEmotePSBDecryptFunc`

因此迁移后必须恢复“两个插件名，两个真实入口”的结构，不能继续依赖“别名 + stub”。

### 3.2 NCB 注册面差异

AetherKiri 当前缺失或未对齐的内容包括：

- `Point/Circle/Rect/Quad/LayerGetter`
- `D3DAdaptor`
- `D3DEmotePlayer`
- 命名空间级函数 `getD3DAvailable` / `doAlphaMaskOperation`
- 更完整的 `Motion.Player` 属性与方法面
- `BezierPatch` / `LayerMeshSupport` 相关接入

这会直接影响脚本层：

- `typeof Motion.Xxx`
- `with(Motion.Player) { ... }`
- `new Motion.EmotePlayer(...)`
- `Motion.ResourceManager.setEmotePSBDecryptSeed(...)`
- 依赖 `LayerGetter`、形状碰撞、timeline/variable 查询的脚本

### 3.3 运行时架构差异

AetherKiri 当前实现是：

- 简单状态字段
- 直接画图或拼贴
- 缺省路径依赖 `missing()`/mock 对象兜底

web 版实现则是：

- `MotionSnapshot`
- `PlayerRuntime`
- `NodeTree`
- timeline/control binding
- layer update/render 分阶段
- EmotePlayer 薄壳委托 Player

这不是小修小补能追上的差距，必须整体移植运行时架构。

### 3.4 渲染能力差异

web 版 motionplayer 依赖的底层能力包括：

- `loadImages`
- `fillRect`
- `operateRect`
- `copyRect`
- `adjustGamma`
- `drawAffine`
- `BezierPatchCopy`
- `OperateBezierPatch`

AetherKiri 已有前半部分常规 Layer 能力，但当前代码库里没有看到 `BezierPatchCopy` / `OperateBezierPatch` 对应实现。

这意味着 mesh/bezier 路径是最大底层风险点，必须单独处理。

### 3.5 `SeparateLayerAdaptor` 能力差异

AetherKiri 当前 `SeparateLayerAdaptor` 只维护 `owner/target` 双指针，见 [cpp/plugins/motionplayer/SeparateLayerAdaptor.h](/root/emote1/AetherKiri/cpp/plugins/motionplayer/SeparateLayerAdaptor.h)。

web 版实现已经承担：

- `targetLayer` 管理
- 私有渲染目标管理
- `assign()` 兼容行为
- target 生命周期与图层属性同步

对应文件见 [cpp/plugins/motionplayer/SeparateLayerAdaptor.cpp](/root/emote1/kirikiroid2-web/cpp/plugins/motionplayer/SeparateLayerAdaptor.cpp)。

这一层是迁移时的必要边界层，不能继续保留 AetherKiri 的简化实现。

## 4. 推荐迁移策略

## 4.1 总体策略

采用“整体移植 web 版 motionplayer，局部适配 AetherKiri 宿主边界”的策略。

不推荐：

- 在 AetherKiri 现有 `Player.h/main.cpp` 上继续堆补丁
- 继续依赖 `missing()` 和 `GenericMockObject` 填坑
- 继续把 `emoteplayer.dll` 作为 `motionplayer.dll` 的别名

推荐：

- 直接移植 web 版 `motionplayer` 目录主体
- 只在以下边界做 AetherKiri 适配：
  - 插件加载流程
  - CMake/链接方式
  - Layer/DrawDevice/RenderManager 能力
  - 平台图形后端抽象

## 4.2 分阶段实施

### 阶段 A: 建立“可编译的完整骨架”

目标：先把 web 版模块结构整体搬进 AetherKiri，并编过。

建议直接迁入的文件：

- `cpp/plugins/motionplayer/EmotePlayer.*`
- `cpp/plugins/motionplayer/Player.h`
- `cpp/plugins/motionplayer/PlayerInternal.h`
- `cpp/plugins/motionplayer/PlayerCore.cpp`
- `cpp/plugins/motionplayer/PlayerResource.cpp`
- `cpp/plugins/motionplayer/PlayerRender.cpp`
- `cpp/plugins/motionplayer/PlayerUpdateLayers.cpp`
- `cpp/plugins/motionplayer/PlayerQuery.cpp`
- `cpp/plugins/motionplayer/RuntimeSupport.*`
- `cpp/plugins/motionplayer/NodeTree.*`
- `cpp/plugins/motionplayer/MotionNode.h`
- `cpp/plugins/motionplayer/MotionNodeBridge.cpp`
- `cpp/plugins/motionplayer/SeparateLayerAdaptor.*`
- `cpp/plugins/motionplayer/SourceCache.h`
- `cpp/plugins/motionplayer/D3DAdaptor.h`
- `cpp/plugins/motionplayer/D3DEmoteModule.h`
- `cpp/plugins/motionplayer/main.cpp`

同时同步修改：

- [cpp/plugins/motionplayer/CMakeLists.txt](/root/emote1/AetherKiri/cpp/plugins/motionplayer/CMakeLists.txt)
- [cpp/plugins/CMakeLists.txt](/root/emote1/AetherKiri/cpp/plugins/CMakeLists.txt)

这一阶段的目标不是跑通，而是把 AetherKiri 当前的简化版实现整体替换掉。

### 阶段 B: 恢复真实插件入口语义

目标：让 `motionplayer.dll` / `emoteplayer.dll` 的行为与 web 版一致。

需要修改：

- [cpp/core/plugin/PluginImpl.cpp](/root/emote1/AetherKiri/cpp/core/plugin/PluginImpl.cpp)
- 删除或收缩 [cpp/plugins/dummy_plugin_stubs.cpp](/root/emote1/AetherKiri/cpp/plugins/dummy_plugin_stubs.cpp) 中与 emote 相关的桩

具体动作：

1. 移除 `emoteplayer.dll -> motionplayer.dll` 的硬编码别名。
2. 让 `motionplayer.dll` 只负责注册 `Motion` 命名空间与基础子类。
3. 新增真实 `emoteplayer.dll` 入口，负责：
   - 注入 `Motion.EmotePlayer`
   - 注入 `Motion.ResourceManager.setEmotePSBDecryptSeed`
   - 注入 `Motion.ResourceManager.setEmotePSBDecryptFunc`
4. 视需要保留 `motionplayer_nod3d.dll` 兼容名，但底层指向同一实现。

### 阶段 C: 适配 AetherKiri 的渲染宿主

目标：把 web 版“名字上叫 D3D、实际可走非 D3D 路线”的接口，映射到 AetherKiri 的真实图形能力。

处理原则：

- 脚本层 API 名称保持原样，不改 `D3DAdaptor`/`D3DEmotePlayer` 名字
- 底层实现改成 AetherKiri 实际后端

这里要做两层适配：

1. `SeparateLayerAdaptor`
   - 用 web 版实现替换 AetherKiri 当前简化版
   - 对齐 `assign/targetLayer/private render target` 语义

2. `D3DAdaptor`
   - 保留脚本接口名
   - 内部转成 GLES/Layer/RenderManager 能力
   - 不能继续返回空 mock 对象

### 阶段 D: 补齐缺失的底层图层能力

这是整个迁移的最高风险点。

优先核对并补齐：

- `BezierPatchCopy`
- `OperateBezierPatch`
- `LayerMeshSupport`/`BezierPatch` 注入链
- `drawAffine` 相关矩阵路径
- `captureCanvas` 路径
- 本地工作层 / scratch layer 生命周期

如果 AetherKiri 底层确实没有 mesh/bezier 支持，建议两步走：

1. 先做“非 mesh 路径可运行”的最小可用版本
2. 再补全 bezier/mesh 渲染

但要明确，这样只能解决部分游戏/部分场景，不能叫“完整迁移”。

### 阶段 E: PSB / ResourceManager / Emote 入口收口

需要对齐的功能：

- `setEmotePSBDecryptSeed`
- `setEmotePSBDecryptFunc`
- `findMotion`
- `loadSource`
- `sourceCandidates`
- `resourceAliases`
- Emote 模式与普通 Motion 模式切换

建议直接复用 web 版 `RuntimeSupport` 与 `ResourceManager` 结构，不要重新设计。

### 阶段 F: 测试迁入与回归验证

建议把 web 版以下内容一并迁入 AetherKiri：

- [tests/unit-tests/plugins/motionplayer-dll.cpp](/root/emote1/kirikiroid2-web/tests/unit-tests/plugins/motionplayer-dll.cpp)
- `tests/test_files/emote/*`

最低验证矩阵：

1. `Plugins.link("motionplayer.dll")` 成功
2. `Plugins.link("emoteplayer.dll")` 成功
3. `Motion.ResourceManager.setEmotePSBDecryptSeed()` 可调用
4. `new Motion.Player(...)` / `new Motion.EmotePlayer(...)` 可创建
5. `findMotion/load/play/progress/draw` 主链路可跑
6. `LayerGetter/variable/timeline` 查询链路可跑
7. logo / title / UI motion 场景可跑
8. 一组真实游戏脚本回归通过

## 5. 具体改动清单

### 5.1 必改文件

- `cpp/plugins/motionplayer/*`
- `cpp/plugins/CMakeLists.txt`
- `cpp/core/plugin/PluginImpl.cpp`

### 5.2 高概率需要改的宿主文件

- `cpp/core/visual/LayerIntf.h`
- `cpp/core/visual/LayerIntf.cpp`
- `cpp/core/visual/RenderManager.*`
- `cpp/plugins/krkrgles.cpp`

目标是补足 motionplayer 依赖的 Layer/Render 能力。

### 5.3 建议新增的测试与文档

- AetherKiri 侧新增 `tests/unit-tests/plugins/motionplayer-dll.cpp`
- 迁入 web 版 motion/emote 分析文档，至少保留：
  - `MotionPlayer_NCB_Registration`
  - `EmotePlayer_Internal_Implementation`
  - `MotionPlayer_EmotePlayer_Misalignment_Report`

## 6. 风险排序

### P0

- `BezierPatch` / mesh 渲染能力缺失
- `emoteplayer.dll` 入口语义错误
- 继续沿用 AetherKiri 当前简化版 `Player/EmotePlayer`

### P1

- `SeparateLayerAdaptor` 生命周期与 owner/target 关系不一致
- `D3DAdaptor` 在 AetherKiri 上仍被 mock
- `drawAffine` / camera / captureCanvas 矩阵链不一致

### P2

- 插件链接方式导致静态注册对象被裁剪
- 平台差异引起 `useD3D/enableD3D` 分支行为不同
- PSB 路径别名、资源缓存策略与 web 版不一致

## 7. 推荐执行顺序

如果要真正落地，建议按这个顺序推进：

1. 先整体替换 AetherKiri 的 `cpp/plugins/motionplayer`
2. 再修正插件入口与 `emoteplayer.dll` 真实注入链
3. 再补宿主渲染接口，优先跑通非 mesh 路径
4. 然后补 bezier/mesh 渲染
5. 最后迁入测试与真实游戏回归

## 8. 最终建议

本任务的正确姿势不是“把 web 版几个方法抄过来”，而是：

- 把 web 版 `motionplayer/emoteplayer` 当成主实现
- 把 AetherKiri 当成宿主平台
- 把渲染桥、插件入口、图层能力当成适配层

只有这样，才有机会做到“完整迁移适配”，而不是继续维持一个能过脚本链接、但无法稳定驱动 E-mote 的兼容壳。
