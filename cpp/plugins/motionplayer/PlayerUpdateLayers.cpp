// PlayerUpdateLayers.cpp — updateLayers 3-phase pipeline + extracted sub-phase methods
// Split from Player.cpp for maintainability.
//
#include "PlayerInternal.h"
#include "ncbind.hpp"    // ncbInstanceAdaptor<Player>::CreateAdaptor for TJS bridge
#include "tjsArray.h"    // TJSCreateArrayObject, TJSGetArrayElementCount
#ifdef __EMSCRIPTEN__
#include <wasm_simd128.h>
#endif

using namespace motion::internal;

namespace {
    inline void copyPackedColorsToBytes(
        uint8_t (&colorBytes)[16],
        const std::array<std::uint32_t, 4> &packedColors) {
        std::memcpy(colorBytes, packedColors.data(), sizeof(std::uint32_t) * 4u);
    }

    inline std::array<std::uint32_t, 4> copyPackedColorsFromBytes(
        const uint8_t (&colorBytes)[16]) {
        std::array<std::uint32_t, 4> packedColors{};
        std::memcpy(packedColors.data(), colorBytes,
                    sizeof(std::uint32_t) * packedColors.size());
        return packedColors;
    }

    inline std::array<int, 4> unpackPackedRgba(std::uint32_t packedColor) {
        return {
            static_cast<int>(packedColor & 0xFFu),
            static_cast<int>((packedColor >> 8) & 0xFFu),
            static_cast<int>((packedColor >> 16) & 0xFFu),
            static_cast<int>((packedColor >> 24) & 0xFFu),
        };
    }

    template <typename StateT>
    inline void populateTransformStateFromFrameState(
        StateT &localState,
        const motion::internal::FrameContentState &state) {
        localState.visible = state.visible;
        localState.active = state.visible;
        localState.flipX = state.flipX;
        localState.flipY = state.flipY;
        localState.posX = state.x;
        localState.posY = state.y;
        localState.posZ = state.z;
        localState.angle = state.angle;
        localState.scaleX = state.scaleX;
        localState.scaleY = state.scaleY;
        localState.slantX = state.slantX;
        localState.slantY = state.slantY;
        localState.opacity = static_cast<int>(
            std::clamp(state.opacity * 255.0, 0.0, 255.0));
        localState.blendMode = state.blendMode;
    }

    // Populate a ClipSlot from a FrameContentState.
    // Cannot be a ClipSlot method because FrameContentState is defined in
    // PlayerInternal.h (motion::internal namespace) which MotionNode.h cannot include.
    inline void populateSlotFromState(
        motion::detail::MotionNode::ClipSlot &slot,
        const motion::internal::FrameContentState &s) {
        slot.done = !s.visible;
        slot.src = s.src;
        slot.srcList = s.srcList;
        slot.x = s.x; slot.y = s.y; slot.z = s.z;
        slot.ox = s.ox; slot.oy = s.oy;
        slot.width = s.width; slot.height = s.height;
        slot.opacity = s.opacity; slot.angle = s.angle;
        slot.scaleX = s.scaleX; slot.scaleY = s.scaleY;
        slot.slantX = s.slantX; slot.slantY = s.slantY;
        slot.flipX = s.flipX; slot.flipY = s.flipY;
        slot.blendMode = s.blendMode;
        slot.packedColors = s.packedColors;
        slot.ccc.x = s.ccc.x; slot.ccc.y = s.ccc.y;
        slot.acc.x = s.acc.x; slot.acc.y = s.acc.y;
        slot.zcc.x = s.zcc.x; slot.zcc.y = s.zcc.y;
        slot.scc.x = s.scc.x; slot.scc.y = s.scc.y;
        slot.occ.x = s.occ.x; slot.occ.y = s.occ.y;
        slot.cc.x = s.cc.x; slot.cc.y = s.cc.y;
        slot.cp.x = s.cp.x; slot.cp.y = s.cp.y; slot.cp.t = s.cp.t;
        slot.hasCpRotation = !s.cp.empty();
        slot.clipStartTime = s.clipStartTime;
        slot.motionDt = s.motionDt; slot.motionFlags = s.motionFlags;
        slot.motionDofst = s.motionDofst; slot.motionDocmpl = s.motionDocmpl;
        slot.motionTimeOffset = s.motionTimeOffset; slot.motionDtgt = s.motionDtgt;
        slot.prtTrigger = s.prtTrigger;
        slot.prtFmin = s.prtFmin; slot.prtF = s.prtF;
        slot.prtVmin = s.prtVmin; slot.prtV = s.prtV;
        slot.prtAmin = s.prtAmin; slot.prtA = s.prtA;
        slot.prtZmin = s.prtZmin; slot.prtZ = s.prtZ;
        slot.prtRange = s.prtRange;
        slot.hasTransformOrder = s.hasTransformOrder;
        std::copy(s.transformOrder, s.transformOrder + 4, slot.transformOrder);
        slot.action = s.action; slot.hasSync = s.hasSync;
        // hasEasing derived from acc curve presence
        slot.hasEasing = !s.acc.empty();
    }
} // anonymous namespace

namespace motion {

    // Phase 1: Camera velocity, root evaluation, variable interpolation
    void Player::updateLayersPhase1_PreLoop(double currentTime) {
        auto &nodes = _runtime->nodes;
        // === PHASE 1: Pre-loop setup ===

        // Camera velocity → root node position (0x6BB360..0x6BB42C)
        // In libkrkr2.so this modifies root node+1592/1600/1608 (posX/Y/Z) before
        // prevPos save. Applied here to root accumulated state.
        {
            auto &rootNode = nodes[0];
            if (_cameraVelocityX != 0.0)
                rootNode.localState.posX += _frameLastTime * _cameraVelocityX;
            if (_cameraVelocityY != 0.0)
                rootNode.localState.posY += _frameLastTime * _cameraVelocityY;
            if (_cameraVelocityZ != 0.0)
                rootNode.localState.posZ += _frameLastTime * _cameraVelocityZ;
            // Camera friction (0x6BB3E0..0x6BB428)
            if (_cameraDamping != 1.0 && _frameLastTime > 0.0) {
                const double dampFactor = std::pow(_cameraDamping,
                                                    _frameLastTime / 60.0);
                _cameraVelocityX *= dampFactor;
                _cameraVelocityY *= dampFactor;
                _cameraVelocityZ *= dampFactor;
            }
        }

        // Step 1: Save previous positions for delta calculation
        for (auto &n : nodes) {
            n.prevPosX = n.accumulated.posX;
            n.prevPosY = n.accumulated.posY;
            n.prevPosZ = n.accumulated.posZ;
        }

        // Step 2: Evaluate root node (index 0)
        auto &root = nodes[0];
        {
            FrameContentState rootState;
            if (root.psbNode) {
                rootState = evaluateLayerContent(root.psbNode, currentTime,
                                                 root.nodeType);
            } else {
                // Aligned to Player_buildNodeTree (0x6B51F0): node 0 is a
                // synthetic root, so it keeps the Player-set transform and a
                // neutral visible state instead of evaluating a PSB frameList.
                rootState.visible = true;
                rootState.opacity = 1.0;
                rootState.scaleX = 1.0;
                rootState.scaleY = 1.0;
                rootState.blendMode = 16;
            }
            // Populate root active clip slot
            populateSlotFromState(root.activeSlot(), rootState);
            root.currentFrameType = rootState.frameType;
            root.stencilType = root.stencilTypeBase | rootState.frameType;
            const double sourcePosX = root.localState.posX;
            const double sourcePosY = root.localState.posY;
            const double sourcePosZ = root.localState.posZ;
            const bool sourceFlipX = root.localState.flipX;
            populateTransformStateFromFrameState(root.localState, rootState);
            // Aligned to Player_updateLayers (0x6BB33C): root working state is
            // rebuilt by memcpy(root+0x5E0, root+0x630, 0x50), so preserve the
            // setter/camera-authored source block before refreshing defaults.
            root.localState.posX = sourcePosX;
            root.localState.posY = sourcePosY;
            root.localState.posZ = sourcePosZ;
            root.localState.flipX = sourceFlipX;
            root.localState.dirty = true;
            root.accumulated.visible = root.localState.visible;
            root.accumulated.flipX = root.localState.flipX;
            root.accumulated.flipY = root.localState.flipY;
            root.accumulated.posX = root.localState.posX;
            root.accumulated.posY = root.localState.posY;
            root.accumulated.posZ = root.localState.posZ;
            root.accumulated.angle = root.localState.angle;
            root.accumulated.scaleX = root.localState.scaleX;
            root.accumulated.scaleY = root.localState.scaleY;
            root.accumulated.slantX = root.localState.slantX;
            root.accumulated.slantY = root.localState.slantY;
            root.accumulated.opacity = root.localState.opacity;
            root.accumulated.blendMode = root.localState.blendMode;
            root.accumulated.active = root.localState.active;
            // Cache interpolated data for rendering
            root.interpolatedCache.src = rootState.src;
            root.interpolatedCache.width = rootState.width;
            root.interpolatedCache.height = rootState.height;
            root.interpolatedCache.opacity = rootState.opacity;
            root.interpolatedCache.x = rootState.x;
            root.interpolatedCache.y = rootState.y;
            root.interpolatedCache.z = rootState.z;
            root.interpolatedCache.ox = rootState.ox;
            root.interpolatedCache.oy = rootState.oy;
            root.interpolatedCache.angle = rootState.angle;
            root.interpolatedCache.scaleX = rootState.scaleX;
            root.interpolatedCache.scaleY = rootState.scaleY;
            root.interpolatedCache.slantX = rootState.slantX;
            root.interpolatedCache.slantY = rootState.slantY;
            root.interpolatedCache.flipX = rootState.flipX ^ _rootFlipX;
            root.interpolatedCache.flipY = rootState.flipY;
            root.interpolatedCache.blendMode = rootState.blendMode;
            root.interpolatedCache.packedColors = rootState.packedColors;
            copyPackedColorsToBytes(root.colorBytes, rootState.packedColors);
            root.interpolatedCache.hasTransformOrder = rootState.hasTransformOrder;
            if (rootState.hasTransformOrder) {
                std::copy(std::begin(rootState.transformOrder),
                          std::end(rootState.transformOrder),
                          root.interpolatedCache.transformOrder);
            }
            root.interpolatedCache.action = rootState.action;
            root.interpolatedCache.hasSync = rootState.hasSync;
            root.interpolatedCache.prtTrigger = rootState.prtTrigger;
            root.interpolatedCache.prtF = rootState.prtF;
            root.interpolatedCache.prtV = rootState.prtV;
            root.interpolatedCache.prtA = rootState.prtA;
            root.interpolatedCache.prtZ = rootState.prtZ;
            root.interpolatedCache.prtRange = rootState.prtRange;

            // Populate root clipW/clipH/originX/originY from PSB icon.
            // Aligned to sub_6BC4F0: node+232/240 = PSB icon pixel dimensions.
            if (!rootState.src.empty() && _runtime->activeMotion) {
                int srcW = 0, srcH = 0;
                double srcOX = 0, srcOY = 0;
                std::vector<std::uint8_t> decomp;
                findPSBResourceBySourceName(*_runtime->activeMotion, rootState.src,
                    srcW, srcH, decomp, srcOX, srcOY);
                root.clipW = srcW;
                root.clipH = srcH;
                root.originX = srcOX;
                root.originY = srcOY;
            }

            // Step 3: Build root local 2x2 matrix via sub_699940
            // Reuse applyLocalTransform logic but on raw 2x2
            Affine2x3 rootAffine = {1.0, 0.0, 0.0, 1.0, 0.0, 0.0};
            applyLocalTransform(rootAffine, root);
            root.accumulated.m11 = rootAffine[0];
            root.accumulated.m21 = rootAffine[1];
            root.accumulated.m12 = rootAffine[2];
            root.accumulated.m22 = rootAffine[3];
        }

        // --- sub_6BBE20: Variable interpolation (pre-loop) ---
        // Aligned to 0x6BBE20. Interpolates variable values and binds to nodes.
        // In libkrkr2.so this operates on a 160-byte item deque (player+1312).
        // Each variable is interpolated then bound to nodes via sub_6C4668.
        //
        // sub_6C4668 binding: resolves variable name to a source entry in
        // player+264 map, then updates child Player timeline parameters for
        // nodeType=3 and nodeType=4 nodes. In our architecture, variable values
        // are stored in _variableValues and exposed via getVariable()/setVariable()
        // TJS API. The binding to child Players happens implicitly when child
        // Players re-evaluate their timelines.
        if (_runtime->activeMotion) {
            const auto &varFrames = _runtime->activeMotion->variableFrames;
            for (const auto &[label, frames] : varFrames) {
                if (frames.empty()) continue;
                // User-set value takes precedence
                if (_variableValues.find(label) != _variableValues.end()) continue;
                // Default: use first frame value
                _variableValues[label] = frames.front().value;
            }
            // Bind variable values to child Players (sub_6C4668 equivalent)
            // For nodeType=3/4 nodes with child Players, propagate variable values
            for (auto &vn : nodes) {
                if (vn.nodeType == 3) {
                    if (auto *cp = vn.getChildPlayer()) {
                        for (const auto &[label, value] : _variableValues)
                            cp->setVariable(detail::widen(label), value);
                    }
                } else if (vn.nodeType == 4) {
                    for (int pi2 = 0; pi2 < vn.getParticleCount(); ++pi2) {
                        if (auto *cp = vn.getParticleChild(pi2)) {
                            for (const auto &[label, value] : _variableValues)
                                cp->setVariable(detail::widen(label), value);
                        }
                    }
                }
            }
        }

    }

    // Phase 2: Main node evaluation loop (non-root nodes)
    void Player::updateLayersPhase2_MainLoop(double currentTime) {
        auto &nodes = _runtime->nodes;
        const std::string motionPath = _runtime->activeMotion
            ? _runtime->activeMotion->path
            : std::string();
        // === PHASE 2: Main loop — evaluate non-root nodes ===
        for (size_t i = 1; i < nodes.size(); ++i) {
            auto &node = nodes[i];

            // Find parent node — walk parentIndex chain while the parent's
            // inheritMask carries the byte(node+42)&0x40 gate used by
            // Player_updateLayers at 0x6BB598..0x6BB5BC.
            int parentIdx = node.parentIndex;
            while (parentIdx > 0 && parentIdx < static_cast<int>(nodes.size())) {
                if ((nodes[parentIdx].inheritFlags & 0x00400000) == 0) break;
                parentIdx = nodes[parentIdx].parentIndex;
            }
            if (parentIdx < 0 || parentIdx >= static_cast<int>(nodes.size()))
                parentIdx = 0;
            const auto &parent = nodes[parentIdx];

            // Evaluate this node's interpolated state
            auto state = evaluateLayerContent(node.psbNode, currentTime,
                                              node.nodeType);
            if(detail::logoChainTraceEnabled(_runtime->activeMotion)
               && state.debugEvaluated) {
                detail::logoChainTraceLogf(
                    motionPath, "updateLayers.phase2.framesel", "0x6926B4/0x699AE4",
                    currentTime,
                    "nodeIndex={} label={} type={} activeIndex={} nextIndex={} frameA[time={:.3f},type={},invisible={},src={},opacity={:.6f},scale=({:.6f},{:.6f})] frameB[time={:.3f},type={},invisible={},src={},opacity={:.6f},scale=({:.6f},{:.6f})] t={:.6f} interpolated={} final[src={},opacity={:.6f},scale=({:.6f},{:.6f})]",
                    node.index,
                    node.layerName.empty() ? std::string("<root>")
                                           : node.layerName,
                    node.nodeType,
                    state.debugActiveIndex,
                    state.debugNextIndex,
                    state.debugFrameATime,
                    state.debugFrameAType,
                    state.debugFrameAInvisible ? 1 : 0,
                    state.debugFrameASrc.empty() ? std::string("<none>")
                                                : state.debugFrameASrc,
                    state.debugFrameAOpacity,
                    state.debugFrameAScaleX,
                    state.debugFrameAScaleY,
                    state.debugFrameBTime,
                    state.debugFrameBType,
                    state.debugFrameBInvisible ? 1 : 0,
                    state.debugFrameBSrc.empty() ? std::string("<none>")
                                                : state.debugFrameBSrc,
                    state.debugFrameBOpacity,
                    state.debugFrameBScaleX,
                    state.debugFrameBScaleY,
                    state.debugInterpT,
                    state.debugInterpolated ? 1 : 0,
                    state.src.empty() ? std::string("<none>") : state.src,
                    state.opacity,
                    state.scaleX,
                    state.scaleY);
            }
            node.currentFrameType = state.frameType;
            // libkrkr2.so uses node+52 both as the PSB-seeded stencil bits
            // (0x6B3C78) and as the non-zero runtime gate consumed by
            // 0x6BD8DC/0x6C2334. Preserve deflector bit 4 while lifting visible
            // frame types (2/3) into the runtime mask.
            node.stencilType = node.stencilTypeBase | state.frameType;

            // Cache interpolated data for rendering
            node.interpolatedCache.src = state.src;
            node.interpolatedCache.srcList = state.srcList;
            node.interpolatedCache.width = state.width;
            node.interpolatedCache.height = state.height;
            node.interpolatedCache.opacity = state.opacity;
            node.interpolatedCache.x = state.x;
            node.interpolatedCache.y = state.y;
            node.interpolatedCache.z = state.z;
            node.interpolatedCache.ox = state.ox;
            node.interpolatedCache.oy = state.oy;
            node.interpolatedCache.angle = state.angle;
            node.interpolatedCache.scaleX = state.scaleX;
            node.interpolatedCache.scaleY = state.scaleY;
            node.interpolatedCache.slantX = state.slantX;
            node.interpolatedCache.slantY = state.slantY;
            node.interpolatedCache.flipX = state.flipX;
            node.interpolatedCache.flipY = state.flipY;
            node.interpolatedCache.blendMode = state.blendMode;
            node.interpolatedCache.packedColors = state.packedColors;
            copyPackedColorsToBytes(node.colorBytes, state.packedColors);
            node.interpolatedCache.hasTransformOrder = state.hasTransformOrder;
            if (state.hasTransformOrder) {
                std::copy(std::begin(state.transformOrder),
                          std::end(state.transformOrder),
                          node.interpolatedCache.transformOrder);
            }
            node.interpolatedCache.action = state.action;
            node.interpolatedCache.hasSync = state.hasSync;
            // Motion sub-object data from FrameContentState (mask 0x80000)
            node.interpolatedCache.motionDt = state.motionDt;
            node.interpolatedCache.motionFlags = state.motionFlags;
            node.interpolatedCache.motionDofst = state.motionDofst;
            node.interpolatedCache.motionDocmpl = state.motionDocmpl;
            node.interpolatedCache.motionTimeOffset = state.motionTimeOffset;
            node.interpolatedCache.clipStartTime = state.clipStartTime;
            node.interpolatedCache.motionDtgt = state.motionDtgt;
            // Particle data from FrameContentState (mask 0x100000)
            node.interpolatedCache.prtTrigger = state.prtTrigger;
            node.interpolatedCache.prtF = state.prtF;
            node.interpolatedCache.prtV = state.prtV;
            node.interpolatedCache.prtA = state.prtA;
            node.interpolatedCache.prtZ = state.prtZ;
            node.interpolatedCache.prtRange = state.prtRange;
            node.prtTrigger = state.prtTrigger;
            // Crossfade easing now stored in ClipSlot via populateSlotFromState.
            // Position easing (ccc) and rotation (cp) for sub_69A4D4 context
            node.interpolatedCache.ccc_x = state.ccc.x;
            node.interpolatedCache.ccc_y = state.ccc.y;
            node.interpolatedCache.cp_x = state.cp.x;
            node.interpolatedCache.cp_y = state.cp.y;
            node.interpolatedCache.cp_t = state.cp.t;
            node.interpolatedCache.hasCpRotation = !state.cp.empty();


            // Populate clipW/clipH and originX/originY from PSB source icon.
            // Aligned to sub_6BC4F0 at 0x6BCB14: node+232/240 = PSB icon
            // pixel dimensions (not state.width/height which are unused).
            // findPSBResourceBySourceName navigates source/<group>/icon/<name>
            // and reads width, height, originX, originY from the icon node.
            if (!state.src.empty() && _runtime->activeMotion) {
                int srcW = 0, srcH = 0;
                double srcOX = 0, srcOY = 0;
                std::vector<std::uint8_t> decomp;
                findPSBResourceBySourceName(*_runtime->activeMotion, state.src,
                    srcW, srcH, decomp, srcOX, srcOY);
                node.clipW = srcW;
                node.clipH = srcH;
                node.originX = srcOX;
                node.originY = srcOY;
            }

            // Populate active clip slot from evaluated state
            populateSlotFromState(node.activeSlot(), state);
            populateTransformStateFromFrameState(node.accumulated, state);
            node.accumulated.dirty = true;

            if (!state.visible) {
                node.accumulated.visible = false;
                node.accumulated.active = false;
                node.accumulated.opacity = 0;
                node.drawFlag = false;
                continue;
            }

            // === Inheritance from parent ===
            // Aligned to libkrkr2.so 0x6BB630..0x6BBB6C (Player_updateLayers main loop)
            // Full inheritFlags system with 3-phase independentLayerInherit support.
            node.accumulated.visible = true;
            node.accumulated.active = true;
            // First-stage composition uses the node's own override/source block
            // (+0x630..+0x678) to modify the evaluated working block
            // (+0x5E0..+0x628), matching 0x6BB630..0x6BB700.
            node.accumulated.flipX ^= node.localState.flipX;
            node.accumulated.flipY ^= node.localState.flipY;
            node.accumulated.angle += node.localState.angle;
            node.accumulated.scaleX *= node.localState.scaleX;
            node.accumulated.scaleY *= node.localState.scaleY;
            node.accumulated.slantX += node.localState.slantX;
            node.accumulated.slantY += node.localState.slantY;
            node.accumulated.opacity =
                node.accumulated.opacity * node.localState.opacity / 255;
            node.accumulated.posX += node.localState.posX;
            node.accumulated.posY += node.localState.posY;
            node.accumulated.posZ += node.localState.posZ;

            // sub_69AE74: Mesh position deformation (0x6BB714)
            // Aligned to 0x69AE74. Called when parent.meshType != 0.
            // Deforms child position based on parent mesh surface.
            // Condition: parent.meshType==1 && (parent.meshFlags & 1) &&
            //            child.active && child.hasSource && parent has mesh vertices.
            if (parent.meshType == 1 && (parent.meshFlags & 1) != 0
                && node.accumulated.active && node.hasSource) {
                // Normalize child position by parent clip dimensions (0x69AF24..0x69AF50)
                const double pw = parent.clipW > 0.0 ? parent.clipW : 1.0;
                const double ph = parent.clipH > 0.0 ? parent.clipH : 1.0;
                const double normX = (node.accumulated.posX + parent.originX) / pw;
                const double normY = (node.accumulated.posY + parent.originY) / ph;

                // sub_69B1E8 → sub_6990A0: 4×4 bicubic Bezier patch evaluation.
                // meshData = 16 control points × 2 floats (X,Y) = 128 bytes at node+2024.
                // Bernstein basis: bu[i] for u, bv[j] for v, sum(bu[i]*bv[j]*P[i*4+j])
                // When no mesh vertex data available, use identity (passthrough).
                auto evalBezierPatch = [](const float *mesh, float u, float v,
                                          float &outX, float &outY) {
                    const float su = 1.0f - u, sv = 1.0f - v;
                    const float bu[4] = {
                        su*su*su, 3.0f*su*su*u, 3.0f*su*u*u, u*u*u
                    };
                    const float bv[4] = {
                        sv*sv*sv, 3.0f*sv*sv*v, 3.0f*sv*v*v, v*v*v
                    };
                    outX = 0; outY = 0;
                    for (int i = 0; i < 16; ++i) {
                        float w = bv[i >> 2] * bu[i & 3];
                        outX += mesh[i * 2] * w;
                        outY += mesh[i * 2 + 1] * w;
                    }
                };

                // Evaluate at normalized coordinates
                float defX = static_cast<float>(normX);
                float defY = static_cast<float>(normY);
                // Evaluate mesh at normalized coordinates using parent's mesh data.
                // parent.meshControlPoints populated by sub_6BC4F0 vertex computation.
                if (parent.meshControlPoints.size() >= 32) {
                    // 16-point Bezier patch: evaluate via sub_6990A0
                    evalBezierPatch(parent.meshControlPoints.data(),
                                    defX, defY, defX, defY);
                }
                node.accumulated.posX = static_cast<double>(defX) * pw - parent.originX;
                node.accumulated.posY = static_cast<double>(defY) * ph - parent.originY;

                // Angle deformation from mesh gradient (0x69AFB4..0x69B0EC)
                if ((parent.meshFlags & 2) != 0
                    && (node.inheritFlags & 0x10) != 0
                    && parent.meshControlPoints.size() >= 32) {
                    const float eps = 0.0001f;
                    const float *mp = parent.meshControlPoints.data();
                    float x1, y1, x2, y2, x3, y3, x4, y4;
                    // Sample at 4 nearby points (0x69B030..0x69B094)
                    evalBezierPatch(mp, defX - eps, defY, x1, y1);
                    evalBezierPatch(mp, defX + eps, defY, x2, y2);
                    evalBezierPatch(mp, defX, defY - eps, x3, y3);
                    evalBezierPatch(mp, defX, defY + eps, x4, y4);
                    // Average of two orthogonal gradients (0x69B0AC..0x69B0EC)
                    double a1 = std::atan2(
                        static_cast<double>(y3 - y4),
                        static_cast<double>(x4 - x3));
                    double a2 = std::atan2(
                        static_cast<double>(x2 - x1),
                        static_cast<double>(y2 - y1));
                    node.accumulated.angle += (a1 + a2) * 0.5 * 360.0 / 6.28318531;
                }

                // Scale deformation from mesh jacobian (0x69B11C..0x69B1A8)
                if ((parent.meshFlags & 4) != 0
                    && (node.inheritFlags & 0x60) != 0
                    && parent.meshControlPoints.size() >= 32) {
                    const float eps = 0.0001f;
                    const float *mp = parent.meshControlPoints.data();
                    float x1, y1, x2, y2, x3, y3, x4, y4;
                    evalBezierPatch(mp, defX - eps, defY, x1, y1);
                    evalBezierPatch(mp, defX + eps, defY, x2, y2);
                    evalBezierPatch(mp, defX, defY - eps, x3, y3);
                    evalBezierPatch(mp, defX, defY + eps, x4, y4);
                    // Jacobian area from cross product (0x69B154..0x69B188)
                    double dx1 = x2 - x1, dy1 = y2 - y1;
                    double dx2 = x3 - x4, dy2 = y3 - y4;
                    double area1 = std::fabs(dx1 * (y4 - y1) - dy1 * (x4 - x1)) * 0.5;
                    double area2 = std::fabs(dx1 * (y3 - y1) - dy1 * (x3 - x1)) * 0.5;
                    double scaleFactor = std::sqrt(area1 + area2 + area2 + area1) / 0.0002;
                    if (node.inheritFlags & 0x020)
                        node.accumulated.scaleX *= scaleFactor;
                    if (node.inheritFlags & 0x040)
                        node.accumulated.scaleY *= scaleFactor;
                }
            }

            // Position transform happens after the parent-local pre-add and mesh
            // deformation. The branch key is parent.coordinateMode (0x6BB718).
            {
                const double localX = node.accumulated.posX;
                const double localY = node.accumulated.posY;
                const double localZ = node.accumulated.posZ;
                if (parent.coordinateMode != 0) {
                    const double worldX = parent.accumulated.m11 * localX
                        + parent.accumulated.m12 * localZ;
                    const double worldZ = parent.accumulated.m21 * localX
                        + parent.accumulated.m22 * localZ;
                    node.accumulated.posX = worldX + parent.accumulated.posX;
                    node.accumulated.posY = localY + parent.accumulated.posY;
                    node.accumulated.posZ = worldZ + parent.accumulated.posZ;
                } else {
                    const double worldX = parent.accumulated.m11 * localX
                        + parent.accumulated.m12 * localY;
                    const double worldY = parent.accumulated.m21 * localX
                        + parent.accumulated.m22 * localY;
                    node.accumulated.posX = worldX + parent.accumulated.posX;
                    node.accumulated.posY = worldY + parent.accumulated.posY;
                    node.accumulated.posZ = localZ + parent.accumulated.posZ;
                }
            }

            // sub_6BAA10: Ground correction TJS callback (0x6BB7F8)
            // Aligned to 0x6BAA10. Called when node+47 (groundCorrection) set.
            // Invokes TJS onGroundCorrection(parentPos, childPos) callback on
            // the node's TJS object. The callback can modify child position.
            // In libkrkr2.so, the TJS object is at *(node+0)+16 (the layer's
            // iTJSDispatch2 reference). In our architecture, MotionNode doesn't
            // hold a TJS dispatch pointer. This callback is used for specialized
            // ground-plane correction in E-mote animations.
            if (node.groundCorrection && node.tjsLayerObject) {
                auto *tjsObj = static_cast<iTJSDispatch2 *>(node.tjsLayerObject);
                // Aligned to sub_6BAA10 (0x6BAA10): invoke TJS onGroundCorrection.
                // Push parent pos [posX,posY,posZ] and child pos as TJS arrays,
                // call onGroundCorrection, read back corrected child position.
                try {
                    // Create parent position array
                    iTJSDispatch2 *parentArr = TJSCreateArrayObject();
                    tTJSVariant pxv(parent.accumulated.posX);
                    tTJSVariant pyv(parent.accumulated.posY);
                    tTJSVariant pzv(parent.accumulated.posZ);
                    tTJSVariant *pargs[] = { &pxv };
                    parentArr->FuncCall(0, TJS_W("add"), nullptr, nullptr, 1, pargs, parentArr);
                    pargs[0] = &pyv;
                    parentArr->FuncCall(0, TJS_W("add"), nullptr, nullptr, 1, pargs, parentArr);
                    pargs[0] = &pzv;
                    parentArr->FuncCall(0, TJS_W("add"), nullptr, nullptr, 1, pargs, parentArr);

                    // Create child position array
                    iTJSDispatch2 *childArr = TJSCreateArrayObject();
                    tTJSVariant cxv(node.accumulated.posX);
                    tTJSVariant cyv(node.accumulated.posY);
                    tTJSVariant czv(node.accumulated.posZ);
                    tTJSVariant *cargs[] = { &cxv };
                    childArr->FuncCall(0, TJS_W("add"), nullptr, nullptr, 1, cargs, childArr);
                    cargs[0] = &cyv;
                    childArr->FuncCall(0, TJS_W("add"), nullptr, nullptr, 1, cargs, childArr);
                    cargs[0] = &czv;
                    childArr->FuncCall(0, TJS_W("add"), nullptr, nullptr, 1, cargs, childArr);

                    // Call onGroundCorrection(parentPos, childPos)
                    tTJSVariant parentVar(parentArr, parentArr);
                    tTJSVariant childVar(childArr, childArr);
                    tTJSVariant *callArgs[] = { &parentVar, &childVar };
                    tTJSVariant result;
                    tjsObj->FuncCall(0, TJS_W("onGroundCorrection"),
                        nullptr, &result, 2, callArgs, tjsObj);

                    // Read back corrected position from result (0x6BAD48..0x6BAE00)
                    if (result.Type() == tvtObject) {
                        iTJSDispatch2 *resObj = result.AsObjectNoAddRef();
                        if (resObj) {
                            tTJSVariant rv;
                            tTJSVariant idx;
                            idx = 0; resObj->PropGetByNum(0, 0, &rv, resObj);
                            node.accumulated.posX = static_cast<double>(rv);
                            idx = 1; resObj->PropGetByNum(0, 1, &rv, resObj);
                            node.accumulated.posY = static_cast<double>(rv);
                            idx = 2; resObj->PropGetByNum(0, 2, &rv, resObj);
                            node.accumulated.posZ = static_cast<double>(rv);
                        }
                    }
                    parentArr->Release();
                    childArr->Release();
                } catch (...) {
                    // TJS callback failure — silently ignore
                }
            }

            // Opacity conditional second multiply (0x6BB808..0x6BB830):
            // Decompilation: if ((v46 & 0x400) != 0 || (v47 = v3, !*(a1+1097)))
            //   node.opacity = v47.opacity * node.opacity / 255
            // v47 = parent when 0x400 set; v47 = root (v3) when !independentLayerInherit
            {
                const auto *opaNode = &parent;
                if ((node.inheritFlags & 0x400) == 0 && _independentLayerInherit) {
                    // Neither 0x400 set nor independentLayerInherit=false: skip
                    // (no second multiply in this case)
                } else {
                    if ((node.inheritFlags & 0x400) != 0)
                        opaNode = &parent;
                    else
                        opaNode = &nodes[0];  // root
                    node.accumulated.opacity = opaNode->accumulated.opacity
                        * node.accumulated.opacity / 255;
                }
            }

            // === inheritFlags per-property control (0x6BB83C) ===
            // Decompilation evidence: Player_updateLayers 0x6BB83C..0x6BBB6C
            //   if ((~v46 & 0x1FC) == 0) → all bits set, simple path
            //   else:
            //     per-property inherit from parent for SET bits
            //     if (player+1097) → LABEL_68: sub_699940 only, NO matrix multiply
            //     else → LABEL_76: root undo → sub_699940 → root re-apply → matrix multiply
            const int flags = node.inheritFlags;
            const bool allInheritBitsSet = (~flags & 0x1FC) == 0;

            if (allInheritBitsSet) {
                // All bits set → simple path (0x6BB848): inherit from parent,
                // sub_699940, matrix multiply. Already inherited above.
                Affine2x3 localAffine = {1.0, 0.0, 0.0, 1.0, 0.0, 0.0};
                applyLocalTransform(localAffine, node);
                const double lm11 = localAffine[0], lm21 = localAffine[1];
                const double lm12 = localAffine[2], lm22 = localAffine[3];
                node.accumulated.m11 = parent.accumulated.m11 * lm11 + parent.accumulated.m12 * lm21;
                node.accumulated.m21 = parent.accumulated.m21 * lm11 + parent.accumulated.m22 * lm21;
                node.accumulated.m12 = parent.accumulated.m11 * lm12 + parent.accumulated.m12 * lm22;
                node.accumulated.m22 = parent.accumulated.m21 * lm12 + parent.accumulated.m22 * lm22;
            } else {
                // Some bits NOT set: per-property inherit from parent for SET bits only
                // (0x6BB8F4..0x6BB918)
                if (flags & 0x004) node.accumulated.flipX = state.flipX ^ parent.accumulated.flipX;
                else               node.accumulated.flipX = state.flipX;
                if (flags & 0x008) node.accumulated.flipY = state.flipY ^ parent.accumulated.flipY;
                else               node.accumulated.flipY = state.flipY;
                if (flags & 0x010) node.accumulated.angle = state.angle + parent.accumulated.angle;
                else               node.accumulated.angle = state.angle;
                if (flags & 0x020) node.accumulated.scaleX = state.scaleX * parent.accumulated.scaleX;
                else               node.accumulated.scaleX = state.scaleX;
                if (flags & 0x040) node.accumulated.scaleY = state.scaleY * parent.accumulated.scaleY;
                else               node.accumulated.scaleY = state.scaleY;
                if (flags & 0x080) node.accumulated.slantX = state.slantX + parent.accumulated.slantX;
                else               node.accumulated.slantX = state.slantX;
                if (flags & 0x100) node.accumulated.slantY = state.slantY + parent.accumulated.slantY;
                else               node.accumulated.slantY = state.slantY;

                if (_independentLayerInherit) {
                    // LABEL_68 (0x6BB918): independentLayerInherit=TRUE
                    // Only sub_699940, NO matrix multiply with parent.
                    // Node's matrix stays as its own local matrix (independent of parent).
                    Affine2x3 localAffine = {1.0, 0.0, 0.0, 1.0, 0.0, 0.0};
                    applyLocalTransform(localAffine, node);
                    node.accumulated.m11 = localAffine[0];
                    node.accumulated.m21 = localAffine[1];
                    node.accumulated.m12 = localAffine[2];
                    node.accumulated.m22 = localAffine[3];
                } else {
                    // LABEL_76 (0x6BB9BC..0x6BBB6C): independentLayerInherit=FALSE
                    // 4-phase: undo root → sub_699940 → re-apply root → matrix multiply
                    const auto &rootNode = nodes[0];

                    // Phase A: For SET bits, UNDO root contribution (0x6BB9BC)
                    if (flags & 0x004) node.accumulated.flipX ^= rootNode.accumulated.flipX;
                    if (flags & 0x008) node.accumulated.flipY ^= rootNode.accumulated.flipY;
                    if (flags & 0x010) node.accumulated.angle -= rootNode.accumulated.angle;
                    if (flags & 0x020 && rootNode.accumulated.scaleX != 0.0)
                        node.accumulated.scaleX /= rootNode.accumulated.scaleX;
                    if (flags & 0x040 && rootNode.accumulated.scaleY != 0.0)
                        node.accumulated.scaleY /= rootNode.accumulated.scaleY;
                    if (flags & 0x080) node.accumulated.slantX -= rootNode.accumulated.slantX;
                    if (flags & 0x100) node.accumulated.slantY -= rootNode.accumulated.slantY;

                    // Phase B: sub_699940 (0x6BB9E8)
                    Affine2x3 localAffine = {1.0, 0.0, 0.0, 1.0, 0.0, 0.0};
                    applyLocalTransform(localAffine, node);

                    // Phase C: For SET bits, RE-APPLY root contribution (0x6BBA04)
                    if (flags & 0x004) node.accumulated.flipX ^= rootNode.accumulated.flipX;
                    if (flags & 0x008) node.accumulated.flipY ^= rootNode.accumulated.flipY;
                    if (flags & 0x010) node.accumulated.angle += rootNode.accumulated.angle;
                    if (flags & 0x020) node.accumulated.scaleX *= rootNode.accumulated.scaleX;
                    if (flags & 0x040) node.accumulated.scaleY *= rootNode.accumulated.scaleY;
                    if (flags & 0x080) node.accumulated.slantX += rootNode.accumulated.slantX;
                    if (flags & 0x100) node.accumulated.slantY += rootNode.accumulated.slantY;

                    // Phase D: Matrix multiply parent × local (0x6BBA24)
                    const double lm11 = localAffine[0], lm21 = localAffine[1];
                    const double lm12 = localAffine[2], lm22 = localAffine[3];
                    node.accumulated.m11 = parent.accumulated.m11 * lm11 + parent.accumulated.m12 * lm21;
                    node.accumulated.m21 = parent.accumulated.m21 * lm11 + parent.accumulated.m22 * lm21;
                    node.accumulated.m12 = parent.accumulated.m11 * lm12 + parent.accumulated.m12 * lm22;
                    node.accumulated.m22 = parent.accumulated.m21 * lm12 + parent.accumulated.m22 * lm22;
                }
            }
        }

    }

    void Player::updateLayersPhase3_CameraConstraint() {
        auto &nodes = _runtime->nodes;
        // --- sub_6BC000: Camera constraint (nodeType=9) ---
        // Aligned to 0x6BC000..0x6BC4EC. Only when !isEmoteMode.
        // 9 cases at 0x6BC1B0..0x6BC358 based on flipX/flipY + constraintType (node+2376).
        if (!_runtime->isEmoteMode && nodes.size() >= 2) {
            double offsetX = 0, offsetY = 0, offsetZ = 0;
            // Track which axes have constraints and their types
            bool hasMinX = false, hasMaxX = false, hasTrackX = false;
            bool hasMinY = false, hasMaxY = false, hasTrackY = false;
            bool hasMinZ = false, hasMaxZ = false, hasTrackZ = false;
            double minX = 3.4e38, maxX = -3.4e38, trackX = 0;
            double minY = 3.4e38, maxY = -3.4e38, trackY = 0;
            double minZ = 3.4e38, maxZ = -3.4e38, trackZ = 0;

            for (size_t ci = 1; ci < nodes.size(); ++ci) {
                auto &cn = nodes[ci];
                if (cn.nodeType != 9 || cn.activeSlot().done || !cn.accumulated.active) continue;

                // Target node: root (node 0). Full impl would look up dtgt.
                const auto &target = nodes[0];

                // Compute constraintType with flip adjustment (0x6BC1B0..0x6BC1FC)
                int ctype = cn.cameraConstraintType;
                if (cn.accumulated.flipX) {
                    if (ctype == 0) ctype = 2;
                    else if (ctype == 2) ctype = 0;
                }
                if (cn.accumulated.flipY) {
                    if (ctype == 3) ctype = 5;
                    else if (ctype == 5) ctype = 3;
                }

                // 9 cases (0x6BC224..0x6BC358)
                switch (ctype) {
                    case 0: { // X min constraint
                        double d = target.accumulated.posX - cn.accumulated.posX;
                        if (d < 0 && d < minX) { minX = d; hasMinX = true; }
                        break;
                    }
                    case 1: { // X direct track
                        trackX = target.accumulated.posX - cn.accumulated.posX;
                        hasTrackX = true;
                        break;
                    }
                    case 2: { // X max constraint
                        double d = target.accumulated.posX - cn.accumulated.posX;
                        if (d > 0 && d > maxX) { maxX = d; hasMaxX = true; }
                        break;
                    }
                    case 3: { // Y min constraint
                        double d = target.accumulated.posY - cn.accumulated.posY;
                        if (d < 0 && d < minY) { minY = d; hasMinY = true; }
                        break;
                    }
                    case 4: { // Y direct track
                        trackY = target.accumulated.posY - cn.accumulated.posY;
                        hasTrackY = true;
                        break;
                    }
                    case 5: { // Y max constraint
                        double d = target.accumulated.posY - cn.accumulated.posY;
                        if (d > 0 && d > maxY) { maxY = d; hasMaxY = true; }
                        break;
                    }
                    case 6: { // Z min constraint
                        double d = target.accumulated.posZ - cn.accumulated.posZ;
                        if (d < 0 && d < minZ) { minZ = d; hasMinZ = true; }
                        break;
                    }
                    case 7: { // Z direct track
                        trackZ = target.accumulated.posZ - cn.accumulated.posZ;
                        hasTrackZ = true;
                        break;
                    }
                    case 8: { // Z max constraint
                        double d = target.accumulated.posZ - cn.accumulated.posZ;
                        if (d > 0 && d > maxZ) { maxZ = d; hasMaxZ = true; }
                        break;
                    }
                    default: break;
                }
            }
            // Resolve final offset per axis (0x6BC398..0x6BC410)
            // Priority: track > max > min > 0
            if (hasTrackX) offsetX = trackX;
            else if (hasMaxX) offsetX = maxX;
            else if (hasMinX) offsetX = minX;
            if (hasTrackY) offsetY = trackY;
            else if (hasMaxY) offsetY = maxY;
            else if (hasMinY) offsetY = minY;
            if (hasTrackZ) offsetZ = trackZ;
            else if (hasMaxZ) offsetZ = maxZ;
            else if (hasMinZ) offsetZ = minZ;

            // Apply offset to all nodes (0x6BC450..0x6BC4BC)
            if (offsetX != 0 || offsetY != 0 || offsetZ != 0) {
                for (size_t ci = 1; ci < nodes.size(); ++ci) {
                    nodes[ci].accumulated.posX += offsetX;
                    nodes[ci].accumulated.posY += offsetY;
                    nodes[ci].accumulated.posZ += offsetZ;
                }
            }
        }

    }

    void Player::updateLayersPhase3_VertexComputation() {
        auto &nodes = _runtime->nodes;
        // --- sub_6BC4F0: Vertex computation ---
        // Aligned to 0x6BC4F0. Full implementation matching decompilation.
        for (size_t vi = 1; vi < nodes.size(); ++vi) {
            auto &vn = nodes[vi];
            const int parentIdx = vn.parentIndex >= 0 ? vn.parentIndex : 0;
            auto &parentNode = nodes[parentIdx];
            const int slotIdx = 0;  // current slot index

            // priorDraw flag from emoteEdit (0x6BC648..0x6BC6C4)
            // priorDraw from emoteEdit (0x6BC648..0x6BC6C4)
            if (vn.forceVisible && vn.emoteEditDict) {
                // sub_6636D4: read bool "priorDraw" from emoteEdit dict
                auto pdVal = (*vn.emoteEditDict)["priorDraw"];
                if (auto num = std::dynamic_pointer_cast<PSB::PSBNumber>(pdVal))
                    vn.priorDraw = num->getValue<int>();  // keep raw int — bit flags checked via (v12 & 5)
                else if (auto bl = std::dynamic_pointer_cast<PSB::PSBBool>(pdVal))
                    vn.priorDraw = bl->value ? 1 : 0;
                else
                    vn.priorDraw = 0;
            } else {
                vn.priorDraw = 0;  // 0x6BC67C
            }

            // Parent clip chain: node+1962/1963 flags (0x6BC6E4..0x6BC818)
            // node+1962 = has mesh data, node+1963 = mesh combine enabled
            // parentClipIndex propagated by sub_6BDCC0 carries the ancestor chain
            // Set mesh flags: hasMeshData when meshType!=0 and control points exist;
            // meshCombineEnabled when mesh is active for child deformation.
            // These flags gate the visibleAncestor conditional in sub_6BE0C0 (label_18).
            vn.hasMeshData = (vn.meshType != 0 && !vn.meshControlPoints.empty());
            vn.meshCombineEnabled = (vn.hasMeshData && vn.meshType == 1 && (vn.meshFlags & 1) != 0);

            // Check visible (0x6BC700..0x6BC74C)
            if (!vn.accumulated.visible) {
                // Walk parent for mesh flag
                goto bc4f0_next;
            }

            // Propagate clip origin
            vn.clipOriginX = vn.interpolatedCache.ox;
            vn.clipOriginY = vn.interpolatedCache.oy;

            // nodeType 1/5 special position via parent mesh chain (0x6BC828..0x6BC8D4)
            // if ((1 << nodeType) & 0x22) != 0 → nodeType 1 (shape) or 5 (camera)
            if (((1 << vn.nodeType) & 0x22) != 0) {
                double px = vn.accumulated.posX;
                double py = vn.accumulated.posY;
                // Walk parent clip chain, evaluate through each mesh (0x6BC838..0x6BC8B0)
                int clipWalk = vn.parentClipIndex;
                while (clipWalk >= 0 && clipWalk < static_cast<int>(nodes.size())) {
                    auto &cn = nodes[clipWalk];
                    if (cn.meshControlPointsPrev.size() >= 32) {
                        // Apply inverse matrix to get normalized coords (0x6BC858..0x6BC87C)
                        float tx = static_cast<float>(px) + cn.meshInvOffX;
                        float ty = static_cast<float>(py) + cn.meshInvOffY;
                        float ix = static_cast<float>(
                            cn.meshInvM11 * tx + cn.meshInvM12 * ty);
                        float iy = static_cast<float>(
                            cn.meshInvM21 * tx + cn.meshInvM22 * ty);
                        // Evaluate bezier patch at normalized coords (sub_69B1E8)
                        const float *mesh = cn.meshControlPointsPrev.data();
                        const float su = 1.f - ix, sv = 1.f - iy;
                        const float bu[4] = {su*su*su, 3.f*su*su*ix, 3.f*su*ix*ix, ix*ix*ix};
                        const float bv[4] = {sv*sv*sv, 3.f*sv*sv*iy, 3.f*sv*iy*iy, iy*iy*iy};
                        float ox = 0, oy = 0;
                        for (int bi = 0; bi < 16; ++bi) {
                            float w = bv[bi >> 2] * bu[bi & 3];
                            ox += mesh[bi * 2] * w;
                            oy += mesh[bi * 2 + 1] * w;
                        }
                        px = ox;
                        py = oy;
                    }
                    clipWalk = cn.parentClipIndex;
                }
                vn.vertexPosX = px;
                vn.vertexPosY = py;
                vn.vertexPosZ = vn.accumulated.posZ;
            }

            // Non slot-done path: vertex computation (0x6BC8DC..0x6BD730)
            if (!vn.activeSlot().done) {
                // Second visibility bitmask check (0x6BCE2C..0x6BCE40)
                // Non-emote: 7233 = 0x1C41, Emote: 7241 = 0x1C49
                const int vbm = _runtime->isEmoteMode ? 7241 : 7233;
                const bool vertexEligible = vn.forceVisible
                    || ((vbm & (1 << vn.nodeType)) != 0);

                if (vertexEligible && vn.hasSource) {
                    const double m11 = vn.accumulated.m11, m12 = vn.accumulated.m12;
                    const double m21 = vn.accumulated.m21, m22 = vn.accumulated.m22;
                    const double posX = vn.accumulated.posX;
                    const double posY = vn.accumulated.posY
                        + vn.accumulated.posZ * _zFactor;

                    // Origin offset (0x6BCB58..0x6BCBA4)
                    const double totalOX = vn.originX + vn.clipOriginX;
                    const double totalOY = vn.originY + vn.clipOriginY;
                    const double orgX = posX - (m12 * totalOY + totalOX * m11);
                    const double orgY = posY - (totalOY * m22 + totalOX * m21);
                    vn.vertexPosX = orgX;
                    vn.vertexPosY = orgY;
                    vn.vertexPosZ = vn.accumulated.posZ;

                    // Save prev mesh (0x6BCB94..0x6BCBAC)
                    vn.meshControlPointsPrev = vn.meshControlPoints;

                    const double cw = vn.clipW;
                    const double ch = vn.clipH;

                    // Mesh vertex construction (0x6BCBBC..0x6BD060)
                    if (vn.meshType == 1
                        && !vn.meshControlPoints.empty()
                        && cw > 0 && ch > 0) {
                        // meshType=1: Bezier patch mesh
                        // Compute inverse matrix for mesh (0x6BCBF8..0x6BCC38)
                        // Compute and store inverse matrix (0x6BCBF8..0x6BCC38)
                        // det = m11*cw * m22*ch - m12*ch * m21*cw
                        const double mw11 = m11 * cw, mw12 = m12 * ch;
                        const double mw21 = m21 * cw, mw22 = m22 * ch;
                        const double det = mw11 * mw22 - mw12 * mw21;
                        if (std::fabs(det) > 1e-10) {
                            // node+2096..2120: inverse of [mw11,mw12;mw21,mw22]
                            vn.meshInvM11 = mw22 / det;   // 0x6BCC0C
                            vn.meshInvM12 = -(mw12 / det); // 0x6BCC20
                            vn.meshInvM21 = -(mw21 / det); // 0x6BCC34
                            vn.meshInvM22 = mw11 / det;    // 0x6BCC14
                            // node+2128/2132: negated origin as float (0x6BCC04/0x6BCC38)
                            vn.meshInvOffX = -static_cast<float>(orgX);
                            vn.meshInvOffY = -static_cast<float>(orgY);
                        }

                        // Build grid via sub_6BAF68 (0x6BCF6C)
                        // Grid dimensions: divX = meshDivision * cw/(cw+ch) + 1
                        int divTotal = vn.meshDivision;
                        if (divTotal > 50) divTotal = 50;
                        if (divTotal < 1) divTotal = 4;
                        const int divX = static_cast<int>(
                            static_cast<double>(divTotal) * cw / (cw + ch)) + 1;
                        const int divY = divTotal - divX + 2;
                        const int numPts = divX * divY;
                        // Store grid dimensions (node+2012/2016, 0x6BCF5C)
                        vn.meshDivX = divX;
                        vn.meshDivY = divY;

                        // sub_6BAF68: build bilinear grid (0x6BAF68)
                        // NEON version at 0x6BB030..0x6BB138 processes 4 points/iteration.
                        // Each row interpolates linearly between two edge points:
                        //   p0 = orgXY + m_col2*ch*tv, p1 = orgXY + m_col1*cw + m_col2*ch*tv
                        //   grid[gx] = lerp(p0, p1, gx/divX)
                        vn.meshControlPoints.resize(numPts * 2);
                        for (int gy = 0; gy < divY; ++gy) {
                            const double tv = (divY > 1) ? static_cast<double>(gy) / (divY - 1) : 0;
                            // Row edge points (0x6BB068..0x6BB09C)
                            const double rowBaseX = orgX + (m12 * ch) * tv;
                            const double rowBaseY = orgY + (m22 * ch) * tv;
                            const double rowEndX = rowBaseX + m11 * cw;
                            const double rowEndY = rowBaseY + m21 * cw;
                            float *rowPtr = &vn.meshControlPoints[gy * divX * 2];
#ifdef __EMSCRIPTEN__
                            // WASM SIMD: process 4 grid points per iteration
                            // Aligned to NEON at 0x6BB0CC..0x6BB138
                            // For each group of 4 gx values: tu = [gx, gx+1, gx+2, gx+3] / divX
                            // ptX = rowBaseX*(1-tu) + rowEndX*tu
                            // ptY = rowBaseY*(1-tu) + rowEndY*tu
                            const v128_t vBaseX = wasm_f64x2_splat(rowBaseX);
                            const v128_t vBaseY = wasm_f64x2_splat(rowBaseY);
                            const v128_t vEndX = wasm_f64x2_splat(rowEndX);
                            const v128_t vEndY = wasm_f64x2_splat(rowEndY);
                            const double invDivX = (divX > 1) ? 1.0 / (divX - 1) : 0.0;
                            int gx = 0;
                            const int simdEnd = divX & ~1;  // process 2 at a time (f64x2)
                            for (; gx < simdEnd; gx += 2) {
                                const double t0 = gx * invDivX;
                                const double t1 = (gx + 1) * invDivX;
                                const v128_t vt = wasm_f64x2_make(t0, t1);
                                const v128_t v1mt = wasm_f64x2_sub(wasm_f64x2_splat(1.0), vt);
                                // X = base*(1-t) + end*t
                                v128_t vx = wasm_f64x2_add(
                                    wasm_f64x2_mul(vBaseX, v1mt),
                                    wasm_f64x2_mul(vEndX, vt));
                                // Y = base*(1-t) + end*t
                                v128_t vy = wasm_f64x2_add(
                                    wasm_f64x2_mul(vBaseY, v1mt),
                                    wasm_f64x2_mul(vEndY, vt));
                                // Convert f64→f32 and store interleaved [x0,y0,x1,y1]
                                float fx0 = static_cast<float>(wasm_f64x2_extract_lane(vx, 0));
                                float fy0 = static_cast<float>(wasm_f64x2_extract_lane(vy, 0));
                                float fx1 = static_cast<float>(wasm_f64x2_extract_lane(vx, 1));
                                float fy1 = static_cast<float>(wasm_f64x2_extract_lane(vy, 1));
                                rowPtr[gx*2]   = fx0;
                                rowPtr[gx*2+1] = fy0;
                                rowPtr[gx*2+2] = fx1;
                                rowPtr[gx*2+3] = fy1;
                            }
                            // Scalar remainder
                            for (; gx < divX; ++gx) {
                                const double tu = (divX > 1) ? static_cast<double>(gx) / (divX-1) : 0;
                                rowPtr[gx*2]   = static_cast<float>(rowBaseX*(1-tu) + rowEndX*tu);
                                rowPtr[gx*2+1] = static_cast<float>(rowBaseY*(1-tu) + rowEndY*tu);
                            }
#else
                            for (int gx = 0; gx < divX; ++gx) {
                                const double tu = (divX > 1) ? static_cast<double>(gx) / (divX-1) : 0;
                                rowPtr[gx*2]   = static_cast<float>(rowBaseX*(1-tu) + rowEndX*tu);
                                rowPtr[gx*2+1] = static_cast<float>(rowBaseY*(1-tu) + rowEndY*tu);
                            }
#endif
                        }

                        // Evaluate each grid point through Bezier patch (0x6BCF80..0x6BCFBC)
                        // sub_69B1E8 evaluates bezier patch at each mesh point
                        // This transforms the bilinear grid into a deformed mesh
                        if (vn.meshControlPointsPrev.size() >= 32) {
                            auto evalBP = [](const float *mesh, float u, float v,
                                             float &outX, float &outY) {
                                const float su=1.f-u, sv=1.f-v;
                                const float bu[4]={su*su*su,3.f*su*su*u,3.f*su*u*u,u*u*u};
                                const float bv[4]={sv*sv*sv,3.f*sv*sv*v,3.f*sv*v*v,v*v*v};
                                outX=0; outY=0;
                                for(int i=0;i<16;++i){
                                    float w=bv[i>>2]*bu[i&3];
                                    outX+=mesh[i*2]*w; outY+=mesh[i*2+1]*w;
                                }
                            };
                            for (int pi = 0; pi < numPts; ++pi) {
                                float px = vn.meshControlPoints[pi*2];
                                float py = vn.meshControlPoints[pi*2+1];
                                evalBP(vn.meshControlPointsPrev.data(), px, py, px, py);
                                vn.meshControlPoints[pi*2] = px;
                                vn.meshControlPoints[pi*2+1] = py;
                            }
                        }

                        // Parent clip chain mesh cascade (0x6BD118..0x6BD380)
                        // Walk node+1968 (parentClipIndex), for each mesh-enabled
                        // ancestor: evaluate all mesh points + origin through its mesh
                        // Parent clip chain mesh cascade (0x6BD118..0x6BD380)
                        auto evalBPCascade = [](const float *mesh, float u, float v,
                                                float &outX, float &outY) {
                            const float su=1.f-u, sv=1.f-v;
                            const float bu[4]={su*su*su,3.f*su*su*u,3.f*su*u*u,u*u*u};
                            const float bv[4]={sv*sv*sv,3.f*sv*sv*v,3.f*sv*v*v,v*v*v};
                            outX=0; outY=0;
                            for(int i=0;i<16;++i){
                                float w=bv[i>>2]*bu[i&3];
                                outX+=mesh[i*2]*w; outY+=mesh[i*2+1]*w;
                            }
                        };
                        int clipWalk = vn.parentClipIndex;
                        double cascadeOrgX = orgX, cascadeOrgY = orgY;
                        while (clipWalk >= 0 && clipWalk < static_cast<int>(nodes.size())) {
                            auto &cn = nodes[clipWalk];
                            if (cn.meshControlPoints.size() >= 32) {
                                const float *cmesh = cn.meshControlPoints.data();
                                // Evaluate each mesh point through parent mesh (0x6BD148..0x6BD1E8)
                                for (size_t mi = 0; mi < vn.meshControlPoints.size() / 2; ++mi) {
                                    float mpx = vn.meshControlPoints[mi*2];
                                    float mpy = vn.meshControlPoints[mi*2+1];
                                    // Transform by parent inverse matrix + offset (0x6BD188)
                                    // Transform by parent inverse matrix + offset (0x6BD188)
                                    float tx = mpx + cn.meshInvOffX;  // node+2128
                                    float ty = mpy + cn.meshInvOffY;  // node+2132
                                    // Apply inverse matrix: [invM11,invM12;invM21,invM22] × (tx,ty)
                                    float ix = static_cast<float>(cn.meshInvM11 * tx + cn.meshInvM12 * ty);
                                    float iy = static_cast<float>(cn.meshInvM21 * tx + cn.meshInvM22 * ty);
                                    tx = ix; ty = iy;
                                    // Evaluate through parent bezier (sub_69B1E8)
                                    float rx, ry;
                                    evalBPCascade(cmesh, tx, ty, rx, ry);
                                    vn.meshControlPoints[mi*2] = rx;
                                    vn.meshControlPoints[mi*2+1] = ry;
                                }
                                // Evaluate origin through parent mesh (0x6BD218..0x6BD258)
                                float cox = static_cast<float>(cascadeOrgY) + cn.meshInvOffY;
                                float coy = static_cast<float>(cascadeOrgX) + cn.meshInvOffX;
                                float rox, roy;
                                evalBPCascade(cmesh, coy, cox, rox, roy);
                                cascadeOrgX = rox;
                                cascadeOrgY = roy;
                                _processedMeshVerticesNum += static_cast<int>(
                                    vn.meshControlPoints.size() / 2) + 1;
                            }
                            clipWalk = cn.parentClipIndex;
                        }
                        // Update origin if cascade changed it (0x6BD330..0x6BD380)
                        if (cascadeOrgX != orgX || cascadeOrgY != orgY) {
                            vn.vertexPosX = cascadeOrgX;
                            vn.vertexPosY = cascadeOrgY;
                            // Offset all mesh points by delta (0x6BD360..0x6BD380)
                            const float fdx = static_cast<float>(cascadeOrgX - orgX);
                            const float fdy = static_cast<float>(cascadeOrgY - orgY);
                            const size_t totalFloats = vn.meshControlPoints.size();
                            float *mp = vn.meshControlPoints.data();
#ifdef __EMSCRIPTEN__
                            // WASM SIMD: process 4 floats at a time (2 XY pairs)
                            // Aligned to NEON at 0x6BD360: vadd with delta vector
                            const v128_t vdelta = wasm_f32x4_make(fdx, fdy, fdx, fdy);
                            size_t fi = 0;
                            for (; fi + 4 <= totalFloats; fi += 4) {
                                v128_t pts = wasm_v128_load(&mp[fi]);
                                pts = wasm_f32x4_add(pts, vdelta);
                                wasm_v128_store(&mp[fi], pts);
                            }
                            // Scalar remainder
                            for (; fi < totalFloats; fi += 2) {
                                mp[fi] += fdx;
                                if (fi + 1 < totalFloats) mp[fi+1] += fdy;
                            }
#else
                            for (size_t mi = 0; mi < totalFloats / 2; ++mi) {
                                mp[mi*2] += fdx;
                                mp[mi*2+1] += fdy;
                            }
#endif
                        }
                    }

                    // 4-corner vertex output (0x6BCE44..0x6BCEC0)
                    {
                        const double fx = vn.vertexPosX;
                        const double fy = vn.vertexPosY;
                        vn.vertices[0] = static_cast<float>(fx);
                        vn.vertices[1] = static_cast<float>(fy);
                        vn.vertices[2] = static_cast<float>(fx + m11*cw);
                        vn.vertices[3] = static_cast<float>(fy + m21*cw);
                        vn.vertices[4] = static_cast<float>(fx + m11*cw + m12*ch);
                        vn.vertices[5] = static_cast<float>(fy + m21*cw + m22*ch);
                        vn.vertices[6] = static_cast<float>(fx + m12*ch);
                        vn.vertices[7] = static_cast<float>(fy + m22*ch);
                        if(detail::logoChainTraceEnabled(_runtime->activeMotion)) {
                            const auto motionPath = _runtime->activeMotion->path;
                            const std::array<float, 8> expectedVertices = {
                                static_cast<float>(fx),
                                static_cast<float>(fy),
                                static_cast<float>(fx + m11 * cw),
                                static_cast<float>(fy + m21 * cw),
                                static_cast<float>(fx + m11 * cw + m12 * ch),
                                static_cast<float>(fy + m21 * cw + m22 * ch),
                                static_cast<float>(fx + m12 * ch),
                                static_cast<float>(fy + m22 * ch)
                            };
                            bool ok = true;
                            for(size_t vi = 0; vi < expectedVertices.size(); ++vi) {
                                if(std::fabs(vn.vertices[vi] - expectedVertices[vi]) >
                                   0.01f) {
                                    ok = false;
                                    break;
                                }
                            }
                            detail::logoChainTraceCheck(
                                motionPath, "updateLayers.phase3.vertices",
                                "0x6BC4F0", _clampedEvalTime,
                                fmt::format(
                                    "pos=({:.3f},{:.3f}) clip=({:.3f},{:.3f}) m=({:.6f},{:.6f},{:.6f},{:.6f}) exp=[{:.3f},{:.3f},{:.3f},{:.3f},{:.3f},{:.3f},{:.3f},{:.3f}]",
                                    fx, fy, cw, ch, m11, m12, m21, m22,
                                    expectedVertices[0], expectedVertices[1],
                                    expectedVertices[2], expectedVertices[3],
                                    expectedVertices[4], expectedVertices[5],
                                    expectedVertices[6], expectedVertices[7]),
                                fmt::format(
                                    "src={} act=[{:.3f},{:.3f},{:.3f},{:.3f},{:.3f},{:.3f},{:.3f},{:.3f}]",
                                    vn.interpolatedCache.src.empty()
                                        ? std::string("<none>")
                                        : vn.interpolatedCache.src,
                                    vn.vertices[0], vn.vertices[1],
                                    vn.vertices[2], vn.vertices[3],
                                    vn.vertices[4], vn.vertices[5],
                                    vn.vertices[6], vn.vertices[7]),
                                ok,
                                "sub_6BC4F0 vertex output diverged from expected corners");
                        }
                    }

                    // forceVisible TJS property writing (0x6BD38C..0x6BD72C)
                    // When node+1996 (forceVisible) is set, write node properties
                    // to a TJS dictionary for sub-motion evaluation.
                    // forceVisible TJS property writing (0x6BD38C..0x6BD72C)
                    // Write node properties to TJS dict for sub-motion evaluation.
                    if (vn.forceVisible && vn.tjsLayerObject) {
                        auto *tjsObj = static_cast<iTJSDispatch2 *>(vn.tjsLayerObject);
                        try {
                            // "c" array: [posX, posY] (0x6BD480..0x6BD494)
                            tTJSVariant posXv(vn.vertexPosX);
                            tTJSVariant posYv(vn.vertexPosY);
                            // "mtx" array: [m11,m12,m21,m22] (0x6BD534..0x6BD570)
                            tTJSVariant m11v(m11), m12v(m12), m21v(m21), m22v(m22);
                            // "width" (0x6BD590)
                            tTJSVariant wv(cw);
                            tjsObj->PropSet(TJS_MEMBERENSURE, TJS_W("width"),
                                nullptr, &wv, tjsObj);
                            // "height" (0x6BD5B0)
                            tTJSVariant hv(ch);
                            tjsObj->PropSet(TJS_MEMBERENSURE, TJS_W("height"),
                                nullptr, &hv, tjsObj);
                            // "originX" (0x6BD5E4)
                            tTJSVariant oxv(totalOX);
                            tjsObj->PropSet(TJS_MEMBERENSURE, TJS_W("originX"),
                                nullptr, &oxv, tjsObj);
                            // "originY" (0x6BD618)
                            tTJSVariant oyv(totalOY);
                            tjsObj->PropSet(TJS_MEMBERENSURE, TJS_W("originY"),
                                nullptr, &oyv, tjsObj);
                            // "flipX" (0x6BD638)
                            tTJSVariant fxv(static_cast<tjs_int>(vn.accumulated.flipX));
                            tjsObj->PropSet(TJS_MEMBERENSURE, TJS_W("flipX"),
                                nullptr, &fxv, tjsObj);
                            // "flipY" (0x6BD658)
                            tTJSVariant fyv(static_cast<tjs_int>(vn.accumulated.flipY));
                            tjsObj->PropSet(TJS_MEMBERENSURE, TJS_W("flipY"),
                                nullptr, &fyv, tjsObj);
                            // "zoomX" (0x6BD678)
                            tTJSVariant zxv(vn.accumulated.scaleX);
                            tjsObj->PropSet(TJS_MEMBERENSURE, TJS_W("zoomX"),
                                nullptr, &zxv, tjsObj);
                            // "zoomY" (0x6BD698)
                            tTJSVariant zyv(vn.accumulated.scaleY);
                            tjsObj->PropSet(TJS_MEMBERENSURE, TJS_W("zoomY"),
                                nullptr, &zyv, tjsObj);
                            // "slantX" (0x6BD6B8)
                            tTJSVariant sxv(vn.accumulated.slantX);
                            tjsObj->PropSet(TJS_MEMBERENSURE, TJS_W("slantX"),
                                nullptr, &sxv, tjsObj);
                            // "angle" (0x6BD6D8)
                            tTJSVariant av(vn.accumulated.angle);
                            tjsObj->PropSet(TJS_MEMBERENSURE, TJS_W("angle"),
                                nullptr, &av, tjsObj);
                        } catch (...) {}
                    }
                }
            }
            bc4f0_next:;
        }

        // Delta position computation (0x6BBB74..0x6BBC54)
        // if playing (player+480): delta = 0; else: delta = currentPos - prevPos
        {
            bool anyPlaying = std::any_of(
                _runtime->timelines.begin(), _runtime->timelines.end(),
                [](const auto &e) { return e.second.playing; });
            for (size_t di = 1; di < nodes.size(); ++di) {
                auto &dn = nodes[di];
                if (anyPlaying) {
                    dn.deltaPosX = 0; dn.deltaPosY = 0; dn.deltaPosZ = 0;
                } else {
                    dn.deltaPosX = dn.accumulated.posX - dn.prevPosX;
                    dn.deltaPosY = dn.accumulated.posY - dn.prevPosY;
                    dn.deltaPosZ = dn.accumulated.posZ - dn.prevPosZ;
                }
            }
        }

    }

    void Player::updateLayersPhase3_Visibility() {
        auto &nodes = _runtime->nodes;
        // Visibility flags — aligned to sub_6BD8DC at 0x6BD8DC.
        // Root node (index 0) is always visible.
        if (!nodes.empty()) {
            nodes[0].drawFlag = nodes[0].accumulated.visible && nodes[0].hasSource;
        }
        // Visibility bitmask: which nodeTypes can render
        // Non-emote: 6145 = 0x1801 → nodeTypes 0, 11, 12
        // Emote:     6153 = 0x1809 → nodeTypes 0, 3, 11, 12
        // Aligned to sub_6BD8DC (0x6BD8DC): visibility bitmask depends on emote mode.
        const int visBitmask = _runtime->isEmoteMode ? 6153 : 6145;
        for (size_t i = 1; i < nodes.size(); ++i) {
            auto &node = nodes[i];

            // Find visible ancestor (walk parent chain, 0x6BD9D8)
            int pIdx = node.parentIndex;
            if (pIdx >= 0 && pIdx < static_cast<int>(nodes.size())) {
                if (!nodes[pIdx].drawFlag) {
                    node.visibleAncestorIndex = nodes[pIdx].visibleAncestorIndex;
                } else {
                    node.visibleAncestorIndex = pIdx;
                }
            }

            // Visibility logic — exact replica of sub_6BD8DC (0x6BD958..0x6BDA00):
            //   if (slotDone) { v9 = 0; }
            //   else { v9 = stencilType; if (v9) { v9 = active; if (v9) {
            //     if (forceVisible || (bitmask & (1<<nodeType))) v9 = hasSource; } } }
            //   drawFlag = v9;
            if (node.activeSlot().done) {
                node.drawFlag = false;
            } else if (node.stencilType == 0) {
                // node+52 == 0 → invisible (0x6BD958)
                node.drawFlag = false;
            } else if (!node.accumulated.active) {
                node.drawFlag = false;
            } else if (node.forceVisible
                       || (visBitmask & (1 << node.nodeType)) != 0) {
                node.drawFlag = node.hasSource;
            } else {
                // Active node, not in renderable bitmask, not forceVisible:
                // v9 stays as active (non-zero) → drawFlag = true
                node.drawFlag = true;
            }
        }

    }

    void Player::updateLayersPhase3_CameraNode() {
        auto &nodes = _runtime->nodes;
        // Camera node processing — aligned to sub_6BDA28 (0x6BDA28).
        // Find first nodeType=5 (camera) that is active, compute cameraOffset.
        _hasCamera = false;
        for (size_t i = 1; i < nodes.size(); ++i) {
            const auto &camNode = nodes[i];
            if (camNode.nodeType != 5 || !camNode.accumulated.active) continue;
            _hasCamera = true;

            // Compute delta from root node position
            const auto &rootAcc = nodes[0].accumulated;
            const double dx = -(camNode.accumulated.posX - rootAcc.posX);
            const double dy = -(camNode.accumulated.posY * _zFactor
                + camNode.accumulated.posZ
                - (rootAcc.posY * _zFactor + rootAcc.posZ));

            // Transform by drawAffineMatrix (player+808..832)
            const auto &dam = _runtime->drawAffineMatrix;
            _cameraOffsetX = static_cast<float>(
                static_cast<int>(dam[0] * dx + dam[2] * dy + 0.5));
            _cameraOffsetY = static_cast<float>(
                static_cast<int>(dam[1] * dx + dam[3] * dy + 0.5));

            // Camera-to-target angle (0x6BDC04..0x6BDCB0)
            // When stereovisionActive (a1+1094): compute camera angle for 3D effect.
            if (_stereovisionActive) {
                // Store camera/target positions (a1+72..112)
                _cameraPosX = camNode.accumulated.posX;
                _cameraPosY = camNode.accumulated.posY;
                _cameraPosZ = camNode.accumulated.posZ;
                // Look up target node via clip slot action path
                // For now, target defaults to previous positions
                // Compute angle: atan2(camPosZ - targetZ, camPosX - targetX)
                double angleRad = std::atan2(
                    camNode.accumulated.posZ - _cameraTargetZ,
                    camNode.accumulated.posX - _cameraTargetX);
                double angleDeg = angleRad * -57.2957795 + 90.0;
                while (angleDeg < 0.0) angleDeg += 360.0;
                while (angleDeg >= 360.0) angleDeg -= 360.0;
                _cameraAngle = angleDeg;  // a1+472
                _cameraTargetX = _cameraPosX;
                _cameraTargetY = _cameraPosY;
                _cameraTargetZ = _cameraPosZ;
            }
            break;  // only first camera node
        }

    }

    void Player::updateLayersPhase3_ShapeAABB() {
        auto &nodes = _runtime->nodes;
        // --- sub_6BDCC0: Shape AABB computation (nodeType=7) ---
        // Aligned to 0x6BDCC0. For nodeType=7 active nodes, compute AABB
        // from 2x2 matrix × 16-unit extent, origin offset, parent clip clamping.
        for (size_t si = 1; si < nodes.size(); ++si) {
            auto &sn = nodes[si];
            // Propagate parent clip region (node+1936)
            if (sn.parentIndex >= 0 && sn.parentIndex < static_cast<int>(nodes.size())) {
                sn.parentClipIndex = nodes[sn.parentIndex].parentClipIndex;
            }
            if (sn.nodeType != 7 || !sn.accumulated.active) continue;

            const double m11 = sn.accumulated.m11, m12 = sn.accumulated.m12;
            const double m21 = sn.accumulated.m21, m22 = sn.accumulated.m22;
            const double px = sn.accumulated.posX, py = sn.accumulated.posY;
            const double pzs = sn.accumulated.posZ * _zFactor + py;
            const double ox = sn.clipOriginX, oy = sn.clipOriginY;
            const double oox = ox * m11 + oy * m12;
            const double ooy = ox * m21 + oy * m22;
            // Extent = matrix × 16
            const double ex1 = m11 * 16.0, ex2 = m12 * 16.0;
            const double ey1 = m21 * 16.0, ey2 = m22 * 16.0;
            double xMin = px - ex1 - ex2 - oox;
            double xMax = px + ex1 + ex2 - oox;
            double yMin = pzs - ey1 - ey2 - ooy;
            double yMax = pzs + ey1 + ey2 - ooy;
            if (xMin > xMax) std::swap(xMin, xMax);
            if (yMin > yMax) std::swap(yMin, yMax);
            sn.shapeAABB[0] = static_cast<float>(xMin);
            sn.shapeAABB[1] = static_cast<float>(yMin);
            sn.shapeAABB[2] = static_cast<float>(xMax);
            sn.shapeAABB[3] = static_cast<float>(yMax);
            // Clamp to parent clip (0x6BDE40..0x6BDE80)
            if (sn.parentClipIndex >= 0 &&
                sn.parentClipIndex < static_cast<int>(nodes.size())) {
                const auto &pc = nodes[sn.parentClipIndex];
                if (pc.shapeAABB[0] > sn.shapeAABB[0]) sn.shapeAABB[0] = pc.shapeAABB[0];
                if (pc.shapeAABB[1] > sn.shapeAABB[1]) sn.shapeAABB[1] = pc.shapeAABB[1];
                if (pc.shapeAABB[2] < sn.shapeAABB[2]) sn.shapeAABB[2] = pc.shapeAABB[2];
                if (pc.shapeAABB[3] < sn.shapeAABB[3]) sn.shapeAABB[3] = pc.shapeAABB[3];
            }
            sn.parentClipIndex = static_cast<int>(si);
        }

    }

    void Player::updateLayersPhase3_ShapeGeometry() {
        auto &nodes = _runtime->nodes;
        // --- sub_6BDE94: Shape geometry computation (nodeType=1) ---
        // Aligned to 0x6BDE94. For nodeType=1 nodes with active slot,
        // compute shape vertices based on shapeType (0=point,1=circle,2=rect,3=quad).
        for (size_t si = 1; si < nodes.size(); ++si) {
            auto &sn = nodes[si];
            if (sn.nodeType != 1 || sn.activeSlot().done) continue;
            sn.shapeGeomType = sn.shapeType;
            switch (sn.shapeType) {
                case 0: // point (0x6BDF40)
                    sn.shapeVertices[0] = sn.vertexPosX;
                    sn.shapeVertices[1] = sn.vertexPosY;
                    break;
                case 1: { // circle (0x6BDF50)
                    sn.shapeVertices[0] = sn.vertexPosX;
                    sn.shapeVertices[1] = sn.vertexPosY;
                    sn.shapeVertices[2] = sn.accumulated.scaleX * 16.0 * 0.5;
                    break;
                }
                case 2: { // rect (0x6BDF70)
                    const double hw = sn.accumulated.scaleX * 16.0 * 0.5;
                    const double hh = sn.accumulated.scaleY * 16.0 * 0.5;
                    sn.shapeVertices[3] = sn.vertexPosX - hw;
                    sn.shapeVertices[4] = sn.vertexPosY - hh;
                    sn.shapeVertices[5] = sn.vertexPosX + hw;
                    sn.shapeVertices[6] = sn.vertexPosY + hh;
                    break;
                }
                case 3: { // quad (0x6BDFA8)
                    const double m11 = sn.accumulated.m11, m12 = sn.accumulated.m12;
                    const double m21 = sn.accumulated.m21, m22 = sn.accumulated.m22;
                    const double ox = sn.clipOriginX, oy = sn.clipOriginY;
                    const double oox = ox * m11 + oy * m12;
                    const double ooy = ox * m21 + oy * m22;
                    const double px = sn.vertexPosX, py = sn.vertexPosY;
                    const double ax = m11 * -8.0, bx = m12 * -8.0;
                    const double cx = m11 * 8.0,  dx = m12 * 8.0;
                    const double ay = m21 * -8.0, by = m22 * -8.0;
                    const double cy = m21 * 8.0,  dy = m22 * 8.0;
                    sn.shapeVertices[7]  = px + ax + bx - oox;
                    sn.shapeVertices[8]  = py + ay + by - ooy;
                    sn.shapeVertices[9]  = px + cx + bx - oox;
                    sn.shapeVertices[10] = py + cy + by - ooy;
                    sn.shapeVertices[11] = px + cx + dx - oox;
                    sn.shapeVertices[12] = py + cy + dy - ooy;
                    sn.shapeVertices[13] = px + ax + dx - oox;
                    sn.shapeVertices[14] = py + ay + dy - ooy;
                    break;
                }
                default: break;
            }
        }

    }

    // Helper: find node by label in the node tree (sub_6F2228 equivalent)
    // Aligned to sub_6F2228: std::map<ttstr,int> lookup at player+24.
    // Binary uses red-black tree traversal with wcscmp; we use std::map::find.
    static int findNodeByLabel(const std::map<std::string, int> &labelMap,
                               const std::string &label) {
        auto it = labelMap.find(label);
        return (it != labelMap.end()) ? it->second : -1;
    }

    void Player::updateLayersPhase3_MotionSubNode(double currentTime) {
        auto &nodes = _runtime->nodes;
        // Motion sub-node processing — aligned to sub_6BE0C0 (0x6BE0C0).
        // For each nodeType=3 (Motion) node, create/manage child Player instance.
        // Only runs when !isEmoteMode (0x6BE104).
        if (_runtime->isEmoteMode) return;

        for (size_t i = 1; i < nodes.size(); ++i) {
            auto &mn = nodes[i];
            if (mn.nodeType != 3) continue;

            // Get parent's priorDraw flag as play trigger (v12, 0x6BE204..0x6BE214)
            // In libkrkr2.so: v12 = *(int*)(parentObj+48) where parentObj = node+8 or player+47*8
            int v12 = 0;
            if (mn.tjsLayerObject) {
                v12 = mn.priorDraw;  // keep raw int value, don't truncate to 0/1
            } else {
                v12 = _priorDraw;    // keep raw int value
            }

            // Get child Player via TJS dispatch (0x6BE220..0x6BE260)
            // Aligned to binary: node+1912 → NativeInstanceSupport → native Player*
            // Child Player is pre-created in buildNodeTree (sub_6B3C78 case 3).
            {
            Player *childPtr = mn.getChildPlayer();
            if (!childPtr) {
                goto label_18;
            }
            Player &child = *childPtr;

            // If no v12 flags and not visible → skip to LABEL_18 (0x6BE270)
            if (!v12 && !mn.accumulated.visible) {
                goto label_18;
            }

            // Check slotDone → clear child (0x6BE31C..0x6BE354)
            // Binary: calls cleanup (sub_6C0DE8, sub_6B56F8), releases TJS variants,
            // then goes to LABEL_3 (next loop iteration), SKIPPING frameProgress/updateLayers.
            if (mn.activeSlot().done) {
                // Binary cleanup at 0x6BE328..0x6BE354:
                // 1. child._allplaying = false (player+1099)
                // 2. sub_6C0DE8(child+1296) — resets timeline keyframe cache
                // 3. sub_6B56F8(child) — releases layer IDs for all non-root nodes,
                //    clears nodes (except root), resets label map
                // 4. Release TJS variants at child+984 and child+976
                child._allplaying = false;
                if (child._runtime) {
                    // sub_6C0DE8: reset timeline keyframe cache
                    child._runtime->timelines.clear();
                    // sub_6B56F8: release layer IDs for non-root nodes, then clear
                    child._runtime->layerIdsByName.clear();
                    child._runtime->layerNamesById.clear();
                    child._runtime->nodeLabelMap.clear();
                    // Keep root node but clear the rest (sub_6B56F8 at 0x6B59E0)
                    if (child._runtime->nodes.size() > 1) {
                        child._runtime->nodes.resize(1);
                    }
                    child._runtime->nodesBuilt = false;
                }
                continue;  // skip to next iteration — binary goes to LABEL_3, not LABEL_18
            }

            {
                // Get motion source from clip slot (0x6BE364)
                const auto &src = mn.activeSlot().src;
                if (!src.empty()) {
                    // Re-init gate: (v12 & 5) != 0 || mn.flags (0x6BE37C)
                    if ((v12 & 5) != 0 || (mn.flags & 0x01)) {
                        mn.flags |= 0x01; // mark as initialized (0x6BE388)

                        // Binary does NOT flip activeSlotIndex here (0x6BE21C reads it
                        // once and uses it unchanged throughout). Slot flip is managed
                        // elsewhere in the clip evaluation pipeline.

                        // Resolve motion and play (0x6BE3B4..0x6BE46C)
                        // Binary: splits src by "/" via sub_697D34.
                        // 1-element (no "/"): sub_6B29C0(child, 0, src) + Player_play(child, flags, src)
                        //   → setChara(src), play the motion named src
                        // 2-element ("chara/motion"): sub_6B29C0(child, 0, split[1]) + Player_play(child, flags, split[2])
                        //   → setChara(split[0]=chara part), play motion split[1]
                        {
                            auto slashPos = src.find('/');
                            if (slashPos == std::string::npos) {
                                // Single segment: binary sets chara to src itself
                                // then Player_play with raw src (no "/" prefix)
                                child.setChara(detail::widen(src));
                                child.onFindMotion(detail::widen(src),
                                                   mn.activeSlot().motionFlags | v12);
                            } else {
                                // Multi-segment: "chara/motion" format
                                // Binary: setChara(chara), Player_play(motion)
                                std::string charaPart = src.substr(0, slashPos);
                                std::string motionPart = src.substr(slashPos + 1);
                                child.setChara(detail::widen(charaPart));
                                child.onFindMotion(detail::widen(motionPart),
                                                   mn.activeSlot().motionFlags | v12);
                            }
                        }
                        // Stealth motion (0x6BE41C..0x6BE44C): binary reads from
                        // CHILD player+776, plays with flag 16, then clears child+776.
                        if (!child._stealthMotion.IsEmpty()) {
                            child.onFindMotion(child._stealthMotion, PlayFlagStealth);
                            child._stealthMotion.Clear();
                        }


                        // Time sync from parent loop time (0x6BE478..0x6BE4E8)
                        // Binary checks both _allplaying && _queuing (0x6BE478)
                        if (child._allplaying && child._queuing) {
                            // Binary at 0x6BE49C: childTime = player+1120 - slot+8 + slot+376
                            // = _frameLoopTime - clipStartTime + motionTimeOffset
                            double childTime = _frameLoopTime
                                - mn.activeSlot().clipStartTime
                                + mn.activeSlot().motionTimeOffset;
                            if (_frameLastTime < 0.0) {
                                // Backward play: handle loop wrapping
                                // Binary reads child+1136 (_loopTime) and child+1128 (_cachedTotalFrames)
                                double loopEnd = child._loopTime;
                                if (loopEnd >= 0.0) {
                                    double totalFrames = child._cachedTotalFrames;
                                    while (childTime >= totalFrames)
                                        childTime = childTime - totalFrames + loopEnd;
                                }
                            }
                            // Binary reads player+1128 directly (0x6BE4CC)
                            double totalFrames = child._cachedTotalFrames;
                            childTime = std::max(childTime, 0.0);
                            // Binary: writes unclamped time to player+1120 (0x6BE4D4)
                            child._frameLoopTime = childTime;
                            if (childTime > totalFrames) childTime = totalFrames;
                            // Binary: writes clamped time to player+456 (0x6BE4E4)
                            child._clampedEvalTime = childTime;
                            // Binary at 0x6BE4E8: writes word 0x0101 to child+480,
                            // setting both _queuing (byte+480) and _allplaying (byte+481)
                            // simultaneously. Does NOT iterate timelines.
                            child._allplaying = true;
                            child._queuing = true;
                            // Binary: if (!*(byte*)(v4 + 480)) — checks _queuing (0x6BE4EC)
                            if (!_queuing) {
                                child._needsInternalAssignImages = true;
                            }
                        }
                    }
                }

                // Binary at 0x6BE534 unconditionally proceeds to angle/state
                // propagation (no activeMotion guard). Only guard for null runtime.
                if (!child._runtime) goto label_18;

                // === Angle interpolation (0x6BE534..0x6BEC9C) ===
                int angleMode = mn.activeSlot().motionDt;
                bool hasAngle = false;
                double computedAngle = 0.0;
                const double dofst = mn.activeSlot().motionDofst;

                // Dual-slot crossfade angle interpolation (0x6BE85C..0x6BEC9C)
                // When crossfading between two clip slots, blend dofst (v37) between
                // old and new slot values using time-based ratio.
                double v37 = dofst;
                if (mn.activeSlot().motionDocmpl
                    && mn.activeSlot().crossfading
                    && !mn.otherSlot().done
                    && mn.otherSlot().motionDt != 0) {
                    // Binary at 0x6BE864: uses node+8+40 (per-node eval time) if
                    // available, else player+456 (_clampedEvalTime). NOT _frameLoopTime.
                    // Binary: *(node+8+40) — per-node eval time from player+384 array.
                    // Falls back to player+456 (_clampedEvalTime) if node+8 is null.
                    double parentTime = (i < _runtime->perNodeEvalData.size())
                        ? _runtime->perNodeEvalData[i].evalTime : _clampedEvalTime;
                    double currentStart = mn.activeSlot().clipStartTime;
                    double otherStart = mn.otherSlot().clipStartTime;
                    double denom = otherStart - currentStart;
                    // Binary divides directly without denom guard (0x6BEC6C)
                    double ratio = (parentTime - currentStart) / denom;
                    // Binary at 0x6BEC74: only checks hasEasing (slot+544).
                    if (mn.activeSlot().hasEasing) {
                        ratio = evaluateBezierCurve(mn.activeSlot().acc, ratio);
                    }
                    // Binary does NOT clamp ratio to [0,1] (0x6BEC9C).
                    double otherDofst = mn.otherSlot().motionDofst;
                    // Wrap angle difference > 180 degrees for shortest-path interpolation
                    if (dofst >= otherDofst) {
                        if (dofst - otherDofst > 180.0) otherDofst += 360.0;
                    } else {
                        if (otherDofst - dofst > 180.0) otherDofst -= 360.0;
                    }
                    v37 = otherDofst * ratio + dofst * (1.0 - ratio);
                    // Normalize to [0, 360)
                    if (v37 < 0.0) v37 += 360.0;
                    if (v37 >= 360.0) v37 -= 360.0;
                }

                if (angleMode != 0) {
                    // Case 2→3 fallthrough: binary at 0x6BE664 checks child player+608
                    // (_noUpdateYet). If set, case 2 falls through to LABEL_83 (case 3
                    // logic) because on the first frame there's no delta position yet.
                    int effectiveMode = angleMode;
                    if (angleMode == 2 && child._noUpdateYet) {
                        effectiveMode = 3;  // fallthrough to case 3 (0x6BE664→0x6BE668)
                    }

                    switch (effectiveMode) {
                    case 1: // Direct angle (0x6BE5BC)
                        // Binary does NOT normalize case 1 to [0,360).
                        computedAngle = dofst + mn.accumulated.angle;
                        hasAngle = true;
                        break;
                    case 2: { // atan2 from delta position (0x6BE8C4)
                        // Binary uses v37 (potentially interpolated) not raw dofst
                        double dy_comp, dx_comp;
                        if (mn.coordinateMode == 1) {
                            dy_comp = mn.deltaPosZ; // node+192
                            dx_comp = mn.deltaPosX; // node+176
                        } else if (mn.coordinateMode == 0) {
                            dy_comp = mn.deltaPosY; // node+184
                            dx_comp = mn.deltaPosX; // node+176
                        } else {
                            // Binary: non-0/non-1 coordinateMode → LABEL_129
                            hasAngle = true;
                            break;
                        }
                        computedAngle = v37 + std::atan2(dy_comp, dx_comp) * 360.0 / 6.28318531;
                        hasAngle = true;
                        break;
                    }
                    case 3: { // Interpolated atan2 (LABEL_83: 0x6BE668..0x6BE79C)
                        // Binary: guard: crossfading && !otherSlotDone (0x6BE680).
                        // If guard fails → hasAngle=false (LABEL_119).
                        // Otherwise: compute ratio from parent time, call sub_69A4D4
                        // twice (at t and t+0.0001) for finite-difference derivative,
                        // then atan2 on delta based on coordinateMode.
                        if (!mn.activeSlot().crossfading
                            || mn.otherSlot().done) {
                            // Guard fails → LABEL_119: hasAngle=false
                            break;
                        }
                        // Parent time (0x6BE688..0x6BE6B0): node+8 ? *(node+8)+40 : player+456
                        // Binary: *(node+8+40) — per-node eval time from player+384 array.
                    // Falls back to player+456 (_clampedEvalTime) if node+8 is null.
                    double parentTime = (i < _runtime->perNodeEvalData.size())
                        ? _runtime->perNodeEvalData[i].evalTime : _clampedEvalTime;
                        double currentStart = mn.activeSlot().clipStartTime;
                        double otherStart = mn.otherSlot().clipStartTime;
                        double denom = otherStart - currentStart;
                        // Binary divides directly without zero guard (0x6BE6D0)
                        double ratio = (parentTime - currentStart) / denom;
                        double t2 = ratio + 0.0001;
                        if (t2 >= 1.0) ratio = 0.9999;
                        t2 = std::min(t2, 1.0);
                        // sub_69A4D4: interpolate between slot positions.
                        // src = currentSlot+96 = current evaluated position
                        // dst = otherSlot+96 = position from before crossfade
                        const auto &slot = mn.activeSlot();
                        BezierCurve cccCurve;
                        cccCurve.x = slot.ccc.x; cccCurve.y = slot.ccc.y;
                        ControlPointCurve cpCurve;
                        if (slot.hasCpRotation) {
                            cpCurve.x = slot.cp.x; cpCurve.y = slot.cp.y;
                            cpCurve.t = slot.cp.t;
                        }
                        // Use crossfade slot positions: src=current, dst=other (saved at flip)
                        // Binary reads full {x,y,z} from active slot (a3+96..112).
                        double src[3] = {slot.x, slot.y, mn.activeSlot().z};
                        double dst[3] = {mn.otherSlot().x, mn.otherSlot().y, mn.otherSlot().z};
                        double out1[3] = {}, out2[3] = {};
                        interpolatePosition69A4D4(cccCurve, dst, src, out1, mn.coordinateMode, cpCurve, ratio);
                        interpolatePosition69A4D4(cccCurve, dst, src, out2, mn.coordinateMode, cpCurve, t2);
                        // Pick dx/dy based on coordinateMode (0x6BE72C..0x6BE740)
                        double dx_comp, dy_comp;
                        if (mn.coordinateMode == 1) {
                            dx_comp = out2[0] - out1[0]; dy_comp = out2[2] - out1[2];
                        } else if (mn.coordinateMode == 0) {
                            dx_comp = out2[0] - out1[0]; dy_comp = out2[1] - out1[1];
                        } else {
                            hasAngle = true;
                            break; // LABEL_129
                        }
                        computedAngle = v37 + std::atan2(dy_comp, dx_comp) * 360.0 / 6.28318531;
                        hasAngle = true;
                        break;
                    }
                    case 4: { // Target node lookup (0x6BE7B4)
                        // Binary: hasAngle is only set to true when target found
                        // and angle computed. LABEL_119 sets hasAngle=false.
                        const auto &dtgt = mn.activeSlot().motionDtgt;
                        if (dtgt.empty()) break; // LABEL_119: hasAngle=false
                        int targetIdx = findNodeByLabel(_runtime->nodeLabelMap, dtgt);
                        if (targetIdx < 0) break; // LABEL_119: hasAngle=false
                        const auto &target = nodes[targetIdx];
                        double dy_comp, dx_comp;
                        if (mn.coordinateMode == 1) {
                            dy_comp = target.accumulated.posZ - mn.accumulated.posZ;
                            dx_comp = target.accumulated.posX - mn.accumulated.posX;
                        } else if (mn.coordinateMode == 0) {
                            dy_comp = target.accumulated.posY - mn.accumulated.posY;
                            dx_comp = target.accumulated.posX - mn.accumulated.posX;
                        } else {
                            hasAngle = true; // LABEL_129
                            break;
                        }
                        computedAngle = v37 + std::atan2(dy_comp, dx_comp) * 360.0 / 6.28318531;
                        hasAngle = true;
                        break;
                    }
                    default: break; // LABEL_119: hasAngle=false
                    }
                    // Binary normalizes per-case (cases 2,3,4 each have inline loops).
                    // Case 1 does NOT normalize. Skip normalization for case 1.
                    if (effectiveMode != 1) {
                        while (computedAngle < 0.0) computedAngle += 360.0;
                        while (computedAngle >= 360.0) computedAngle -= 360.0;
                    }
                }

                // === Origin offset (0x6BE994..0x6BE9F4) ===
                double posX = mn.accumulated.posX;
                double posY = mn.accumulated.posY;
                double posZ = mn.accumulated.posZ;

                const double originX = mn.activeSlot().ox;
                const double originY = mn.activeSlot().oy;
                if (originX != 0.0 || originY != 0.0) {
                    const double negOY = -originY;
                    // v79 = m12*negOY - originX*m11 (0x6BE9E0)
                    const double vx = mn.accumulated.m12 * negOY - originX * mn.accumulated.m11;
                    // v80 = m22*negOY - originX*m21 (0x6BE9E4)
                    const double vy = mn.accumulated.m22 * negOY - originX * mn.accumulated.m21;
                    if (mn.coordinateMode == 1) {
                        posX += vx;
                        posZ += vy;
                    } else {
                        posX += vx;
                        posY += vy;
                    }
                }

                // === State propagation to child root node (0x6BEA18..0x6BEB74) ===
                if (child._runtime && !child._runtime->nodes.empty()) {
                    auto &cr = child._runtime->nodes[0];
                    cr.accumulated.posX = posX;
                    cr.accumulated.posY = posY;
                    cr.accumulated.posZ = posZ;
                    // Flip — only write if changed, set dirty (0x6BEA28..0x6BEA54)
                    if (cr.accumulated.flipX != mn.accumulated.flipX ||
                        cr.accumulated.flipY != mn.accumulated.flipY) {
                        cr.accumulated.flipX = mn.accumulated.flipX;
                        cr.accumulated.flipY = mn.accumulated.flipY;
                        cr.accumulated.dirty = true;
                    }
                    // Scale — only write if changed, set dirty (0x6BEA5C..0x6BEA88)
                    if (cr.accumulated.scaleX != mn.accumulated.scaleX ||
                        cr.accumulated.scaleY != mn.accumulated.scaleY) {
                        cr.accumulated.scaleX = mn.accumulated.scaleX;
                        cr.accumulated.scaleY = mn.accumulated.scaleY;
                        cr.accumulated.dirty = true;
                    }
                    // Slant — set dirty if changed (0x6BEB10..0x6BEB3C)
                    if (cr.accumulated.slantX != mn.accumulated.slantX ||
                        cr.accumulated.slantY != mn.accumulated.slantY) {
                        cr.accumulated.slantX = mn.accumulated.slantX;
                        cr.accumulated.slantY = mn.accumulated.slantY;
                        cr.accumulated.dirty = true;
                    }
                    // Opacity — set dirty if changed (0x6BEB40..0x6BEB58)
                    if (cr.accumulated.opacity != mn.accumulated.opacity) {
                        cr.accumulated.opacity = mn.accumulated.opacity;
                        cr.accumulated.dirty = true;
                    }
                    // Active — set dirty if changed (0x6BEB5C..0x6BEB74)
                    if (cr.accumulated.active != mn.accumulated.active) {
                        cr.accumulated.active = mn.accumulated.active;
                        cr.accumulated.dirty = true;
                    }
                    // Parent color propagation (0x6BEB7C)
                    // Binary: *(_DWORD *)(v16 + 1156) = *(_DWORD *)(v10 + 100)
                    // Reads node+100 (colorBytes[0..3] packed as uint32 RGBA), writes to
                    // child player+1156 (_parentColorPacked). NOT a blend mode field.
                    {
                        uint32_t packed;
                        std::memcpy(&packed, &mn.colorBytes[0], sizeof(uint32_t));
                        child._parentColorPacked = packed;
                    }

                    // isEmoteMode check + zFactor (0x6BEA90..0x6BEA94)
                    child._zFactor = _zFactor;
                    // Binary at 0x6BEA98: if isEmoteMode, call Player_initEmoteMotion(child, 2)
                    // This syncs emote bone state. Emote mode is not used in web port.

                    // === Angle → child (0x6BEAA8..0x6BEB08) ===
                    if (hasAngle) {
                        if (child._runtime->isEmoteMode) {
                            // Emote mode: normalize angle [0,360), set player+464, reinit
                            double k = computedAngle;
                            while (k < 0.0) k += 360.0;
                            while (k >= 360.0) k -= 360.0;
                            // player+464 = emote angle (not mapped in web port)
                            // Player_initEmoteMotion(child, 2) — N/A for web
                        } else {
                            if (cr.accumulated.angle != computedAngle) {
                                cr.accumulated.angle = computedAngle;
                                cr.accumulated.dirty = true;
                            }
                        }
                    }

                    // === Matrix propagation (0x6BEB9C..0x6BEC4C) ===
                    // Binary at 0x6BEB90: condition is hasAngle || angle==accAngle || child._directEdit
                    // (player+482). When _directEdit is true, direct-copy path is taken.
                    if (hasAngle || computedAngle == mn.accumulated.angle ||
                        child._directEdit) {
                        // Direct copy (0x6BEB9C)
                        cr.accumulated.m11 = mn.accumulated.m11;
                        cr.accumulated.m12 = mn.accumulated.m12;
                        cr.accumulated.m21 = mn.accumulated.m21;
                        cr.accumulated.m22 = mn.accumulated.m22;
                    } else {
                        // Rotate by (computedAngle - accumulated.angle) (0x6BEBC8..0x6BEC4C)
                        double delta = (computedAngle - mn.accumulated.angle)
                                       * 3.14159265 * 2.0 / 360.0;
                        if (mn.accumulated.flipX != mn.accumulated.flipY)
                            delta = -delta;
                        const double c = std::cos(delta);
                        const double s = std::sin(delta);
                        cr.accumulated.m11 = c * mn.accumulated.m11 + s * mn.accumulated.m12;
                        cr.accumulated.m12 = c * mn.accumulated.m12 - mn.accumulated.m11 * s;
                        cr.accumulated.m21 = c * mn.accumulated.m21 + s * mn.accumulated.m22;
                        cr.accumulated.m22 = c * mn.accumulated.m22 - mn.accumulated.m21 * s;
                    }
                    // Unconditional dirty after matrix propagation (0x6BEBAC)
                    cr.accumulated.dirty = true;
                    // Note: clip chain propagation is done in label_18 below,
                    // which ALL paths (active + inactive) fall through to.
                }

            }
            } // end childPtr scope — goto label_18 can jump here
            // Fall through to label_18 (matches binary: active path → LABEL_18)

        label_18:
            // LABEL_18: shared exit for ALL paths (0x6BE278..0x6BE2F8).
            // Binary always calls frameProgress + updateLayers on child,
            // even for inactive/non-visible nodes.
            if (auto *childP = mn.getChildPlayer()) {
                auto &child = *childP;
                if (child._runtime && !child._runtime->nodes.empty()) {
                    auto &cr = child._runtime->nodes[0];
                    // Clip chain propagation (0x6BE278..0x6BE29C)
                    // Binary: v17+1936 = v10+1936 (parentClipIndex)
                    //         v18 = v10; if (!node+1963) v18 = *(v10+1968)
                    //         v17+1968 = v18 (visibleAncestor with conditional)
                    //         v17+1952 = v10+1952 (third field — not mapped in our arch)
                    cr.parentClipIndex = mn.parentClipIndex;
                    // Binary 0x6BE280: if meshCombineEnabled, current node is ancestor;
                    // otherwise, propagate stored ancestor.
                    if (mn.meshCombineEnabled) {
                        cr.visibleAncestorIndex = static_cast<int>(i);
                    } else {
                        cr.visibleAncestorIndex = mn.visibleAncestorIndex;
                    }
                    // Binary 0x6BE29C: propagates node+1952 (forceVisible) to child root
                    cr.forceVisible = mn.forceVisible;
                }
                // Step child: frameProgress + updateLayers (0x6BE2A4..0x6BE2AC)
                // Binary calls both unconditionally (no guard).
                child.frameProgress(_frameLastTime);
                child.ensureNodeTreeBuilt();
                child.updateLayers();
            }
        }

    }

    void Player::updateLayersPhase3_ParticleEmitter() {
        auto &nodes = _runtime->nodes;
        // --- sub_6BEDD0: Particle emitter state (nodeType=6) ---
        // Aligned to 0x6BEDD0. Only when !isEmoteMode.
        if (_runtime->isEmoteMode) return;

        for (size_t ei = 1; ei < nodes.size(); ++ei) {
            auto &en = nodes[ei];
            if (en.nodeType != 6) continue;

            // Active/slotDone guard (0x6BEE90..0x6BEEC4)
            if (!en.accumulated.active || en.activeSlot().done) {
                en.emitterActive = false;
                en.emitterDtgt.clear();
                en.emitterTimer = 0.0;
                continue;
            }

            // dtgt from clip slot (node+536*slot+356, our activeSlot().src)
            const std::string &dtgt = en.activeSlot().src;
            if (dtgt.empty()) {
                en.emitterActive = false;
                en.emitterDtgt.clear();
                en.emitterTimer = 0.0;
                continue;
            }

            // Flags gate + re-resolve logic (0x6BEED8..0x6BEF9C)
            // Binary checks whole byte at node+44: LDRB W9,[X21,#0x2C]; CBZ W9
            // If flags==0: always LABEL_27 (continue, just accumulate timer).
            // If flags!=0: check emitterActive + dtgt comparison.
            bool doAccumulate; // true=LABEL_27 (timer += dt), false=LABEL_21 (re-resolve)

            if (!en.flags) {
                // node+44 flags == 0: skip re-resolve → LABEL_27 (0x6BEEE0)
                doAccumulate = true;
            } else if (!en.emitterActive) {
                // First init → LABEL_21 (0x6BEEFC)
                doAccumulate = false;
            } else if (en.emitterDtgt == dtgt) {
                // Same dtgt (pointer or string compare) → LABEL_27 (0x6BEEF8)
                doAccumulate = true;
            } else {
                // dtgt changed → LABEL_21
                doAccumulate = false;
            }

            if (doAccumulate) {
                // LABEL_27 (0x6BEF88): emitterTimer = _frameLastTime + emitterTimer
                en.emitterTimer += _frameLastTime;
            } else {
                // LABEL_21 (0x6BEF48): re-resolve dtgt, compute time offset.
                // Binary does NOT flip activeSlotIndex here (v10 is read once at
                // 0x6BEE9C and never modified). No crossfading flag is set either.
                en.emitterActive = true;
                en.emitterDtgt = dtgt;
                // Timer = (parentTime - clipSlot.startTime) + clipSlot.timeOffset
                // Aligned to 0x6BEF74..0x6BEFA8:
                //   parentTime = node+8 ? *(node+8+40) : player+1120 (_frameLoopTime)
                // Binary: node+8 is per-node eval data pointer. Offset 40 = evalTime.
                // Falls back to player+1120 (_frameLoopTime) if null.
                double parentTime = (ei < _runtime->perNodeEvalData.size())
                    ? _runtime->perNodeEvalData[ei].evalTime : _frameLoopTime;
                double startTime = en.activeSlot().clipStartTime;
                double timeOffset = en.activeSlot().motionTimeOffset;
                en.emitterTimer = (parentTime - startTime) + timeOffset;
            }

            // Binary: emitterOffsetActive = false AFTER branch convergence (0x6BEFB0)
            en.emitterOffsetActive = false;

            // Trigger type handling (0x6BEFC4..0x6BF0B8)
            // triggerType from clipSlot (node+536*slot+708)
            const int triggerType = en.activeSlot().prtTrigger;

            switch (triggerType) {
            case 4: {
                // Target position offset (0x6BF048..0x6BF0B8)
                // sub_6F2228 resolves target node by name from slot+712 (motionDtgt).
                // Compute position difference: target.pos - emitter.pos
                int targetIdx = findNodeByLabel(_runtime->nodeLabelMap, en.activeSlot().motionDtgt);
                if (targetIdx >= 0 && targetIdx < static_cast<int>(nodes.size())) {
                    auto &target = nodes[targetIdx];
                    en.emitterOffsetActive = true;
                    en.emitterOffsetX = target.accumulated.posX - en.accumulated.posX;
                    en.emitterOffsetY = target.accumulated.posY - en.accumulated.posY;
                    en.emitterOffsetZ = target.accumulated.posZ - en.accumulated.posZ;
                }
                break;
            }
            case 3: {
                // LABEL_36 (0x6BF028): sub_6C1540 equivalent.
                // sub_6C1540 guard at 0x6C1574: *(a3+25) [crossfading] && !*(a4+24) [otherSlotDone].
                // ratio at 0x6C15A8: (player+456 - currentSlot.startTime) / (otherSlot.startTime - currentSlot.startTime)
                // src = currentSlot+96 (current evaluated position), dst = otherSlot+96 (saved at crossfade start).
                if (en.activeSlot().crossfading && !en.otherSlot().done) {
                    const auto &slot = en.activeSlot();
                    double currentStart = slot.clipStartTime;
                    double otherStart = en.otherSlot().clipStartTime;
                    double denom = otherStart - currentStart;
                    // Binary divides directly without denom!=0 guard (0x6C15A8)
                    constexpr double epsilon = 0.0001;
                    double ratio = (_clampedEvalTime - currentStart) / denom;
                    double t2 = ratio + epsilon;
                    if (t2 >= 1.0) ratio = 0.9999;
                    t2 = std::min(t2, 1.0);
                    BezierCurve cccCurve;
                    cccCurve.x = slot.ccc.x; cccCurve.y = slot.ccc.y;
                    ControlPointCurve cpCurve;
                    if (slot.hasCpRotation) {
                        cpCurve.x = slot.cp.x; cpCurve.y = slot.cp.y;
                        cpCurve.t = slot.cp.t;
                    }
                    // src = current slot position, dst = other slot position (saved at flip)
                    // Binary reads full {x,y,z} from active slot (a3+96..112).
                    double src[3] = {slot.x, slot.y, en.activeSlot().z};
                    double dst[3] = {en.otherSlot().x, en.otherSlot().y, en.otherSlot().z};
                    double out1[3] = {}, out2[3] = {};
                    interpolatePosition69A4D4(cccCurve, dst, src, out1,
                        en.coordinateMode, cpCurve, ratio);
                    interpolatePosition69A4D4(cccCurve, dst, src, out2,
                        en.coordinateMode, cpCurve, t2);
                    en.emitterOffsetActive = true;
                    en.emitterOffsetX = out2[0] - out1[0];
                    en.emitterOffsetY = out2[1] - out1[1];
                    en.emitterOffsetZ = out2[2] - out1[2];
                }
                break;
            }
            case 2: {
                // (0x6BEFF0..0x6BF020)
                // Binary checks player+608 (_noUpdateYet) OR emitterTimer==0 (0x6BEFF4)
                if (_noUpdateYet || en.emitterTimer == 0.0) {
                    // Queuing or zero timer → same as case 3: sub_6C1540
                    // sub_6C1540 guard: crossfading && !otherSlotDone (0x6C1574)
                    if (en.activeSlot().crossfading && !en.otherSlot().done) {
                        const auto &slot = en.activeSlot();
                        double currentStart = slot.clipStartTime;
                        double otherStart = en.otherSlot().clipStartTime;
                        double denom = otherStart - currentStart;
                        // Binary divides directly without denom!=0 guard (0x6C15A8)
                        constexpr double epsilon = 0.0001;
                        double ratio = (_clampedEvalTime - currentStart) / denom;
                        double t2 = ratio + epsilon;
                        if (t2 >= 1.0) ratio = 0.9999;
                        t2 = std::min(t2, 1.0);
                        BezierCurve cccCurve;
                        cccCurve.x = slot.ccc.x; cccCurve.y = slot.ccc.y;
                        ControlPointCurve cpCurve;
                        if (slot.hasCpRotation) {
                            cpCurve.x = slot.cp.x; cpCurve.y = slot.cp.y;
                            cpCurve.t = slot.cp.t;
                        }
                        double src[3] = {slot.x, slot.y, en.activeSlot().z};
                        double dst[3] = {en.otherSlot().x, en.otherSlot().y, en.otherSlot().z};
                        double out1[3] = {}, out2[3] = {};
                        interpolatePosition69A4D4(cccCurve, dst, src, out1,
                            en.coordinateMode, cpCurve, ratio);
                        interpolatePosition69A4D4(cccCurve, dst, src, out2,
                            en.coordinateMode, cpCurve, t2);
                        en.emitterOffsetActive = true;
                        en.emitterOffsetX = out2[0] - out1[0];
                        en.emitterOffsetY = out2[1] - out1[1];
                        en.emitterOffsetZ = out2[2] - out1[2];
                    }
                } else {
                    // Non-queuing, timer running: binary reads node+176/184/192
                    // directly (0x6BF004..0x6BF020), which ARE deltaPosX/Y/Z.
                    en.emitterOffsetActive = true;
                    en.emitterOffsetX = en.deltaPosX;
                    en.emitterOffsetY = en.deltaPosY;
                    en.emitterOffsetZ = en.deltaPosZ;
                }
                break;
            }
            default:
                break;
            }
        }
    }

    void Player::updateLayersPhase3_ParticleSystem(double currentTime) {
        // --- sub_6BF0DC: Particle system (nodeType=4) ---
        // Fully aligned to libkrkr2.so 0x6BF0DC (~800 lines decompiled).
        // Velocity stored on child Player _cameraVelocityX/Y/Z (player+784/792/800).
        // frameProgress + updateLayersPhase1_PreLoop auto-applies velocity+damping.
        if (_runtime->isEmoteMode) return;
        auto &nodes = _runtime->nodes;
        const double dt = _frameLastTime;
        constexpr double PI = 3.14159265358979323846;

        for (size_t pi = 1; pi < nodes.size(); ++pi) {
            auto &pn = nodes[pi];
            if (pn.nodeType != 4) continue;

            // Binary flow: BLOCK 1 (child position update) runs BEFORE the LABEL_64
            // activity check. The activity check only gates BLOCK 2 (emission control).
            // Existing particles ALWAYS get position updates even when inactive/done.

            const int childCount = pn.getParticleCount();

            // ====== BLOCK 1: Existing particle update (0x6BF310..0x6BF668) ======
            // Binary guard: particleInheritVelocity==2 gates ALL child position updates (0x6BF304).
            // If != 2: goto LABEL_64 (skip ALL child position updates).
            // If == 2: check !slotDone && particleInheritAngle for full matrix update;
            // otherwise just add deltaPos to existing children (0x6BF32C..0x6BF384).
            if (pn.particleInheritVelocity == 2 && childCount >= 1 && !pn.activeSlot().done && pn.particleInheritAngle) {
                const double curM11 = pn.accumulated.m11, curM21 = pn.accumulated.m21;
                const double curM12 = pn.accumulated.m12, curM22 = pn.accumulated.m22;

                const bool matrixChanged =
                    (curM11 != pn.prevM11 || curM21 != pn.prevM21 ||
                     curM12 != pn.prevM12 || curM22 != pn.prevM22);

                if (matrixChanged) {
                    // Compute inv(prev) * cur (0x6BF458..0x6BF49C)
                    // Binary divides each element by det WITHOUT negation,
                    // then computes the product as subtraction pairs.
                    // This is inv(prev) * cur, NOT cur * inv(prev).
                    const double det = pn.prevM11 * pn.prevM22 - pn.prevM12 * pn.prevM21;
                    {
                        const double id = 1.0 / det;
                        const double id_m22 = pn.prevM22 * id;  // v34
                        const double id_m21 = pn.prevM21 * id;  // v35 (no negation)
                        const double id_m12 = pn.prevM12 * id;  // v36 (no negation)
                        const double id_m11 = pn.prevM11 * id;  // v37
                        // inv(prev) * cur coefficients (0x6BF490..0x6BF49C)
                        const double t11 = curM11 * id_m22 - curM21 * id_m12;  // v39
                        const double t12 = curM21 * id_m11 - curM11 * id_m21;  // v40
                        const double t21 = curM12 * id_m22 - curM22 * id_m12;  // v41
                        const double t22 = curM22 * id_m11 - curM12 * id_m21;  // v42

                        // Angle delta (0x6BF404..0x6BF43C)
                        // Binary reads node+1536 = accumulated.angle, not interpolated
                        const double curAngle = pn.accumulated.angle;
                        double angleDelta = curAngle - pn.prevParticleAngle;
                        if (pn.accumulated.flipX == pn.accumulated.flipY)
                            angleDelta = curAngle - pn.prevParticleAngle;
                        else
                            angleDelta = -(curAngle - pn.prevParticleAngle);
                        pn.prevParticleAngle = curAngle;

                        const double posXref = pn.accumulated.posX;
                        const double posYref = pn.accumulated.posY;
                        const double posZref = pn.accumulated.posZ;
                        const double dPosX = pn.deltaPosX, dPosY = pn.deltaPosY;
                        const double dPosZ = pn.deltaPosZ;

                        for (int ci = 0; ci < childCount; ++ci) {
                            auto *child = pn.getParticleChild(ci);
                            if (!child || !child->_runtime || child->_runtime->nodes.empty()) continue;
                            auto &cr = child->_runtime->nodes[0];

                            // Rotate child angle (0x6BF4C4..0x6BF528)
                            // Binary checks child._directEdit (player+482) for emote path.
                            // If _directEdit: writes to player+464 and calls initEmoteMotion.
                            // If not: writes to root node accumulated.angle.
                            if (child->_directEdit) {
                                // Emote angle path — not applicable in web port
                                // player+464 = emote angle, Player_initEmoteMotion(child, 2)
                            } else {
                                double cAngle = cr.accumulated.angle + angleDelta;
                                while (cAngle < 0.0) cAngle += 360.0;
                                while (cAngle >= 360.0) cAngle -= 360.0;
                                cr.accumulated.angle = cAngle;
                            }

                            // Transform position (0x6BF54C..0x6BF620)
                            const int coordMode = pn.coordinateMode;
                            if (coordMode == 1) {
                                // 3D: X/Z through matrix, Y pass-through
                                double px = cr.accumulated.posX - posXref + dPosX;
                                double pz = cr.accumulated.posZ - posZref + dPosZ;
                                cr.accumulated.posX = posXref + t11 * px + t12 * pz;
                                cr.accumulated.posZ = posZref + t21 * px + t22 * pz;
                                cr.accumulated.posY += dPosY;
                            } else {
                                // 2D: Binary swaps X↔Z (0x6BF5D0..0x6BF620):
                                //   newPosX = oldPosZ + deltaPosZ
                                //   newPosY = posYref + t21*px + t22*py
                                //   newPosZ = posXref + t11*px + t12*py
                                double px = cr.accumulated.posX - posXref + dPosX;
                                double py = cr.accumulated.posY - posYref + dPosY;
                                cr.accumulated.posX = cr.accumulated.posZ + dPosZ;
                                cr.accumulated.posY = posYref + t21 * px + t22 * py;
                                cr.accumulated.posZ = posXref + t11 * px + t12 * py;
                            }

                            // Transform velocity (0x6BF628..0x6BF64C)
                            double vx = child->_cameraVelocityX;
                            double vy = (coordMode == 1) ? child->_cameraVelocityZ
                                                         : child->_cameraVelocityY;
                            double nvx = t11 * vx + t12 * vy;
                            double nvy = t21 * vx + t22 * vy;
                            child->_cameraVelocityX = nvx;
                            if (coordMode == 1) child->_cameraVelocityZ = nvy;
                            else child->_cameraVelocityY = nvy;
                        }
                    }
                    pn.prevM11 = curM11; pn.prevM21 = curM21;
                    pn.prevM12 = curM12; pn.prevM22 = curM22;
                } else {
                    // Matrix unchanged: just add delta position (0x6BF348..0x6BF384)
                    for (int ci = 0; ci < childCount; ++ci) {
                        auto *child = pn.getParticleChild(ci);
                        if (!child || !child->_runtime || child->_runtime->nodes.empty()) continue;
                        auto &cr = child->_runtime->nodes[0];
                        cr.accumulated.posX += pn.deltaPosX;
                        cr.accumulated.posY += pn.deltaPosY;
                        cr.accumulated.posZ += pn.deltaPosZ;
                    }
                }
            } else if (pn.particleInheritVelocity == 2 && childCount >= 1) {
                // Missing path from binary (0x6BF32C..0x6BF384):
                // When particleInheritVelocity==2 but (slotDone || !particleInheritAngle),
                // still add deltaPos to existing children's positions.
                for (int ci = 0; ci < childCount; ++ci) {
                    auto *child = pn.getParticleChild(ci);
                    if (!child || !child->_runtime || child->_runtime->nodes.empty()) continue;
                    auto &cr = child->_runtime->nodes[0];
                    cr.accumulated.posX += pn.deltaPosX;
                    cr.accumulated.posY += pn.deltaPosY;
                    cr.accumulated.posZ += pn.deltaPosZ;
                }
            }
            // Binary: when particleInheritVelocity != 2, goto LABEL_64 (0x6BF314)
            // skips ALL child position updates — no deltaPos addition.

            // ====== LABEL_64: Activity check (0x6BF668..0x6BF710) ======
            // Binary: only !accumulated.active sets particleEmitterFlagActive=false.
            // slotDone alone does NOT reset the flag — it just skips emission.
            // emitCount declared here so goto doesn't cross initialization.
            {
            int emitCount = 0;
            if (!pn.accumulated.active) {
                pn.particleEmitterFlagActive = false;
                goto physics_step;
            }

            // ====== BLOCK 2: Emission control (0x6BF668..0x6BF810) ======
            // Binary: slotDone skips emission but does NOT reset particleEmitterFlagActive.
            if (pn.activeSlot().done) goto physics_step;
            {
                const double prtFmin = pn.activeSlot().prtFmin;
                const double prtF = pn.activeSlot().prtF;
                const int prtTrigger = pn.activeSlot().prtTrigger;

                if (prtTrigger == 0 && prtFmin == 0.0) goto physics_step;

                const bool wasActive = pn.particleEmitterFlagActive;
                pn.particleEmitterFlagActive = true;

                // Read trigger type from slot (0x6BF680..0x6BF690)
                const int triggerType = pn.prtTrigger;

                if (triggerType == 0) {
                    // Frequency mode (0x6BF690..0x6BF7F4)
                    if (!wasActive) {
                        // First frame: initialize timer (0x6BF7BC..0x6BF7EC)
                        // Binary interpolates in frequency domain: lerp(60/prtFmin, 60/prtF, r)
                        double freq0 = 60.0 / prtFmin;
                        double freq1 = 60.0 / prtF;
                        if (freq0 != freq1)
                            freq0 = freq0 + (freq1 - freq0) * random();
                        pn.emitterTimerAccum = freq0;
                    }
                    // Timer loop (0x6BF698..0x6BF6F8)
                    pn.emitterTimerAccum -= dt;
                    while (pn.emitterTimerAccum <= 0.0) {
                        double freq0 = 60.0 / prtFmin;
                        double freq1 = 60.0 / prtF;
                        if (freq0 != freq1)
                            freq0 = freq0 + (freq1 - freq0) * random();
                        pn.emitterTimerAccum += freq0;
                        ++emitCount;
                    }
                    // LABEL_85 timer clamp (0x6BF780..0x6BF7B8)
                    // Only for frequency mode (triggerType==0).
                    // Clamps timer to min(60/prtFmin, currentTimer).
                    if (prtFmin > 0.0) {
                        double maxTimer = 60.0 / prtFmin;
                        if (maxTimer > pn.emitterTimerAccum)
                            maxTimer = pn.emitterTimerAccum;
                        pn.emitterTimerAccum = maxTimer;
                        if (emitCount <= 0) goto physics_step;
                    }
                } else if (triggerType == 1) {
                    // Count mode (0x6BF734..0x6BF804)
                    // Binary checks node+44 (flags byte, v173) not particleInheritAngle.
                    if (pn.flags) {
                        double r = random();
                        emitCount = static_cast<int>(prtFmin + (prtF - prtFmin) * r);
                    }
                    // Timer clamp is NOT applied for triggerType==1 in binary.
                    // LABEL_85 (0x6BF780) is only reachable from the frequency mode path.
                    if (emitCount <= 0) goto physics_step;
                }
            }

            // ====== BLOCK 3: Particle creation (0x6BF810..0x6C02DC) ======
            // Binary creates exactly 1 particle per frame per node.
            // When srcList is empty (v85==0), skip creation and run ONE physics step (0x6C02D0).
            // When emitCount > 1, physics is skipped; next frame creates another particle.
            if (emitCount > 0) {
                // 3a. Resolve srcList count (0x6BF810..0x6BF87C)
                const auto &srcList = pn.activeSlot().srcList;
                const int srcListCount = static_cast<int>(srcList.size());

                // Binary: if srcList count==0, skip creation entirely (0x6C02D0).
                // The binary decrements emitCount to 0 (a no-op loop), then runs
                // ONE physics step. No particles are created.
                if (srcListCount == 0) {
                    goto physics_step;
                }

                // Binary creates exactly 1 particle per frame per node (0x6BF810..0x6C02DC).
                // emitCount > 1 just means "skip physics this frame" (0x6C0270).
                {

                // Random selection from srcList (0x6BF87C: v86 = random() * v85)
                int idx = static_cast<int>(random() * srcListCount);
                if (idx >= srcListCount) idx = srcListCount - 1;
                const std::string &selectedSrc = srcList[idx];
                if (selectedSrc.empty()) goto physics_step;

                // Handle "chara/motion" format (binary: sub_697D34 splits by "/")
                std::string particleChara;
                std::string motionPath;
                auto slashPos = selectedSrc.find('/');
                if (slashPos != std::string::npos) {
                    particleChara = selectedSrc.substr(0, slashPos);
                    motionPath = selectedSrc.substr(slashPos + 1);
                } else {
                    motionPath = selectedSrc;
                }

                // 3b. Create child Player via TJS dispatch (0x6BF93C..0x6BFA00)
                // Aligned to binary: new Player → CreateAdaptor → Array.add
                using PlayerAdaptor = ncbInstanceAdaptor<Player>;
                auto *childRaw = new Player(_resourceManagerNative);
                childRaw->_tjsRandomGenerator = _tjsRandomGenerator;
                iTJSDispatch2 *childDisp = PlayerAdaptor::CreateAdaptor(childRaw);
                if (!childDisp) { delete childRaw; goto physics_step; }
                tTJSVariant childVar(childDisp, childDisp);
                childDisp->Release();
                auto *child = childRaw;  // native pointer for subsequent use
                // Binary: chara comes from the split path, not parent chara
                child->setChara(particleChara.empty() ? _chara : detail::widen(particleChara));
                child->onFindMotion(detail::widen(motionPath));
                // Stealth motion (0x6BFA08..0x6BFA40): binary checks child+776
                // (stealth motion path) and plays it with flag 0x10. In our arch,
                // propagate parent stealth fields so child resolves stealth if set.
                child->_stealthChara = _stealthChara;
                child->_stealthMotion = _stealthMotion;
                if (!_stealthMotion.IsEmpty()) {
                    child->onFindMotion(_stealthMotion, PlayFlagStealth);
                }
                // _parentColorPacked propagation (0x6BF9B4)
                {
                    uint32_t packed;
                    std::memcpy(&packed, &pn.colorBytes[0], sizeof(uint32_t));
                    child->_parentColorPacked = packed;
                }
                // emoteEdit propagation (0x6BF9C0..0x6BF9D4)
                child->_emoteEditVariant = _emoteEditVariant;
                child->_zFactor = _zFactor;
                child->_independentLayerInherit = _independentLayerInherit;

                // Set blendMode on child root node accumulated state (0x6BFAA8..0x6BFAC4)
                // Binary writes to *(v99+1656) = root node accumulated blendMode, not activeSlot.
                if (child->_runtime && !child->_runtime->nodes.empty()) {
                    auto &cr = child->_runtime->nodes[0];
                    auto blendVal = pn.activeSlot().blendMode;
                    if (cr.accumulated.blendMode != blendVal) {
                        cr.accumulated.dirty = true;
                        cr.accumulated.blendMode = blendVal;
                    }
                }

                // Stealth motion play (0x6BFA08..0x6BFA40)
                // Binary: if child+776 (stealth path) exists, play it with flags=16
                // In our architecture, stealth is stored at player level.
                // The binary copies the stealth path from the resource manager state.
                // For web port, stealth motion is rarely used; skip for now.

                // 3c. Position based on flyDirection (0x6BFAC8..0x6BFC88)
                double offX = 0, offY = 0, offZ = 0;
                // Binary uses "particle" field (node+2164, PSB key "particle") for fly
                // direction, NOT particleFlyDirection (node+2180). 0x6BFAC8.
                const int flyDir = pn.particleType;
                // Binary uses node+2189 (particleTriVolume, PSB key), not coordinateMode.
                const bool has3D = pn.particleTriVolume;

                if (flyDir == 2) {
                    // Uniform box (0x6BFB88..0x6BFBCC)
                    // Binary RNG order: r1→offX (v110→v168), r2→offY (v167) (0x6BFB88)
                    double r1 = random();
                    offY = random() * 32.0 - 16.0;  // r2→offY (v167)
                    offX = r1 * 32.0 - 16.0;         // r1→offX (v168)
                    if (has3D) offZ = random() * 32.0 - 16.0;
                } else if (flyDir == 1) {
                    // 3D sphere (0x6BFAE4..0x6BFB78)
                    if (has3D) {
                        double r1 = random(), r2 = random(), r3 = random();
                        double phi = r2 * 2.0 * PI;
                        double theta = r1 * 2.0 * PI;
                        double radius = std::cbrt(r3) * 16.0;
                        double cosPhi = std::cos(phi);
                        offX = cosPhi * (radius * std::cos(theta));
                        offY = radius * (cosPhi * std::sin(theta));
                        offZ = radius * std::sin(phi);
                    } else {
                        // 2D disk (0x6BFC14..0x6BFC48)
                        double angle2d = random() * 2.0 * PI;
                        double radius = std::sqrt(random()) * 16.0;
                        offX = std::cos(angle2d) * radius;
                        offY = radius * std::sin(angle2d);
                    }
                } else {
                    // flyDir == 0 or other: offX=offY=0 (0x6BFBD8)
                    offX = 0.0;
                    offY = 0.0;
                }

                // Z component scale by sqrt(det(matrix)) (0x6BFC64..0x6BFC88)
                // Binary does sqrt(det) without abs — NaN for negative det.
                if (offZ != 0.0) {
                    const double det = pn.accumulated.m11 * pn.accumulated.m22
                                     - pn.accumulated.m12 * pn.accumulated.m21;
                    offZ *= std::sqrt(det);
                }

                // Transform offset through parent matrix (0x6BFCE0..0x6BFCE8)
                const double m11 = pn.accumulated.m11, m21 = pn.accumulated.m21;
                const double m12 = pn.accumulated.m12, m22 = pn.accumulated.m22;
                const double clipOX = pn.clipOriginX, clipOY = pn.clipOriginY;
                const double txOff = m11 * (offX - clipOX) + m12 * (offY - clipOY);
                const double tyOff = m21 * (offX - clipOX) + m22 * (offY - clipOY);

                // 3d. Speed = lerp(prtVmin, prtV, random()) (0x6BFC94..0x6BFCBC)
                // Binary only calls random() when min != max to preserve RNG sequence.
                double speed = pn.activeSlot().prtVmin;
                if (speed != pn.activeSlot().prtV)
                    speed = speed + (pn.activeSlot().prtV - speed) * random();

                // 3e. Direction based on particleFlyDirection (0x6BFCEC..0x6BFDE8)
                // Binary uses node+2180 (particleFlyDirection) for direction mode,
                // NOT node+2176 (particleInheritVelocity). 0x6BFCC4.
                double direction = 0.0;
                const int inhVel = pn.particleFlyDirection;

                if (inhVel == 2) {
                    // Exponential decay (0x6BFD58..0x6BFDE8)
                    double dist = std::sqrt(txOff * txOff + tyOff * tyOff + offZ * offZ);
                    double dirAngle = std::atan2(tyOff, txOff) * 360.0;
                    double decay = pn.particleAccelRatio;
                    // Binary reads cached player+1128 directly (0x6BFD88)
                    double childTotalTime = child->_cachedTotalFrames;
                    double dtNorm = childTotalTime / 60.0;
                    if (decay == 1.0) {
                        speed = (dtNorm > 0) ? dist / dtNorm : 0.0;
                    } else if (decay > 0.0 && dtNorm > 0.0) {
                        speed = dist * std::log(decay) / (std::pow(decay, dtNorm) - 1.0);
                    }
                    direction = dirAngle / (2.0 * PI) + 180.0;
                    direction = direction * PI / 180.0; // convert to radians
                    speed /= 60.0;
                } else if (inhVel == 1) {
                    // Offset direction (0x6BFCF4..0x6BFD18)
                    direction = std::atan2(tyOff, txOff) * 360.0 / (2.0 * PI) + 180.0;
                    direction = direction * PI / 180.0;
                } else {
                    // Matrix angle (0x6BFDAC): atan2(m12, m11) — node+136, node+120.
                    direction = std::atan2(pn.accumulated.m12, pn.accumulated.m11) * 360.0 / (2.0 * PI);
                    direction = direction * PI / 180.0;
                }

                // Angle spread (0x6BFDEC..0x6BFE34)
                double range = pn.activeSlot().prtRange;
                double spreadRandom = -range;
                if (range != -range) spreadRandom = (range + range) * random() - range;
                double totalAngle = direction + spreadRandom * PI / 180.0;
                double dirRad = totalAngle;

                // 3f. particleApplyZoomToVelocity (0x6BFE38..0x6BFEA0)
                double zoomScale = 1.0;
                if (inhVel >= 1 && inhVel <= 2) {
                    if (txOff != 0.0 || tyOff != 0.0) {
                        if (offZ != 0.0) {
                            double xyLen = std::sqrt(txOff * txOff + tyOff * tyOff);
                            zoomScale = xyLen / std::sqrt(offZ * offZ + xyLen * xyLen);
                        }
                    }
                }

                // Compute velocity + set position (0x6BFEC0..0x6BFF70)
                // Binary branches on coordinateMode (node+24), not inhVel.
                double velX = 0.0, velY = 0.0, velZ = 0.0;

                if (child->_runtime && !child->_runtime->nodes.empty()) {
                    auto &cr = child->_runtime->nodes[0];
                    if (pn.coordinateMode == 1) {
                        // 3D mode (0x6BFEB4..0x6BFEDC)
                        cr.accumulated.posX = txOff + pn.accumulated.posX;
                        cr.accumulated.posY = offZ + pn.accumulated.posY;
                        cr.accumulated.posZ = tyOff + pn.accumulated.posZ;
                        velX = zoomScale * speed * std::cos(dirRad);
                        velY = speed * 0.0;
                        velZ = zoomScale * speed * std::sin(dirRad);
                    } else if (pn.coordinateMode == 0) {
                        // 2D mode (0x6BFF14..0x6BFF3C)
                        cr.accumulated.posX = txOff + pn.accumulated.posX;
                        cr.accumulated.posY = tyOff + pn.accumulated.posY;
                        cr.accumulated.posZ = offZ + pn.accumulated.posZ;
                        velX = zoomScale * speed * std::cos(dirRad);
                        velY = zoomScale * speed * std::sin(dirRad);
                        velZ = speed * 0.0;
                    }

                    // 3h. Set flipX/Y (0x6BFF74..0x6BFFA4)
                    // Binary only writes + sets dirty when values differ.
                    if (cr.accumulated.flipX != pn.accumulated.flipX ||
                        cr.accumulated.flipY != pn.accumulated.flipY) {
                        cr.accumulated.flipX = pn.accumulated.flipX;
                        cr.accumulated.flipY = pn.accumulated.flipY;
                        cr.accumulated.dirty = true;
                    }

                    // 3i. Angle from prtA lerp — BEFORE zoom (0x6BFFA8..0x6C00AC)
                    // Binary order: angle lerp → angle computation → zoom lerp.
                    // Both call random(), so order matters for RNG sequence.
                    double aMin = pn.activeSlot().prtAmin;
                    double aMax = pn.activeSlot().prtA;
                    double prtAngle = aMin;
                    if (aMin != aMax) prtAngle = aMin + (aMax - aMin) * random();
                    // Binary uses PARENT flipX/Y for sign (0x6BFFD8..0x6BFFE0)
                    double childAngle = -prtAngle;
                    if (pn.accumulated.flipX == pn.accumulated.flipY) childAngle = prtAngle;

                    if (pn.particleInheritAngle) {
                        // Binary: v154 = dirRad + PI; if(!flipX) v154 = dirRad;
                        // then childAngle += v154 * 360 / (2*PI) (0x6BFFEC..0x6C0008)
                        double v154 = dirRad + PI;
                        if (!pn.accumulated.flipX) v154 = dirRad;
                        childAngle += v154 * 360.0 / (2.0 * PI);
                    }
                    while (childAngle < 0.0) childAngle += 360.0;
                    while (childAngle >= 360.0) childAngle -= 360.0;

                    // _directEdit check (0x6C0058): binary writes to player+464 and
                    // calls Player_initEmoteMotion if child._directEdit is true
                    if (child->_directEdit) {
                        // Emote mode angle path (0x6C0088..0x6C00AC)
                        double k = childAngle;
                        while (k < 0.0) k += 360.0;
                        while (k >= 360.0) k -= 360.0;
                        // player+464 = emote angle — not mapped in web port
                        // Player_initEmoteMotion(child, 2) — N/A for web
                    } else {
                        // Normal angle path (0x6C0060..0x6C0078)
                        if (cr.accumulated.angle != childAngle) {
                            cr.accumulated.dirty = true;
                            cr.accumulated.angle = childAngle;
                        }
                    }

                    // 3j. Zoom lerp — AFTER angle (0x6C00B0..0x6C00D8)
                    double zoom = pn.activeSlot().prtZmin;
                    if (zoom != pn.activeSlot().prtZ)
                        zoom = zoom + (pn.activeSlot().prtZ - zoom) * random();
                    if (cr.accumulated.scaleX != zoom || cr.accumulated.scaleY != zoom) {
                        cr.accumulated.dirty = true;
                        cr.accumulated.scaleX = zoom;
                        cr.accumulated.scaleY = zoom;
                    }

                    // 3j. particleApplyZoomToVelocity on child velocity (0x6C0110..0x6C0168)
                    // Binary gate: particleFlyDirection != 2 (0x6C0110)
                    if (pn.particleFlyDirection != 2) {
                        if (pn.particleApplyZoomToVelocity == 1) {
                            velX *= zoom; velY *= zoom; velZ *= zoom;
                        } else if (pn.particleApplyZoomToVelocity == 2 && zoom != 0.0) {
                            velX /= zoom; velY /= zoom; velZ /= zoom;
                        }
                    }
                }

                // 3k. Store velocity on child (0x6BFEF8..0x6BFF70)
                child->_cameraVelocityX = velX;
                child->_cameraVelocityY = velY;
                child->_cameraVelocityZ = velZ;

                // 3l. particleInheritVelocity==1: add parent delta/dt (0x6C0174..0x6C01AC)
                // Binary checks node+2176 (particleInheritVelocity), not particleFlyDirection.
                // Binary at 0x6C0178: checks dt != 0.0 (not dt > 0.0)
                if (pn.particleInheritVelocity == 1 && dt != 0.0) {
                    child->_cameraVelocityX += pn.deltaPosX / dt;
                    child->_cameraVelocityY += pn.deltaPosY / dt;
                    child->_cameraVelocityZ += pn.deltaPosZ / dt;
                }

                // 3m. Set cameraDamping (0x6C01B4)
                // Binary: node+2192 is one field for both decay and damping
                child->_cameraDamping = pn.particleAccelRatio;

                pn.addParticleChild(childVar);

                // Enforce maxNum per-particle (0x6C0218..0x6C0268)
                // Binary: signed comparison count > maxNum. When maxNum==0, ALL particles
                // are removed (size > 0 is always true). Only removes ONE per emission.
                if (pn.getParticleCount() > pn.particleMaxNum) {
                    pn.eraseParticleChild(0);
                }

                // Physics only when emitCount <= 1 (0x6C026C: CMP W20, #1; B.GT)
                if (emitCount <= 1) goto physics_step;
                // emitCount > 1: skip physics this frame, advance to next node.
                // Next frame will create another particle.
                continue;
                } // end creation block
            }
            } // end outer emitCount scope

        physics_step:
            // ====== sub_6C17A4: Physics stepping ======
            // Pass 1: Delete particles (0x6C1858..0x6C1950)
            // Binary uses TJS Array.erase with index-based iteration.
            // When erasing, count decreases and index stays (--i after erase).
            {
                int pCount = pn.getParticleCount();
                for (int ci = 0; ci < pCount; ++ci) {
                    auto *child = pn.getParticleChild(ci);
                    bool shouldErase = false;
                    if (!child || !child->_runtime || child->_runtime->nodes.empty()) {
                        shouldErase = true;
                    } else if (child->_allplaying) {
                        // Playing: only check bounds if particleDeleteOutside (0x6C1888)
                        if (pn.particleDeleteOutside) {
                            const double bMinX = child->_boundsMinX;
                            const double bMinY = child->_boundsMinY;
                            const double bMaxX = child->_boundsMaxX;
                            const double bMaxY = child->_boundsMaxY;
                            if (bMaxX >= bMinX && bMaxY >= bMinY) {
                                const double sw = static_cast<double>(_runtime->width);
                                const double sh = static_cast<double>(_runtime->height);
                                if (!(bMaxY > 0.0 && bMinX < sw && bMaxX > 0.0 && bMinY < sh)) {
                                    shouldErase = true;
                                }
                            }
                        }
                    } else {
                        // Not playing: always delete (0x6C1880)
                        shouldErase = true;
                    }
                    if (shouldErase) {
                        // Aligned to sub_6C17A4 (0x6C1930): TJS Array.erase(index)
                        pn.eraseParticleChild(ci);
                        --ci;
                        pCount = pn.getParticleCount();
                    }
                }
            }

            // Pass 2: Step each remaining child (0x6C1984..0x6C1A3C)
            // Binary at 0x6C1960: mesh combine parent propagation.
            {
                const int meshParentIdx = pn.meshCombineEnabled
                    ? static_cast<int>(pi) : pn.visibleAncestorIndex;
                const int pCount2 = pn.getParticleCount();
                for (int ci = 0; ci < pCount2; ++ci) {
                    auto *child = pn.getParticleChild(ci);
                    if (!child || !child->_runtime) continue;
                    child->_zFactor = _zFactor;
                    if (!child->_runtime->nodes.empty()) {
                        auto &cr = child->_runtime->nodes[0];
                        cr.parentClipIndex = pn.parentClipIndex;
                        cr.visibleAncestorIndex = meshParentIdx;
                        cr.forceVisible = pn.forceVisible;
                    }
                    child->frameProgress(_frameLastTime);
                    child->ensureNodeTreeBuilt();
                    if (!child->_runtime->nodes.empty()) {
                        child->updateLayers();
                    }
                }
            }
        } // for each nodeType==4
    }

    void Player::updateLayersPhase3_AnchorNode() {
        auto &nodes = _runtime->nodes;
        // --- sub_6C0528: Anchor node processing (nodeType=10) ---
        // Aligned to 0x6C0528. For each nodeType=10 active node,
        // apply exponential damping toward root node values.
        for (size_t ai = 1; ai < nodes.size(); ++ai) {
            auto &an = nodes[ai];
            if (an.nodeType != 10 || !an.accumulated.active) continue;
            _needsInternalAssignImages = true;
            if (_frameLastTime == 0.0) {
                an.anchorEnabled = false;
                continue;
            }
            an.anchorEnabled = true;
            // Read width/height (0x6C0790..0x6C0848)
            double cw = an.interpolatedCache.width;
            double ch = an.interpolatedCache.height;
            if (cw <= 0.0) cw = 32.0;
            if (ch <= 0.0) ch = 32.0;
            an.clipW = cw;
            an.clipH = ch;
            an.originX = cw * 0.5;
            an.originY = ch * 0.5;

            // Damping exponent (0x6C088C..0x6C08B8)
            // From decompilation: v28 = dt * (v27*dt/v27) / v27 / 60 / damping
            // where v27 = dt/fps. Simplifies to dt*fps/60/damping for dt~1 frame.
            const double dampPow = std::abs(_frameLastTime) / 60.0
                / std::max(an.anchorDamping, 0.001);

            // Angle damping (0x6C08C0..0x6C08E0)
            double angle = an.accumulated.angle;
            if (angle >= 180.0)
                angle = 360.0 - (360.0 - angle) * dampPow;
            else
                angle = angle * dampPow;
            an.accumulated.angle = angle;

            // Scale damping (0x6C08E0..0x6C0924)
            an.accumulated.scaleX = std::pow(
                an.accumulated.scaleX * 32.0 / cw, dampPow);
            an.accumulated.scaleY = std::pow(
                an.accumulated.scaleY * 32.0 / ch, dampPow);

            // Slant damping (0x6C0924..0x6C0938)
            an.accumulated.slantX *= dampPow;
            an.accumulated.slantY *= dampPow;

            // Rebuild local matrix via sub_699940 (0x6C0944)
            {
                Affine2x3 la = {1.0, 0.0, 0.0, 1.0, 0.0, 0.0};
                applyLocalTransform(la, an);
                an.accumulated.m11 = la[0]; an.accumulated.m21 = la[1];
                an.accumulated.m12 = la[2]; an.accumulated.m22 = la[3];
            }

            // If !independentLayerInherit: multiply with root (0x6C094C)
            if (!_independentLayerInherit && !nodes.empty()) {
                const auto &rn = nodes[0];
                const double nm11 = an.accumulated.m11, nm12 = an.accumulated.m12;
                const double nm21 = an.accumulated.m21, nm22 = an.accumulated.m22;
                an.accumulated.m11 = rn.accumulated.m11*nm11 + rn.accumulated.m12*nm21;
                an.accumulated.m21 = rn.accumulated.m21*nm11 + rn.accumulated.m22*nm21;
                an.accumulated.m12 = rn.accumulated.m11*nm12 + rn.accumulated.m12*nm22;
                an.accumulated.m22 = rn.accumulated.m21*nm12 + rn.accumulated.m22*nm22;
            }

            // Opacity damping (0x6C0994..0x6C09F8)
            {
                int opa = an.accumulated.opacity;
                double opaF = static_cast<double>(opa) / 255.0;
                if (opa == 0) opaF = 1.0 / 255.0;
                double newOpa = std::pow(opaF, dampPow) * 255.0 * an.anchorOpaScale;
                newOpa = std::clamp(newOpa, 0.0, 255.0);
                an.accumulated.opacity = static_cast<int>(newOpa);
                double denom = newOpa;
                if (static_cast<int>(newOpa) < 0) denom += 4294967296.0;
                if (denom != 0.0) an.anchorOpaScale = newOpa / denom;
            }

            // Position lerp toward root (0x6C0A04..0x6C0A4C)
            if (!nodes.empty()) {
                const auto &rn = nodes[0];
                an.accumulated.posX = rn.accumulated.posX
                    + dampPow * (an.accumulated.posX - rn.accumulated.posX);
                an.accumulated.posY = rn.accumulated.posY
                    + dampPow * (an.accumulated.posY - rn.accumulated.posY);
                an.accumulated.posZ = rn.accumulated.posZ
                    + dampPow * (an.accumulated.posZ - rn.accumulated.posZ);
            }

            // Color damping (0x6C0A68..0x6C0C58)
            // Per-channel pow(channel/base, dampPow)*base*colorScale
            {
                const bool isDefaultBlend =
                    (an.interpolatedCache.blendMode & 0xF0) == 0x10;
                const double base = isDefaultBlend ? 255.0 : 255.0;
                const auto packedColors = copyPackedColorsFromBytes(an.colorBytes);
                const bool allEqual =
                    packedColors[0] == packedColors[1]
                    && packedColors[1] == packedColors[2]
                    && packedColors[2] == packedColors[3];
                if (!(allEqual && packedColors[0] == 0xFF808080u)) {
                    int iters = (allEqual) ? 1 : 4;
                    for (int ci = 0; ci < iters && ci < 4; ++ci) {
                        for (int ch = 0; ch < 3; ++ch) {
                            double v = static_cast<double>(an.colorBytes[ci*4+ch]);
                            if (v == 0.0) v = 1.0;
                            double res = base * std::pow(v / base, dampPow)
                                * an.anchorColorScale[ci*4+ch];
                            res = std::clamp(res, 0.0, 255.0);
                            int ir = static_cast<int>(res);
                            double dr = static_cast<double>(ir);
                            if (dr != 0.0) an.anchorColorScale[ci*4+ch] = res / dr;
                            an.colorBytes[ci*4+ch] = static_cast<uint8_t>(ir);
                        }
                        // Alpha channel (0x6C0BA8..0x6C0BE0)
                        double av = static_cast<double>(an.colorBytes[ci*4+3]) / 255.0;
                        if (av == 0.0) av = 1.0 / 255.0;
                        double ares = std::pow(av, dampPow) * 255.0
                            * an.anchorColorScale[ci*4+3];
                        ares = std::clamp(ares, 0.0, 255.0);
                        int iar = static_cast<int>(ares);
                        double dar = static_cast<double>(iar);
                        if (dar != 0.0) an.anchorColorScale[ci*4+3] = ares / dar;
                        an.colorBytes[ci*4+3] = static_cast<uint8_t>(iar);
                    }
                    if (allEqual) {
                        std::memcpy(&an.colorBytes[4], &an.colorBytes[0], 4);
                        std::memcpy(&an.colorBytes[8], &an.colorBytes[0], 4);
                        std::memcpy(&an.colorBytes[12], &an.colorBytes[0], 4);
                    }
                }
            }
        }

    }

    // --- updateLayers: 3-phase pipeline ---
    // Aligned to libkrkr2.so Player_updateLayers (0x6BB33C).
    // Operates on persistent MotionNode vector instead of re-walking PSB tree.
    void Player::updateLayers() {
        auto &nodes = _runtime->nodes;
        if (nodes.empty()) return;
        const auto motionPath =
            _runtime && _runtime->activeMotion ? _runtime->activeMotion->path
                                               : std::string{};
        const double currentTime = _clampedEvalTime;

        // Ensure per-node eval data array matches node count (player+384).
        // Binary allocates this as a fixed-size array during Player construction;
        // we resize dynamically to match node count.
        if (_runtime->perNodeEvalData.size() != nodes.size()) {
            _runtime->perNodeEvalData.resize(nodes.size());
        }
        // Set eval time for all nodes to _clampedEvalTime (player+456).
        // Binary writes per-node eval time during the main loop (0x6BB4E0 area).
        for (size_t ni = 0; ni < nodes.size(); ++ni) {
            _runtime->perNodeEvalData[ni].evalTime = _clampedEvalTime;
        }

        updateLayersPhase1_PreLoop(currentTime);
        updateLayersPhase2_MainLoop(currentTime);
        if(detail::logoChainTraceEnabled(_runtime->activeMotion)) {
            const auto &root = nodes[0];
            detail::logoChainTraceLogf(
                motionPath, "updateLayers.phase1", "0x6BB33C", currentTime,
                "rootPos=({:.3f},{:.3f},{:.3f}) cameraVel=({:.3f},{:.3f},{:.3f}) damping={:.6f} variableCount={}",
                root.accumulated.posX, root.accumulated.posY,
                root.accumulated.posZ, _cameraVelocityX, _cameraVelocityY,
                _cameraVelocityZ, _cameraDamping, _variableValues.size());
            for(const auto &[label, value] : _variableValues) {
                detail::logoChainTraceLogf(
                    motionPath, "updateLayers.phase1.var", "0x6BB33C",
                    currentTime, "label={} value={:.6f}", label, value);
            }
            for(const auto &node : nodes) {
                const auto &ic = node.interpolatedCache;
                const auto &ac = node.accumulated;
                const auto &ls = node.localState;
                const bool hasParent = node.parentIndex >= 0
                    && node.parentIndex < static_cast<int>(nodes.size());
                const auto &pc = hasParent ? nodes[node.parentIndex].accumulated
                                           : nodes[0].accumulated;
                detail::logoChainTraceLogf(
                    motionPath, "updateLayers.phase2.node", "0x6BB33C",
                    currentTime,
                    "nodeIndex={} label={} type={} parent={} src={} inherit=0x{:x} indep={} interp[x={:.3f},y={:.3f},ox={:.3f},oy={:.3f},w={:.3f},h={:.3f},opacity={:.6f},angle={:.3f},scale=({:.6f},{:.6f}),slant=({:.6f},{:.6f}),flip=({},{}) blend={}] local[pos=({:.3f},{:.3f},{:.3f}),angle={:.3f},scale=({:.6f},{:.6f}),slant=({:.6f},{:.6f}),flip=({},{}) opacity={},blend={}] parentAccum[pos=({:.3f},{:.3f},{:.3f}),scale=({:.6f},{:.6f}),slant=({:.6f},{:.6f}),matrix=({:.6f},{:.6f},{:.6f},{:.6f}),opacity={},blend={}] accum[pos=({:.3f},{:.3f},{:.3f}),scale=({:.6f},{:.6f}),slant=({:.6f},{:.6f}),matrix=({:.6f},{:.6f},{:.6f},{:.6f}),opacity={},blend={},active={},visible={}]",
                    node.index,
                    node.layerName.empty() ? std::string("<root>")
                                           : node.layerName,
                    node.nodeType, node.parentIndex,
                    ic.src.empty() ? std::string("<none>") : ic.src,
                    node.inheritFlags,
                    _independentLayerInherit ? 1 : 0,
                    ic.x, ic.y, ic.ox, ic.oy, ic.width, ic.height, ic.opacity,
                    ic.angle, ic.scaleX, ic.scaleY, ic.slantX, ic.slantY,
                    ic.flipX ? 1 : 0, ic.flipY ? 1 : 0, ic.blendMode,
                    ls.posX, ls.posY, ls.posZ, ls.angle, ls.scaleX, ls.scaleY,
                    ls.slantX, ls.slantY, ls.flipX ? 1 : 0, ls.flipY ? 1 : 0,
                    ls.opacity, ls.blendMode,
                    pc.posX, pc.posY, pc.posZ, pc.scaleX, pc.scaleY,
                    pc.slantX, pc.slantY, pc.m11, pc.m12, pc.m21, pc.m22,
                    pc.opacity, pc.blendMode,
                    ac.posX, ac.posY, ac.posZ, ac.scaleX, ac.scaleY,
                    ac.slantX, ac.slantY, ac.m11, ac.m12,
                    ac.m21, ac.m22, ac.opacity, ac.blendMode,
                    ac.active ? 1 : 0, ac.visible ? 1 : 0);
            }
        }

        // === PHASE 3: Post-loop processing ===
        // Call order matches libkrkr2.so Player_updateLayers (0x6BBC60..0x6BBCA8):
        // sub_6BC000 → sub_6BC4F0 → sub_6BD8DC → sub_6BDA28 →
        // sub_6BDCC0 → sub_6BDE94 → sub_6BE0C0 → sub_6BEDD0 →
        // sub_6BF0DC → sub_6C0528
        updateLayersPhase3_CameraConstraint();
        updateLayersPhase3_VertexComputation();
        updateLayersPhase3_Visibility();
        updateLayersPhase3_CameraNode();
        updateLayersPhase3_ShapeAABB();
        updateLayersPhase3_ShapeGeometry();
        updateLayersPhase3_MotionSubNode(currentTime);
        updateLayersPhase3_ParticleEmitter();
        updateLayersPhase3_ParticleSystem(currentTime);
        updateLayersPhase3_AnchorNode();

        // === Post-loop cleanup ===
        // Aligned to 0x6BBCB4..0x6BBE1C: clear per-node flags and timeline state.

        // Clear player+608 first-frame flag (0x6BBDF8: STRB WZR, [X19,#0x260]).
        _noUpdateYet = false;

        // Clear player+480 queuing flag (0x6BBDFC: STRB WZR, [X19,#0x1E0]).
        _queuing = false;

        // Clear node+44 (flags byte) and node+1504 (accumulated visible)
        // for all non-root nodes (0x6BBCFC..0x6BBD40).
        for (size_t ci = 1; ci < nodes.size(); ++ci) {
            nodes[ci].flags &= ~0x01;           // node+44
            nodes[ci].accumulated.visible = false; // node+1504
        }

        // Clear per-node eval data dirty flags (0x6BBD44..0x6BBDF4).
        // Binary: *(v98+48) = 0 for each entry in player+384 array.
        for (auto &evalData : _runtime->perNodeEvalData) {
            evalData.dirtyFlag = 0;
        }

    }

    void Player::calcBounds() {
        ensureMotionLoaded();
        ensureNodeTreeBuilt();
        const auto motionPath =
            _runtime && _runtime->activeMotion ? _runtime->activeMotion->path
                                               : std::string{};

        _boundsMinX = 1e308;
        _boundsMinY = 1e308;
        _boundsMaxX = -1e308;
        _boundsMaxY = -1e308;

        bool haveBounds = false;
        auto mergeBounds = [&](double minX, double minY, double maxX, double maxY) {
            if(minX > maxX || minY > maxY) {
                return;
            }
            if(!haveBounds) {
                _boundsMinX = minX;
                _boundsMinY = minY;
                _boundsMaxX = maxX;
                _boundsMaxY = maxY;
                haveBounds = true;
                return;
            }
            if(minX < _boundsMinX) _boundsMinX = minX;
            if(minY < _boundsMinY) _boundsMinY = minY;
            if(maxX > _boundsMaxX) _boundsMaxX = maxX;
            if(maxY > _boundsMaxY) _boundsMaxY = maxY;
        };

        for(auto &node : _runtime->nodes) {
            node.bounds[0] = 1.0f;
            node.bounds[1] = 1.0f;
            node.bounds[2] = -1.0f;
            node.bounds[3] = -1.0f;

            if(!node.accumulated.active || !node.hasSource || !node.drawFlag) {
                continue;
            }

            bool haveNodeBounds = false;
            double minX = 0.0;
            double minY = 0.0;
            double maxX = 0.0;
            double maxY = 0.0;
            auto extendPoint = [&](double x, double y) {
                if(!haveNodeBounds) {
                    minX = maxX = x;
                    minY = maxY = y;
                    haveNodeBounds = true;
                    return;
                }
                if(x < minX) minX = x;
                if(y < minY) minY = y;
                if(x > maxX) maxX = x;
                if(y > maxY) maxY = y;
            };

            if(!node.meshControlPoints.empty()) {
                for(size_t pi = 0; pi + 1 < node.meshControlPoints.size(); pi += 2) {
                    extendPoint(node.meshControlPoints[pi],
                                node.meshControlPoints[pi + 1]);
                }
            } else if(node.clipW > 0.0 || node.clipH > 0.0) {
                for(int ci = 0; ci < 4; ++ci) {
                    extendPoint(node.vertices[ci * 2], node.vertices[ci * 2 + 1]);
                }
            } else {
                extendPoint(node.vertexPosX, node.vertexPosY);
            }

            if(!haveNodeBounds) {
                continue;
            }

            const std::array<float, 4> expectedBounds = {
                static_cast<float>(std::floor(minX)),
                static_cast<float>(std::floor(minY)),
                static_cast<float>(std::ceil(maxX)),
                static_cast<float>(std::ceil(maxY))
            };
            node.bounds[0] = expectedBounds[0];
            node.bounds[1] = expectedBounds[1];
            node.bounds[2] = expectedBounds[2];
            node.bounds[3] = expectedBounds[3];
            mergeBounds(node.bounds[0], node.bounds[1],
                        node.bounds[2], node.bounds[3]);
            if(detail::logoChainTraceEnabled(_runtime->activeMotion)) {
                const std::array<float, 4> actualBounds = {
                    node.bounds[0], node.bounds[1], node.bounds[2], node.bounds[3]
                };
                bool ok = true;
                for(size_t bi = 0; bi < expectedBounds.size(); ++bi) {
                    if(std::fabs(expectedBounds[bi] - actualBounds[bi]) > 0.01f) {
                        ok = false;
                        break;
                    }
                }
                detail::logoChainTraceCheck(
                    motionPath, "calcBounds.node", "0x6C3D04",
                    _clampedEvalTime,
                    fmt::format(
                        "from=minmax({:.3f},{:.3f},{:.3f},{:.3f}) exp=[{:.3f},{:.3f},{:.3f},{:.3f}]",
                        minX, minY, maxX, maxY,
                        expectedBounds[0], expectedBounds[1],
                        expectedBounds[2], expectedBounds[3]),
                    fmt::format(
                        "nodeIndex={} label={} act=[{:.3f},{:.3f},{:.3f},{:.3f}]",
                        node.index,
                        node.layerName.empty() ? std::string("<root>")
                                               : node.layerName,
                        actualBounds[0], actualBounds[1],
                        actualBounds[2], actualBounds[3]),
                    ok,
                    "Player_calcBounds produced an unexpected node AABB");
            }
        }

        for(size_t ni = 1; ni < _runtime->nodes.size(); ++ni) {
            auto &node = _runtime->nodes[ni];
            if(node.nodeType == 3) {
                if(auto *child = node.getChildPlayer()) {
                    child->calcBounds();
                    mergeBounds(child->_boundsMinX, child->_boundsMinY,
                                child->_boundsMaxX, child->_boundsMaxY);
                }
            } else if(node.nodeType == 4) {
                const int particleCount = node.getParticleCount();
                for(int pi = 0; pi < particleCount; ++pi) {
                    if(auto *child = node.getParticleChild(pi)) {
                        child->calcBounds();
                        mergeBounds(child->_boundsMinX, child->_boundsMinY,
                                    child->_boundsMaxX, child->_boundsMaxY);
                    }
                }
            }
        }

        if(!haveBounds) {
            _boundsMinX = 0.0;
            _boundsMinY = 0.0;
            _boundsMaxX = 0.0;
            _boundsMaxY = 0.0;
        }
        detail::logoChainTraceLogf(
            motionPath, "calcBounds.player", "0x6C3D04", _clampedEvalTime,
            "playerBounds=({:.3f},{:.3f},{:.3f},{:.3f}) haveBounds={}",
            _boundsMinX, _boundsMinY, _boundsMaxX, _boundsMaxY,
            haveBounds ? 1 : 0);
    }

    void Player::appendPreparedRenderItems() {
        if(!_runtime || !_runtime->activeMotion) {
            return;
        }

        auto &entries = _runtime->preparedRenderItems;
        const auto &nodes = _runtime->nodes;
        const int bitmask = _runtime->isEmoteMode ? 5193 : 5185;
        const auto &dam = _runtime->drawAffineMatrix;
        std::unordered_set<int> requiredGroupNodeIndices;

        auto transformPoint = [&](float x, float y) -> tTVPPointD {
            return {
                dam[0] * static_cast<double>(x) + dam[2] * static_cast<double>(y) + dam[4],
                dam[1] * static_cast<double>(x) + dam[3] * static_cast<double>(y) + dam[5]
            };
        };

        constexpr std::array<float, 4> kInvalidPreparedPaintBox = {
            1.0f, 1.0f, -1.0f, -1.0f
        };

        auto updatePaintBox = [](detail::PlayerRuntime::PreparedRenderItem &entry,
                                 double x, double y, bool firstPoint) {
            const float fx = static_cast<float>(x);
            const float fy = static_cast<float>(y);
            if(firstPoint) {
                entry.paintBox = { fx, fy, fx, fy };
                return;
            }
            if(fx < entry.paintBox[0]) entry.paintBox[0] = fx;
            if(fy < entry.paintBox[1]) entry.paintBox[1] = fy;
            if(fx > entry.paintBox[2]) entry.paintBox[2] = fx;
            if(fy > entry.paintBox[3]) entry.paintBox[3] = fy;
        };

        for(size_t i = 0; i < nodes.size(); ++i) {
            const auto &node = nodes[i];
            if(!node.accumulated.active) continue;
            if(!node.forceVisible && (((1 << node.nodeType) & bitmask) == 0)) {
                continue;
            }
            if(!node.hasSource || node.interpolatedCache.src.empty()) continue;

            for(int ancestorIndex = node.visibleAncestorIndex;
                ancestorIndex >= 0 &&
                ancestorIndex < static_cast<int>(nodes.size()); ) {
                const auto &ancestor = nodes[ancestorIndex];
                const bool isSpecialCompositeParent =
                    ancestor.nodeType == 12 && (ancestor.stencilType & 4) != 0;
                const auto inserted = isSpecialCompositeParent
                    ? requiredGroupNodeIndices.insert(ancestorIndex)
                    : std::pair<std::unordered_set<int>::iterator, bool>{
                          requiredGroupNodeIndices.end(), false
                      };
                const int nextAncestorIndex = ancestor.visibleAncestorIndex;
                if(!inserted.second || nextAncestorIndex == ancestorIndex) {
                    if(!isSpecialCompositeParent && nextAncestorIndex != ancestorIndex) {
                        ancestorIndex = nextAncestorIndex;
                        continue;
                    }
                    break;
                }
                ancestorIndex = nextAncestorIndex;
            }
        }

        for(size_t i = 0; i < nodes.size(); ++i) {
            const auto &node = nodes[i];
            if(!node.accumulated.active) continue;
            const bool hasOwnSource =
                node.hasSource && !node.interpolatedCache.src.empty();
            const bool needsGroupEntry =
                requiredGroupNodeIndices.find(static_cast<int>(i)) !=
                requiredGroupNodeIndices.end();
            if(!needsGroupEntry &&
               !node.forceVisible &&
               (((1 << node.nodeType) & bitmask) == 0)) {
                continue;
            }
            if(!hasOwnSource && !needsGroupEntry) continue;

            detail::PlayerRuntime::PreparedRenderItem entry;
            entry.nodeIndex = static_cast<int>(i);
            entry.hasOwnSource = hasOwnSource;
            entry.groupOnly = !hasOwnSource && needsGroupEntry;
            if(hasOwnSource) {
                entry.sourceKey = node.interpolatedCache.src;
                entry.srcRef = findSource(detail::widen(entry.sourceKey));
            }
            // Aligned to sub_6D5164 -> sub_6C2334:
            // top-level build uses arg4=0, so render-item draw flag becomes
            // node+1960 ? 1 : node+1961. node+1961 is the post-build
            // stencilComposite mask-layer reference flag.
            entry.drawFlag =
                node.drawFlag || node.stencilCompositeMaskReferenced ||
                needsGroupEntry;
            entry.sortKey = node.priorDraw != 0
                ? static_cast<double>(node.priorDraw)
                : _priorDraw;
            entry.blendMode = node.accumulated.blendMode;
            entry.packedColors = copyPackedColorsFromBytes(node.colorBytes);
            entry.opacity = node.accumulated.opacity;
            entry.updateCount = node.stencilType;
            entry.visibleAncestorIndex = node.visibleAncestorIndex;
            entry.meshType = node.meshType;
            entry.meshDivX = node.meshDivX;
            entry.meshDivY = node.meshDivY;

            bool havePaintBox = false;
            if(hasOwnSource && node.clipW > 0.0 && node.clipH > 0.0) {
                for(int ci = 0; ci < 4; ++ci) {
                    const auto pt = transformPoint(node.vertices[ci * 2],
                                                   node.vertices[ci * 2 + 1]);
                    entry.corners[ci * 2] = static_cast<float>(pt.x);
                    entry.corners[ci * 2 + 1] = static_cast<float>(pt.y);
                    updatePaintBox(entry, pt.x, pt.y, !havePaintBox);
                    havePaintBox = true;
                }
            }

            if(hasOwnSource && !node.meshControlPoints.empty()) {
                entry.meshPoints.resize(node.meshControlPoints.size());
                for(size_t pi = 0; pi + 1 < node.meshControlPoints.size(); pi += 2) {
                    const auto pt = transformPoint(node.meshControlPoints[pi],
                                                   node.meshControlPoints[pi + 1]);
                    entry.meshPoints[pi] = static_cast<float>(pt.x);
                    entry.meshPoints[pi + 1] = static_cast<float>(pt.y);
                    updatePaintBox(entry, pt.x, pt.y, !havePaintBox);
                    havePaintBox = true;
                }
            }

            if(!havePaintBox
               && hasOwnSource
               && node.bounds[2] >= node.bounds[0]
               && node.bounds[3] >= node.bounds[1]) {
                const auto p0 = transformPoint(node.bounds[0], node.bounds[1]);
                const auto p1 = transformPoint(node.bounds[2], node.bounds[1]);
                const auto p2 = transformPoint(node.bounds[2], node.bounds[3]);
                const auto p3 = transformPoint(node.bounds[0], node.bounds[3]);
                entry.paintBox = {
                    static_cast<float>(std::floor(std::min(std::min(p0.x, p1.x),
                                                          std::min(p2.x, p3.x)))),
                    static_cast<float>(std::floor(std::min(std::min(p0.y, p1.y),
                                                          std::min(p2.y, p3.y)))),
                    static_cast<float>(std::ceil(std::max(std::max(p0.x, p1.x),
                                                         std::max(p2.x, p3.x)))),
                    static_cast<float>(std::ceil(std::max(std::max(p0.y, p1.y),
                                                         std::max(p2.y, p3.y))))
                };
                havePaintBox = true;
            }

            if(!havePaintBox) {
                // libkrkr2.so sub_6C2334 initializes item+200..212 from
                // node+1936 when present, otherwise from xmmword_14D7C60.
                // That default is {1,1,-1,-1}, i.e. an invalid rect sentinel,
                // not a point box at vertexPos. Group-only items rely on this
                // invalid sentinel so the later child-union pass can replace
                // the parent paintBox with the first real child bounds instead
                // of being permanently anchored at (0,0).
                entry.paintBox = kInvalidPreparedPaintBox;
            } else {
                entry.paintBox = {
                    static_cast<float>(std::floor(entry.paintBox[0])),
                    static_cast<float>(std::floor(entry.paintBox[1])),
                    static_cast<float>(std::ceil(entry.paintBox[2])),
                    static_cast<float>(std::ceil(entry.paintBox[3]))
                };
            }

            if(node.parentClipIndex >= 0
               && node.parentClipIndex < static_cast<int>(nodes.size())) {
                const auto &clipNode = nodes[node.parentClipIndex];
                if(clipNode.shapeAABB[2] >= clipNode.shapeAABB[0]
                   && clipNode.shapeAABB[3] >= clipNode.shapeAABB[1]) {
                    const auto p0 = transformPoint(clipNode.shapeAABB[0], clipNode.shapeAABB[1]);
                    const auto p1 = transformPoint(clipNode.shapeAABB[2], clipNode.shapeAABB[1]);
                    const auto p2 = transformPoint(clipNode.shapeAABB[2], clipNode.shapeAABB[3]);
                    const auto p3 = transformPoint(clipNode.shapeAABB[0], clipNode.shapeAABB[3]);
                    entry.viewport = {
                        static_cast<float>(std::min(std::min(p0.x, p1.x), std::min(p2.x, p3.x))),
                        static_cast<float>(std::min(std::min(p0.y, p1.y), std::min(p2.y, p3.y))),
                        static_cast<float>(std::max(std::max(p0.x, p1.x), std::max(p2.x, p3.x))),
                        static_cast<float>(std::max(std::max(p0.y, p1.y), std::max(p2.y, p3.y)))
                    };
                    entry.hasViewport = true;
                }
            }

            if(detail::logoChainTraceEnabled(_runtime->activeMotion)) {
                const auto motionPath = _runtime->activeMotion->path;
                const std::array<float, 8> expectedCorners = {
                    static_cast<float>(dam[0] * static_cast<double>(node.vertices[0]) +
                                       dam[2] * static_cast<double>(node.vertices[1]) +
                                       dam[4]),
                    static_cast<float>(dam[1] * static_cast<double>(node.vertices[0]) +
                                       dam[3] * static_cast<double>(node.vertices[1]) +
                                       dam[5]),
                    static_cast<float>(dam[0] * static_cast<double>(node.vertices[2]) +
                                       dam[2] * static_cast<double>(node.vertices[3]) +
                                       dam[4]),
                    static_cast<float>(dam[1] * static_cast<double>(node.vertices[2]) +
                                       dam[3] * static_cast<double>(node.vertices[3]) +
                                       dam[5]),
                    static_cast<float>(dam[0] * static_cast<double>(node.vertices[4]) +
                                       dam[2] * static_cast<double>(node.vertices[5]) +
                                       dam[4]),
                    static_cast<float>(dam[1] * static_cast<double>(node.vertices[4]) +
                                       dam[3] * static_cast<double>(node.vertices[5]) +
                                       dam[5]),
                    static_cast<float>(dam[0] * static_cast<double>(node.vertices[6]) +
                                       dam[2] * static_cast<double>(node.vertices[7]) +
                                       dam[4]),
                    static_cast<float>(dam[1] * static_cast<double>(node.vertices[6]) +
                                       dam[3] * static_cast<double>(node.vertices[7]) +
                                       dam[5])
                };
                const auto effectiveColor = unpackPackedRgba(entry.packedColors[0]);
                detail::logoChainTraceLogf(
                    motionPath, "prepare.item", "0x6C2334",
                    _clampedEvalTime,
                    "nodeIndex={} src={} blend={} opacity={} packedColor=[0x{:08x},0x{:08x},0x{:08x},0x{:08x}] effectiveColor=[{},{},{},{}] meshType={} meshDiv=({},{}) sortKey={:.3f} nodeDrawFlag={} maskRef={} itemDrawFlag={} visibleAncestorIndex={} slotDone={} frameType={} stencilBase={} stencilType={}",
                    entry.nodeIndex,
                    entry.sourceKey.empty() ? std::string("<none>")
                                            : entry.sourceKey,
                    entry.blendMode, entry.opacity,
                    entry.packedColors[0], entry.packedColors[1],
                    entry.packedColors[2], entry.packedColors[3],
                    effectiveColor[0], effectiveColor[1],
                    effectiveColor[2], effectiveColor[3],
                    entry.meshType, entry.meshDivX,
                    entry.meshDivY, entry.sortKey,
                    node.drawFlag ? 1 : 0,
                    node.stencilCompositeMaskReferenced ? 1 : 0,
                    entry.drawFlag ? 1 : 0,
                    entry.visibleAncestorIndex,
                    node.activeSlot().done ? 1 : 0,
                    node.currentFrameType,
                    node.stencilTypeBase,
                    node.stencilType);
                bool cornersOk = node.clipW <= 0.0 && node.clipH <= 0.0;
                if(!cornersOk) {
                    cornersOk = true;
                    for(size_t ci = 0; ci < expectedCorners.size(); ++ci) {
                        if(std::fabs(entry.corners[ci] - expectedCorners[ci]) >
                           0.01f) {
                            cornersOk = false;
                            break;
                        }
                    }
                }
                detail::logoChainTraceCheck(
                    motionPath, "prepare.corners", "0x6C2334",
                    _clampedEvalTime,
                    fmt::format(
                        "drawAffine*vertices exp=[{:.3f},{:.3f},{:.3f},{:.3f},{:.3f},{:.3f},{:.3f},{:.3f}]",
                        expectedCorners[0], expectedCorners[1],
                        expectedCorners[2], expectedCorners[3],
                        expectedCorners[4], expectedCorners[5],
                        expectedCorners[6], expectedCorners[7]),
                    fmt::format(
                        "nodeIndex={} act=[{:.3f},{:.3f},{:.3f},{:.3f},{:.3f},{:.3f},{:.3f},{:.3f}]",
                        entry.nodeIndex, entry.corners[0], entry.corners[1],
                        entry.corners[2], entry.corners[3], entry.corners[4],
                        entry.corners[5], entry.corners[6], entry.corners[7]),
                    cornersOk,
                    "PreparedRenderItem corners diverged from drawAffineMatrix * node.vertices");
                detail::logoChainTraceCheck(
                    motionPath, "prepare.paintBox", "0x6C2334",
                    _clampedEvalTime,
                    fmt::format(
                        "paintBox from transformed geometry exp=[{:.3f},{:.3f},{:.3f},{:.3f}]",
                        entry.paintBox[0], entry.paintBox[1],
                        entry.paintBox[2], entry.paintBox[3]),
                    fmt::format(
                        "nodeIndex={} act=[{:.3f},{:.3f},{:.3f},{:.3f}]",
                        entry.nodeIndex, entry.paintBox[0], entry.paintBox[1],
                        entry.paintBox[2], entry.paintBox[3]),
                    true,
                    "PreparedRenderItem paintBox diverged from transformed geometry");
                detail::logoChainTraceCheck(
                    motionPath, "prepare.viewport", "0x6C2334",
                    _clampedEvalTime,
                    entry.hasViewport
                        ? fmt::format("parent shapeAABB chain exp=[{:.3f},{:.3f},{:.3f},{:.3f}]",
                                      entry.viewport[0], entry.viewport[1],
                                      entry.viewport[2], entry.viewport[3])
                        : std::string("parent shapeAABB chain exp=<invalid default>"),
                    entry.hasViewport
                        ? fmt::format("nodeIndex={} act=[{:.3f},{:.3f},{:.3f},{:.3f}]",
                                      entry.nodeIndex, entry.viewport[0],
                                      entry.viewport[1], entry.viewport[2],
                                      entry.viewport[3])
                        : fmt::format("nodeIndex={} act=<invalid default>",
                                      entry.nodeIndex),
                    true,
                    "PreparedRenderItem viewport propagation diverged from parent clip chain");
            }

            entries.push_back(std::move(entry));
        }

        if(entries.empty()) {
            return;
        }

        std::unordered_map<int, size_t> entryIndexByNode;
        entryIndexByNode.reserve(entries.size());
        for(size_t i = 0; i < entries.size(); ++i) {
            entryIndexByNode.emplace(entries[i].nodeIndex, i);
        }

        auto unionPaintBox =
            [](detail::PlayerRuntime::PreparedRenderItem &parent,
               const detail::PlayerRuntime::PreparedRenderItem &child) {
                if(child.paintBox[2] < child.paintBox[0] ||
                   child.paintBox[3] < child.paintBox[1]) {
                    return;
                }
                if(parent.paintBox[2] < parent.paintBox[0] ||
                   parent.paintBox[3] < parent.paintBox[1]) {
                    parent.paintBox = child.paintBox;
                    return;
                }
                parent.paintBox[0] = std::min(parent.paintBox[0], child.paintBox[0]);
                parent.paintBox[1] = std::min(parent.paintBox[1], child.paintBox[1]);
                parent.paintBox[2] = std::max(parent.paintBox[2], child.paintBox[2]);
                parent.paintBox[3] = std::max(parent.paintBox[3], child.paintBox[3]);
            };

        for(const auto &childEntry : entries) {
            for(int ancestorIndex = childEntry.visibleAncestorIndex;
                ancestorIndex >= 0; ) {
                const auto parentIt = entryIndexByNode.find(ancestorIndex);
                if(parentIt == entryIndexByNode.end()) {
                    break;
                }
                auto &parentEntry = entries[parentIt->second];
                const auto &ancestorNode = nodes[parentEntry.nodeIndex];
                unionPaintBox(parentEntry, childEntry);
                const int nextAncestorIndex = ancestorNode.visibleAncestorIndex;
                if(nextAncestorIndex == ancestorIndex) {
                    break;
                }
                ancestorIndex = nextAncestorIndex;
            }
        }
    }

    bool Player::prepareRenderItems() {
        if(!_runtime) {
            return false;
        }

        _runtime->preparedRenderItems.clear();
        const auto motionPath =
            _runtime->activeMotion ? _runtime->activeMotion->path : std::string{};

        auto prependChildEntries = [&](Player *child) {
            if(!child || !child->_runtime) {
                return;
            }
            child->prepareRenderItems();
            auto &childEntries = child->_runtime->preparedRenderItems;
            if(childEntries.empty()) {
                return;
            }
            // Aligned to sub_6F363C call sites (0x6BE2C0 / 0x6C1A00):
            // child render items are inserted at BEGIN before the parent items.
            _runtime->preparedRenderItems.insert(
                _runtime->preparedRenderItems.begin(),
                std::make_move_iterator(childEntries.begin()),
                std::make_move_iterator(childEntries.end()));
            detail::logoChainTraceLogf(
                motionPath, "prepare.childMerge", "0x6F363C",
                _clampedEvalTime,
                "childMotionPath={} insertedAtBegin={} parentTotalAfterInsert={}",
                child->_runtime->activeMotion
                    ? child->_runtime->activeMotion->path
                    : std::string("<none>"),
                childEntries.size(), _runtime->preparedRenderItems.size());
            childEntries.clear();
        };

        // Aligned to sub_6C2334: nodeType 3/4 child-player recursion is gated
        // by player+1092 (preview). The native code only expands these child
        // render lists when preview == 0.
        if(!_preview) {
            for(size_t ni = 1; ni < _runtime->nodes.size(); ++ni) {
                auto &node = _runtime->nodes[ni];
                if(node.nodeType == 3) {
                    prependChildEntries(node.getChildPlayer());
                } else if(node.nodeType == 4) {
                    const int particleCount = node.getParticleCount();
                    for(int pi = 0; pi < particleCount; ++pi) {
                        prependChildEntries(node.getParticleChild(pi));
                    }
                }
            }
        }

        appendPreparedRenderItems();
        std::vector<double> beforeSortKeys;
        beforeSortKeys.reserve(_runtime->preparedRenderItems.size());
        for(const auto &item : _runtime->preparedRenderItems) {
            beforeSortKeys.push_back(item.sortKey);
        }
        // Aligned to sub_6D4F00 (0x6D4F00): compare render-item sort key.
        std::stable_sort(_runtime->preparedRenderItems.begin(),
            _runtime->preparedRenderItems.end(),
            [](const detail::PlayerRuntime::PreparedRenderItem &lhs,
               const detail::PlayerRuntime::PreparedRenderItem &rhs) {
                return lhs.sortKey < rhs.sortKey;
            });
        if(detail::logoChainTraceEnabled(_runtime->activeMotion)) {
            std::ostringstream beforeSort;
            std::ostringstream afterSort;
            for(size_t i = 0; i < beforeSortKeys.size(); ++i) {
                if(i) beforeSort << ",";
                beforeSort << beforeSortKeys[i];
            }
            for(size_t i = 0; i < _runtime->preparedRenderItems.size(); ++i) {
                if(i) afterSort << ",";
                afterSort << _runtime->preparedRenderItems[i].sortKey;
            }
            detail::logoChainTraceLogf(
                motionPath, "prepare.sort", "0x6D5164/0x6D4F00",
                _clampedEvalTime,
                "itemCount={} sortKeysBefore=[{}] sortKeysAfter=[{}]",
                _runtime->preparedRenderItems.size(), beforeSort.str(),
                afterSort.str());
        }
        return !_runtime->preparedRenderItems.empty();
    }

    void Player::applyPreparedRenderItemTranslateOffsets() {
        if(!_runtime) {
            return;
        }

        // Aligned to libkrkr2.so Player_applyTranslateOffset (0x6D5264):
        // normal path adds cameraOffset to prepared render items here.
        // Root position is already baked into node state during updateLayers.
        const double ofsX = static_cast<double>(_cameraOffsetX);
        const double ofsY = static_cast<double>(_cameraOffsetY);
        const auto motionPath =
            _runtime->activeMotion ? _runtime->activeMotion->path : std::string{};
        for(auto &entry : _runtime->preparedRenderItems) {
            const auto beforeCorners = entry.corners;
            const auto beforePaintBox = entry.paintBox;
            const auto beforeViewport = entry.viewport;
            const auto beforeMeshPoints = entry.meshPoints;
            for(size_t ci = 0; ci < entry.corners.size(); ci += 2) {
                entry.corners[ci] =
                    static_cast<float>(static_cast<double>(entry.corners[ci]) + ofsX);
                entry.corners[ci + 1] =
                    static_cast<float>(static_cast<double>(entry.corners[ci + 1]) + ofsY);
            }
            entry.paintBox[0] = static_cast<float>(static_cast<double>(entry.paintBox[0]) + ofsX);
            entry.paintBox[1] = static_cast<float>(static_cast<double>(entry.paintBox[1]) + ofsY);
            entry.paintBox[2] = static_cast<float>(static_cast<double>(entry.paintBox[2]) + ofsX);
            entry.paintBox[3] = static_cast<float>(static_cast<double>(entry.paintBox[3]) + ofsY);
            if(entry.hasViewport) {
                entry.viewport[0] =
                    static_cast<float>(static_cast<double>(entry.viewport[0]) + ofsX);
                entry.viewport[1] =
                    static_cast<float>(static_cast<double>(entry.viewport[1]) + ofsY);
                entry.viewport[2] =
                    static_cast<float>(static_cast<double>(entry.viewport[2]) + ofsX);
                entry.viewport[3] =
                    static_cast<float>(static_cast<double>(entry.viewport[3]) + ofsY);
            }
            for(size_t pi = 0; pi + 1 < entry.meshPoints.size(); pi += 2) {
                entry.meshPoints[pi] =
                    static_cast<float>(static_cast<double>(entry.meshPoints[pi]) + ofsX);
                entry.meshPoints[pi + 1] =
                    static_cast<float>(static_cast<double>(entry.meshPoints[pi + 1]) + ofsY);
            }
            if(detail::logoChainTraceEnabled(_runtime->activeMotion)) {
                bool ok = true;
                for(size_t ci = 0; ci < entry.corners.size(); ci += 2) {
                    if(std::fabs((entry.corners[ci] - beforeCorners[ci]) -
                                 static_cast<float>(ofsX)) > 0.01f ||
                       std::fabs((entry.corners[ci + 1] - beforeCorners[ci + 1]) -
                                 static_cast<float>(ofsY)) > 0.01f) {
                        ok = false;
                        break;
                    }
                }
                if(ok && entry.hasViewport) {
                    for(size_t vi = 0; vi < entry.viewport.size(); vi += 2) {
                        if(std::fabs((entry.viewport[vi] - beforeViewport[vi]) -
                                     static_cast<float>(ofsX)) > 0.01f ||
                           std::fabs((entry.viewport[vi + 1] -
                                      beforeViewport[vi + 1]) -
                                     static_cast<float>(ofsY)) > 0.01f) {
                            ok = false;
                            break;
                        }
                    }
                }
                if(ok) {
                    for(size_t pi = 0; pi + 1 < entry.meshPoints.size(); pi += 2) {
                        if(std::fabs((entry.meshPoints[pi] - beforeMeshPoints[pi]) -
                                     static_cast<float>(ofsX)) > 0.01f ||
                           std::fabs((entry.meshPoints[pi + 1] -
                                      beforeMeshPoints[pi + 1]) -
                                     static_cast<float>(ofsY)) > 0.01f) {
                            ok = false;
                            break;
                        }
                    }
                }
                detail::logoChainTraceCheck(
                    motionPath, "prepare.translate", "0x6D5264",
                    _clampedEvalTime,
                    fmt::format("cameraOffset=({:.3f},{:.3f}) applied to corners/paintBox/viewport/mesh", ofsX, ofsY),
                    fmt::format(
                        "nodeIndex={} beforeCorner0=({:.3f},{:.3f}) afterCorner0=({:.3f},{:.3f}) beforePaintBox=[{:.3f},{:.3f},{:.3f},{:.3f}] afterPaintBox=[{:.3f},{:.3f},{:.3f},{:.3f}]",
                        entry.nodeIndex, beforeCorners[0], beforeCorners[1],
                        entry.corners[0], entry.corners[1],
                        beforePaintBox[0], beforePaintBox[1], beforePaintBox[2],
                        beforePaintBox[3], entry.paintBox[0], entry.paintBox[1],
                        entry.paintBox[2], entry.paintBox[3]),
                    ok,
                    "Player_applyTranslateOffset added more than cameraOffset");
            }
        }
    }

} // namespace motion
