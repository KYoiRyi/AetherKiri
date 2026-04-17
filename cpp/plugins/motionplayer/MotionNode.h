//
// Persistent per-node state for MotionPlayer rendering pipeline.
// Aligned to libkrkr2.so 2632-byte node structure in std::deque.
//
// PSB key → node offset mapping (from IDA decompilation of sub_6B3C78 at 0x6B3C78):
//   "label"            → node+0   (name)
//   "type"             → node+28  (nodeType)
//   "coordinate"       → node+24  (coordinateMode)
//   "inheritMask"      → node+40  (inheritFlags, bits 2-8, default 0x1FC)
//   "groundCorrection" → node+47  (bool)
//   "transformOrder"   → node+84..96 (4 ints, default [0,1,2,3])
//   "frameList"        → node+64  (PSB variant for keyframes)
//   "meshTransform"    → node+2000 (meshType)
//   "stencilType"      → node+52
//   parentIndex        → node+36  (set during tree walk)
//   node+344, node+880 → clip slot done flags (initialized to 1=done)
//
#pragma once

#include <array>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "tjs.h"  // tTJSVariant, iTJSDispatch2 for TJS↔Native bridge (node+1912, node+2296)

namespace PSB {
    class PSBDictionary;
}

namespace motion {
    class Player;
}

namespace motion::detail {

    struct MotionNode {
        // Identity (from PSB, set once during tree build)
        int index = 0;
        int parentIndex = -1;          // node+36
        int nodeType = 0;              // node+28
        int coordinateMode = 0;        // node+24
        int inheritFlags = 0x1FC;      // node+40. Player_updateLayers (0x6BB33C)
                                        // also tests byte(node+42)&0x40, i.e.
                                        // inheritMask bit 0x00400000.
        uint8_t flags = 0;             // node+44 (sub_6BE0C0 at 0x6BE37C, sub_6BF0DC at 0x6BF310)
        bool groundCorrection = false; // node+47
        // TJS layer dispatch object for callbacks (sub_6BAA10 onGroundCorrection).
        // In libkrkr2.so this is at *(node+0)+16 (the layer's iTJSDispatch2*).
        // Stored as void* to avoid iTJSDispatch2 header dependency here;
        // cast to iTJSDispatch2* in Player.cpp where tjs.h is included.
        void *tjsLayerObject = nullptr;  // non-owning reference
        int transformOrder[4] = {0, 1, 2, 3}; // node+84..96
        bool hasSource = false;        // has any frame with non-empty src
        std::string layerName;         // "label" from PSB
        int meshType = 0;             // "meshTransform" from PSB (node+2000)
        int meshFlags = 0;            // "meshSyncChildMask" from PSB (node+2004)
        int meshDivision = 0;         // "meshDivision" from PSB (node+2008)
        int meshDivX = 0;             // node+2012: computed grid width
        int meshDivY = 0;             // node+2016: computed grid height
        // Mesh inverse matrix for sub_69AE74 child deformation (node+2096..2132)
        double meshInvM11 = 0, meshInvM12 = 0;  // node+2096, node+2104
        double meshInvM21 = 0, meshInvM22 = 0;  // node+2112, node+2120
        float meshInvOffX = 0, meshInvOffY = 0;  // node+2128, node+2132
        // Computed mesh flags (sub_6BC4F0 at 0x6BC6E4..0x6BC818)
        bool hasMeshData = false;        // node+1962: has active mesh data
        bool stencilCompositeMaskReferenced = false; // node+1961: post-build mask-layer reference
        bool meshCombineEnabled = false; // node+1963: mesh combines with children
        // libkrkr2.so seeds node+52 from PSB "stencilType" in Player_initNodeFields
        // (0x6B3C78), but later visibility/render-tree stages consume the same slot as
        // a per-frame non-zero update mask while still preserving deflector bit 4.
        // Keep both the raw PSB seed and the runtime-composed value explicitly.
        int stencilTypeBase = 0;      // raw PSB "stencilType"
        int stencilType = 0;          // runtime node+52-compatible mask
        int currentFrameType = 0;     // current frameList type (0/2/3), for trace

        // Mesh control points (node+2024..2032 in libkrkr2.so).
        // For meshType=1: 16 × 2 floats (Bezier patch 4×4 control grid) = 32 floats.
        // For meshType=2: (divX+1)*(divY+1)*2 floats (grid mesh).
        // Built by sub_6BC4F0 vertex computation.
        std::vector<float> meshControlPoints;      // node+2024
        std::vector<float> meshControlPointsPrev;  // node+2048 (previous frame)

        // emoteEdit PSB dict reference (node+1980, sub_6B3C78 at 0x6B3D48)
        std::shared_ptr<const PSB::PSBDictionary> emoteEditDict;

        // Prior draw flag (node+48, from PSB emoteEdit "priorDraw")
        // Raw int, not bool — binary checks bit flags (v12 & 5) in sub_6BE0C0.
        int priorDraw = 0;

        // ========== Dual Clip Slot Architecture ==========
        // Aligned to libkrkr2.so's two 536-byte clip slots per node:
        //   slot0 @ node+320, slot1 @ node+856, activeSlotIndex @ node+1392.
        // When a new motion is played, data is written to the inactive slot,
        // then activeSlotIndex ^= 1 flips. The old slot is preserved for
        // crossfade blending.
        //
        // CurveData is a lightweight bezier curve container compatible with
        // the evaluateBezierCurve() template (requires .x, .y, .empty()).
        // BezierCurve/ControlPointCurve types are defined in PlayerInternal.h
        // which MotionNode.h cannot include, so we use this standalone type.
        struct CurveData {
            std::vector<double> x, y;
            bool empty() const { return x.empty(); }
        };
        struct ControlPointData {
            std::vector<double> x, y, t;
            bool empty() const { return t.empty(); }
        };

        struct ClipSlot {
            // Visibility (slot+24, slot+25)
            bool done = true;
            bool crossfading = false;      // slot+25: currently blending with other slot
            bool hasEasing = false;         // slot+544: has easing curve for crossfade

            // Source (slot+36)
            std::string src;
            std::vector<std::string> srcList;

            // Position (slot+96..112)
            double x = 0, y = 0, z = 0;
            double ox = 0, oy = 0;

            // Transform
            double width = 0, height = 0;
            double opacity = 1.0;
            double angle = 0.0;
            double scaleX = 1.0, scaleY = 1.0;
            double slantX = 0.0, slantY = 0.0;
            bool flipX = false, flipY = false;
            int blendMode = 16;
            std::array<std::uint32_t, 4> packedColors{
                0xFF808080u, 0xFF808080u, 0xFF808080u, 0xFF808080u
            };

            // Easing curves (slot+168, +208, +228, +248, +268, +296)
            CurveData ccc, acc, zcc, scc, occ, cc;
            ControlPointData cp;
            bool hasCpRotation = false;

            // Time (slot+328)
            double clipStartTime = 0.0;

            // Motion sub-object (mask 0x80000)
            int motionDt = 0, motionFlags = 0;
            double motionDofst = 0.0;
            bool motionDocmpl = false;
            double motionTimeOffset = 0.0;
            std::string motionDtgt;

            // Particle sub-object (mask 0x100000)
            int prtTrigger = 0;
            double prtFmin = 10.0, prtF = 10.0;
            double prtVmin = 0.0, prtV = 0.0;
            double prtAmin = 0.0, prtA = 0.0;
            double prtZmin = 1.0, prtZ = 1.0;
            double prtRange = 0.0;

            // TransformOrder
            bool hasTransformOrder = false;
            int transformOrder[4] = {0,1,2,3};
            std::string action;
            bool hasSync = false;
        };

        ClipSlot slots[2];
        int activeSlotIndex = 0;       // node+1392
        ClipSlot& activeSlot() { return slots[activeSlotIndex]; }
        const ClipSlot& activeSlot() const { return slots[activeSlotIndex]; }
        ClipSlot& otherSlot() { return slots[activeSlotIndex ^ 1]; }
        const ClipSlot& otherSlot() const { return slots[activeSlotIndex ^ 1]; }

        // PSB reference (for evaluateLayerContent calls)
        std::shared_ptr<const PSB::PSBDictionary> psbNode;

        // Per-node source/override state.
        // Aligned to libkrkr2.so node+0x630..0x678 block consumed by
        // Player_updateLayers (0x6BB630..0x6BB700). Root setters write here
        // (e.g. Player_setRootX 0x6CD028, Player_setRootFlipX 0x6CD068), and
        // the main loop folds it into the working block after timeline eval.
        struct LocalState {
            bool visible = false;
            bool active = false;
            bool dirty = false;
            bool flipX = false;
            bool flipY = false;
            double posX = 0.0;
            double posY = 0.0;
            double posZ = 0.0;
            double angle = 0.0;
            double scaleX = 1.0;
            double scaleY = 1.0;
            double slantX = 0.0;
            double slantY = 0.0;
            int opacity = 255;
            int blendMode = 16;
        } localState;

        // Working/evaluated state (built during updateLayers inheritance loop)
        // Aligned to libkrkr2.so node+0x5E0..0x628 block written by
        // Player_evaluateTimeline (0x699AE4) and further composed by
        // Player_updateLayers (0x6BB33C).
        struct AccumulatedState {
            bool visible = false;
            bool active = false;
            bool dirty = false;     // node+1584: set when state changes, cleared by consumer
            bool flipX = false;
            bool flipY = false;
            double posX = 0.0;
            double posY = 0.0;
            double posZ = 0.0;
            double angle = 0.0;
            double scaleX = 1.0;
            double scaleY = 1.0;
            double slantX = 0.0;
            double slantY = 0.0;
            int opacity = 255;         // 0-255 integer, matching libkrkr2.so int math
            int blendMode = 16;        // node+1656: accumulated blend mode (default 0x10)
            // 2x2 matrix (local × parent accumulated)
            double m11 = 1.0;
            double m12 = 0.0;
            double m21 = 0.0;
            double m22 = 1.0;
        } accumulated;

        // Previous position (for delta computation in post-loop)
        double prevPosX = 0.0;
        double prevPosY = 0.0;
        double prevPosZ = 0.0;

        // Draw flag (computed in post-loop visibility pass, sub_6BD8DC)
        bool drawFlag = false;
        int forceVisible = 0;              // node+1996
        int visibleAncestorIndex = -1;     // replaces pointer at node+1952

        // Child Player for nodeType=3 (Motion).
        // Aligned to libkrkr2.so node+1912: tTJSVariant holding iTJSDispatch2-wrapped
        // Player object, created by sub_6B3C78 case 3 via sub_6F1794 (NCB CreateAdaptor).
        // Use getChildPlayer() helper to extract native Player*.
        tTJSVariant childPlayerVar;

        // Particle children for nodeType=4 (Particle).
        // Aligned to libkrkr2.so node+2296: tTJSVariant holding TJS Array of
        // iTJSDispatch2-wrapped Player objects, created by sub_6B3C78 case 4 via
        // sub_704CB8 (TJSCreateArrayObject).
        // Use getParticleCount()/getParticleChild(i)/addParticleChild()/eraseParticleChild()
        // helpers for Array operations matching sub_56C694/sub_6C1678.
        tTJSVariant particleArrayVar;

        // Shape type for nodeType=1 (from PSB "shape" key, sub_6B3C78 case 1)
        int shapeType = 0;             // node+32: 0=point, 1=circle, 2=rect, 3=quad

        // Shape AABB for nodeType=7 (sub_6BDCC0 at 0x6BDCC0)
        float shapeAABB[4] = {};       // node+2144: minX, minY, maxX, maxY

        // Shape geometry for nodeType=1 (sub_6BDE94 at 0x6BDE94)
        int shapeGeomType = 0;         // node+1664: stored shape type
        double shapeVertices[16] = {}; // node+1672..1784: shape geometry

        // Vertex-computed position (sub_6BC4F0 at 0x6BC4F0)
        double vertexPosX = 0.0;       // node+152
        double vertexPosY = 0.0;       // node+160
        double vertexPosZ = 0.0;       // node+168

        // Vertex output (sub_6BC4F0)
        float vertices[8] = {};        // node+1856..1884: 4 corners x 2 floats

        // Bounding box output (Player_calcBounds, 0x6C3D04)
        float bounds[4] = { 1.0f, 1.0f, -1.0f, -1.0f }; // node+1888..1900

        // Origin offsets (from PSB source icon, sub_6BC4F0)
        double originX = 0.0;          // node+248
        double originY = 0.0;          // node+256
        double clipW = 0.0;            // node+232
        double clipH = 0.0;            // node+240

        // Anchor node data for nodeType=10 (sub_6C0528 at 0x6C0528)
        int anchorType = 0;            // node+2376: "anchor" from PSB
        double anchorDamping = 1.0;    // node+2432: damping factor
        double anchorOpaScale = 1.0;   // node+2440: opacity damping scale
        // Anchor color channel scales (4 channels × gamma factor, node+2448..2504)
        double anchorColorScale[16] = {1,1,1,1, 1,1,1,1, 1,1,1,1, 1,1,1,1};

        // Camera constraint for nodeType=9 (sub_6BC000 at 0x6BC000)
        int cameraConstraintType = 0;  // node+2376: "anchor" type

        // (particleChildren replaced by particleArrayVar above — TJS Array)
        int particleType = 0;          // node+2164: particle subtype
        int particleMaxNum = 0;        // node+2168: max particle count
        // Binary: node+2192 is a SINGLE field used as both "accel ratio" (decay
        // factor in exponential velocity mode) and "camera damping" (copied to
        // child player). PSB key: "particleAccelRatio". See sub_6BF0DC.
        double particleAccelRatio = 0; // node+2192 — also used as cameraDamping
        bool particleInheritAngle = false; // node+2172
        int particleInheritVelocity = 0;   // node+2176
        int particleFlyDirection = 0;      // node+2180
        int particleApplyZoomToVelocity = 0; // node+2184
        bool particleDeleteOutside = false;  // node+2188
        bool particleTriVolume = false;    // node+2189: PSB "particleTriVolume", 3D particle flag
        // Previous frame matrix for change detection (node+2320..2336)
        double prevM11 = 1.0, prevM21 = 0.0;
        double prevM12 = 0.0, prevM22 = 1.0;
        double prevParticleAngle = 0.0;        // node+2352
        double emitterTimerAccum = 0.0;        // node+2360: frequency timer
        bool particleEmitterFlagActive = false; // v8[135].u8[0]

        // Particle emitter state for nodeType=6 (sub_6BEDD0 at 0x6BEDD0)
        bool emitterActive = false;    // node+2380
        double emitterTimer = 0.0;     // node+2392
        std::string emitterDtgt;       // resolved dtgt path (node+2384 stores ttstr)
        bool emitterOffsetActive = false; // node+2400
        double emitterOffsetX = 0.0;   // node+2408
        double emitterOffsetY = 0.0;   // node+2416
        double emitterOffsetZ = 0.0;   // node+2424

        // Delta position (post-loop: accumulated - prev, node+176/184/192)
        double deltaPosX = 0.0;
        double deltaPosY = 0.0;
        double deltaPosZ = 0.0;

        // Clip origin offsets (from clip slot+376/384, used by sub_6BC4F0)
        double clipOriginX = 0.0;      // node+248 local
        double clipOriginY = 0.0;      // node+256 local

        // Parent clip region index (replaces node+1936 pointer)
        int parentClipIndex = -1;

        // Anchor enabled flag (node+200 in sub_6C0528 context)
        bool anchorEnabled = false;

        // Color bytes for anchor damping (node+100..115: 4 sets of RGBA)
        uint8_t colorBytes[16] = {
            0x80, 0x80, 0x80, 0xFF,
            0x80, 0x80, 0x80, 0xFF,
            0x80, 0x80, 0x80, 0xFF,
            0x80, 0x80, 0x80, 0xFF
        };

        // Particle trigger type (from interpolatedCache, used by sub_6BEDD0)
        int prtTrigger = 0;

        // Per-frame interpolated data cached from evaluateLayerContent.
        // These are the fields needed by buildRenderListFromNodes that
        // come from FrameContentState (which lives in Player.cpp's
        // anonymous namespace and can't be referenced here).
        struct InterpolatedCache {
            std::string src;
            std::vector<std::string> srcList;  // node+2200: particle motion path array
            double width = 0.0;
            double height = 0.0;
            double opacity = 1.0;  // 0.0-1.0 as from PSB
            double angle = 0.0;
            double scaleX = 1.0;
            double scaleY = 1.0;
            double slantX = 0.0;
            double slantY = 0.0;
            bool flipX = false;
            bool flipY = false;
            double x = 0.0;
            double y = 0.0;
            double z = 0.0;
            double ox = 0.0;
            double oy = 0.0;
            int blendMode = 16;
            std::array<std::uint32_t, 4> packedColors{
                0xFF808080u, 0xFF808080u, 0xFF808080u, 0xFF808080u
            };
            bool hasTransformOrder = false;
            int transformOrder[4] = {0, 1, 2, 3};
            std::string action;
            bool hasSync = false;
            // Motion sub-object data from FrameContentState (mask 0x80000)
            int motionDt = 0;          // angleMode: 0=none,1=direct,2=atan2-delta,3=interpolated,4=target
            int motionFlags = 0;       // play flags from PSB "flags"
            double motionDofst = 0.0;  // angle value for case 1
            bool motionDocmpl = false;
            double motionTimeOffset = 0.0;
            double clipStartTime = 0.0;  // slot+328: frame start time in clip
            std::string motionDtgt;    // target node name for angleMode=4
            // Particle data from FrameContentState (mask 0x100000)
            int prtTrigger = 0;
            double prtFmin = 10.0;
            double prtF = 10.0;
            double prtVmin = 0.0;
            double prtV = 0.0;
            double prtAmin = 0.0;
            double prtA = 0.0;
            double prtZmin = 1.0;
            double prtZ = 1.0;
            double prtRange = 0.0;
            // Position easing curve (slot+168=ccc in sub_69A4D4 context)
            // and rotation control points (slot+268="cp").
            // Used by sub_6C1540 / sub_6BE0C0 case 3 for position derivative.
            std::vector<double> ccc_x, ccc_y;  // ccc curve for t easing in sub_69A4D4
            std::vector<double> cp_x, cp_y;    // cp main bezier points
            std::vector<double> cp_t;           // cp time knots
            bool hasCpRotation = false;         // slot+284 type != 0
        } interpolatedCache;

        // === TJS↔Native bridge helpers ===
        // These are implemented in MotionNodeBridge.cpp to avoid circular
        // dependency (MotionNode.h cannot include Player.h or ncbind.hpp).

        // nodeType=3: Get native Player* from childPlayerVar (node+1912).
        // Aligned to sub_6BE0C0 NativeInstanceSupport pattern.
        // Returns nullptr if childPlayerVar is void/invalid.
        Player* getChildPlayer() const;

        // nodeType=4: Get particle count from TJS Array (node+2296).
        // Aligned to sub_56C694: Array.count.
        int getParticleCount() const;

        // nodeType=4: Get native Player* for particle child at index.
        // Aligned to sub_6C1678: Array[i] + NativeInstanceSupport.
        Player* getParticleChild(int index) const;

        // nodeType=4: Get iTJSDispatch2* for particle child at index.
        // Returns the TJS dispatch object (for passing to sub_6B29C0 etc).
        iTJSDispatch2* getParticleChildDispatch(int index) const;

        // nodeType=4: Add a TJS-wrapped Player to the particle Array.
        // Aligned to TJS Array.add.
        void addParticleChild(const tTJSVariant &playerVar);

        // nodeType=4: Erase particle child at index from TJS Array.
        // Aligned to TJS Array.erase(index).
        void eraseParticleChild(int index);
    };

} // namespace motion::detail
