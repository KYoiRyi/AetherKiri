//
// Created by LiDon on 2025/9/15.
// Reverse-engineered from libkrkr2.so MMotionPlayer API surface
//
#pragma once

#include <array>
#include <deque>
#include <list>
#include <memory>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <spdlog/spdlog.h>
#include "tjs.h"
#include "ResourceManager.h"

namespace PSB {
    class PSBDictionary;
}

namespace motion {
    class D3DAdaptor;
    class SeparateLayerAdaptor;
}

namespace motion {
    namespace detail {
        struct MotionClip;
        struct MotionSnapshot;
        struct PlayerRuntime;
        struct TimelineControlBinding;
        struct TimelineState;
    }

    // Motion class enums
    enum LayerType {
        LayerTypeObj = 0,
        LayerTypeShape = 1,
        LayerTypeLayout = 2,
        LayerTypeMotion = 3,
        LayerTypeParticle = 4,
        LayerTypeCamera = 5
    };

    enum ShapeType {
        ShapeTypePoint = 0,
        ShapeTypeCircle = 1,
        ShapeTypeRect = 2,
        ShapeTypeQuad = 3
    };

    enum PlayFlag {
        PlayFlagForce = 1,
        PlayFlagChain = 2,
        PlayFlagAsCan = 4,
        PlayFlagJoin = 8,
        PlayFlagStealth = 16
    };

    // Aligned to libkrkr2.so Motion_namespace_ncb_register (0x6D9B08)
    enum TransformOrder {
        TransformOrderFlip = 0,
        TransformOrderAngle = 1,
        TransformOrderZoom = 2,
        TransformOrderSlant = 3
    };

    enum CoordinateType {
        CoordinateRecutangularXY = 0,
        CoordinateRecutangularXZ = 1
    };

    class Player {
    public:
        explicit Player(ResourceManager rm = ResourceManager{});
        ~Player();

        // --- Properties (getter/setter) ---
        void setCompletionType(int v) { _completionType = v; }
        int getCompletionType() const { return _completionType; }

        void setMetadata(tTJSVariant v) { _metadata = v; }
        tTJSVariant getMetadata() const { return _metadata; }

        void setChara(ttstr v) { _chara = v; }
        ttstr getChara() const { return _chara; }

        void setMotion(ttstr v);
        ttstr getMotion() const { return _motionKey; }

        void setMotionKey(ttstr v) { setMotion(v); }
        ttstr getMotionKey() const { return _motionKey; }

        // Aligned to libkrkr2.so +1032: ttstr, not bool
        void setOutline(ttstr v) { _outline = v; }
        ttstr getOutline() const { return _outline; }

        // Aligned to libkrkr2.so +1160: double, not int
        void setPriorDraw(double v) { _priorDraw = v; }
        double getPriorDraw() const { return _priorDraw; }

        void setFrameLastTime(double v) { _frameLastTime = v; }
        double getFrameLastTime() const { return _frameLastTime; }

        void setProgressCompat(double v);
        double getProgressCompat() const;

        void setFrameLoopTime(double v) { _frameLoopTime = v; }
        double getFrameLoopTime() const { return _frameLoopTime; }

        void setLoopTime(double v) { _loopTime = v; }
        double getLoopTime() const { return _loopTime; }

        void setProcessedMeshVerticesNum(int v) { _processedMeshVerticesNum = v; }
        int getProcessedMeshVerticesNum() const { return _processedMeshVerticesNum; }

        void setQueuing(bool v) { _queuing = v; }
        bool getQueuing() const { return _queuing; }

        void setDirectEdit(bool v) { _directEdit = v; }
        bool getDirectEdit() const { return _directEdit; }

        void setSelectorEnabled(bool v);
        bool getSelectorEnabled() const { return _selectorEnabled; }

        void setVariableKeys(tTJSVariant v) { _variableKeys = v; }
        tTJSVariant getVariableKeys();

        void setAllplaying(bool v) { _allplaying = v; }
        bool getAllplaying() const { return _allplaying; }

        void setSyncWaiting(bool v) { _syncWaiting = v; }
        bool getSyncWaiting() const { return _syncWaiting; }

        void setSyncActive(bool v) { _syncActive = v; }
        bool getSyncActive() const { return _syncActive; }

        void setHasCamera(bool v) { _hasCamera = v; }
        bool getHasCamera() const { return _hasCamera; }

        void setCameraActive(bool v) { _cameraActive = v; }
        bool getCameraActive() const { return _cameraActive; }

        void setStereovisionActive(bool v) { _stereovisionActive = v; }
        bool getStereovisionActive() const { return _stereovisionActive; }

        // Aligned to libkrkr2.so: tickCount uses ms↔frame conversion (60fps internal)
        // Getter: frameTickCount * 1000/60; Setter: value * 60/1000
        void setTickCount(double v) { _frameTickCount = v * 60.0 / 1000.0; }
        double getTickCount() const {
            return _frameTickCount > 0 ? _frameTickCount * 1000.0 / 60.0 : 0.0;
        }

        // Aligned to libkrkr2.so +1093: bool flag (defaultSyncActive), not double
        void setSpeed(bool v) { _speed = v; }
        bool getSpeed() const { return _speed; }

        void setFrameTickCount(double v) { _frameTickCount = v; }
        double getFrameTickCount() const { return _frameTickCount; }

        // Aligned to libkrkr2.so 0x6CD724 / 0x6CD710: packed color int at +1156
        void setColorWeight(tjs_int v);
        tjs_int getColorWeight() const;

        // Aligned to libkrkr2.so 0x6D9760 / 0x6D9758: raw int at +1148
        void setMaskMode(tjs_int v);
        tjs_int getMaskMode() const;

        // Aligned to libkrkr2.so 0x6CC9D4 / 0x6D9768: bool flag at +1097
        void setIndependentLayerInherit(bool v);
        bool getIndependentLayerInherit() const { return _independentLayerInherit; }

        void setZFactor(double v) { _zFactor = v; }
        double getZFactor() const { return _zFactor; }

        void setCameraTarget(tTJSVariant v) { _cameraTarget = v; }
        tTJSVariant getCameraTarget() const { return _cameraTarget; }

        void setCameraPosition(tTJSVariant v) { _cameraPosition = v; }
        tTJSVariant getCameraPosition() const { return _cameraPosition; }

        void setCameraFOV(double v) { _cameraFOV = v; }
        double getCameraFOV() const { return _cameraFOV; }

        void setCameraAlive(bool v) { _cameraAlive = v; }
        bool getCameraAlive() const { return _cameraAlive; }

        void setCanvasCaptureEnabled(bool v) { _canvasCaptureEnabled = v; }
        bool getCanvasCaptureEnabled() const { return _canvasCaptureEnabled; }

        void setClearEnabled(bool v) { _clearEnabled = v; }
        bool getClearEnabled() const { return _clearEnabled; }

        void setHitThreshold(double v) { _hitThreshold = v; }
        double getHitThreshold() const { return _hitThreshold; }

        void setPreview(bool v) { _preview = v; }
        bool getPreview() const { return _preview; }

        void setOutsideFactor(double v) { _outsideFactor = v; }
        double getOutsideFactor() const { return _outsideFactor; }

        void setResourceManager(tTJSVariant v) { _resourceManager = v; }
        tTJSVariant getResourceManager() const { return _resourceManager; }

        void setStealthChara(ttstr v) { _stealthChara = v; }
        ttstr getStealthChara() const { return _stealthChara; }

        void setStealthMotion(ttstr v) { _stealthMotion = v; }
        ttstr getStealthMotion() const { return _stealthMotion; }

        void setTags(tTJSVariant v) { _tags = v; }
        tTJSVariant getTags() const { return _tags; }

        void setProject(tTJSVariant v) { _project = v; }
        tTJSVariant getProject() const { return _project; }

        void setUseD3D(bool v) { _useD3D = v; }
        bool getUseD3D() const { return _useD3D; }

        // Static accessors for class-level property access
        // (patch.tjs uses: with(Motion.Player) { .useD3D = 0; })
        static tjs_error setUseD3DStatic(tTJSVariant *, tjs_int count,
                                         tTJSVariant **p, iTJSDispatch2 *) {
            if (count == 1 && (*p)->Type() == tvtInteger) {
                bool old = _useD3D;
                _useD3D = static_cast<bool>(**p);
                auto logger = spdlog::get("plugin");
                if(logger) {
                    logger->warn("Motion.Player.useD3D: {} -> {}",
                                 old, _useD3D);
                }
                return TJS_S_OK;
            }
            return TJS_E_INVALIDPARAM;
        }

        static tjs_error getUseD3DStatic(tTJSVariant *r, tjs_int,
                                         tTJSVariant **, iTJSDispatch2 *) {
            *r = tTJSVariant{_useD3D};
            return TJS_S_OK;
        }

        // Aligned to libkrkr2.so +1052: ttstr, not bool
        void setMeshline(ttstr v) { _meshline = v; }
        ttstr getMeshline() const { return _meshline; }

        bool getBusy() const { return _busy; }

        // --- Methods ---
        void initPhysics();
        tTJSVariant serialize();
        void unserialize(tTJSVariant data);
        void setEmoteCoord(double x, double y, double transition = 0.0,
                           double ease = 0.0);
        void setEmoteScale(double scale, double transition = 0.0,
                           double ease = 0.0);
        void setRotate(double rot, double transition = 0.0,
                       double ease = 0.0);
        void setEmoteColor(tjs_uint32 color, double transition = 0.0,
                           double ease = 0.0);
        void setMirror(bool mirror);
        void setEmoteMeshDivisionRatio(double v);
        void setHairScale(double s);
        void setPartsScale(double s);
        void setBustScale(double s);
        void startWind(double minAngle, double maxAngle, double amplitude,
                       double freqX, double freqY);
        void stopWind();
        void setOuterForce(ttstr label, double x, double y,
                           double transition = 0.0, double ease = 0.0);
        void setDrawAffineTranslateMatrix(tTJSVariant m);
        tTJSVariant getCameraOffset();
        void setCameraOffset(tTJSVariant offset);
        void modifyRoot(tTJSVariant data);
        void debugPrint();

        double random();

        // Load from a pre-loaded snapshot (used by EmotePlayer.setModule)
        // Aligned to libkrkr2.so: EmoteObject_init (sub_67DBAC) sets Player's
        // activeMotion directly from loaded PSB data without file I/O.
        void loadFromSnapshot(std::shared_ptr<detail::MotionSnapshot> snapshot);

        // Resource management
        void unload(ttstr name);
        void unloadAll();
        bool isExistMotion(ttstr name);
        tTJSVariant findMotion(ttstr name);
        tjs_int requireLayerId(ttstr name);
        void releaseLayerId(tjs_int id);

        // Drawing/rendering
        void setClearColor(tjs_int color);
        void setResizable(bool v);
        void removeAllTextures();
        void removeAllBg();
        void removeAllCaption();
        void registerBg(tTJSVariant bg);
        void registerCaption(tTJSVariant caption);
        void unloadUnusedTextures();
        tjs_int alphaOpAdd();
        tTJSVariant captureCanvas();
        tTJSVariant findSource(ttstr name);
        void loadSource(ttstr name);
        void clearCache();
        void setSize(tjs_int w, tjs_int h);
        void copyRect(tTJSVariant args);
        void adjustGamma(tTJSVariant args);
        void draw();
        void frameProgress(double dt);

        // Viewport/display
        void setFlip(bool v);
        void setOpacity(double v);
        void setVisible(bool v);
        void setSlant(double v);
        void setZoom(double v);
        tTJSVariant getLayerNames();
        void releaseSyncWait();
        void calcViewParam();
        tTJSVariant getLayerMotion(ttstr name);
        tTJSVariant getLayerGetter(ttstr name);
        tTJSVariant getLayerGetterList();
        void skipToSync();
        void setStereovisionCameraPosition(double x, double y, double z);

        // Timeline/variable queries
        void setVariable(ttstr label, double value, double transition = 0.0,
                         double ease = 0.0);
        double getVariable(ttstr label);
        tjs_int countVariables();
        ttstr getVariableLabelAt(tjs_int idx);
        tjs_int countVariableFrameAt(tjs_int idx);
        ttstr getVariableFrameLabelAt(tjs_int idx, tjs_int frameIdx);
        double getVariableFrameValueAt(tjs_int idx, tjs_int frameIdx);
        bool getTimelinePlaying(ttstr label);
        tTJSVariant getVariableRange(ttstr label);
        tTJSVariant getVariableFrameList(ttstr label);
        tjs_int countMainTimelines();
        ttstr getMainTimelineLabelAt(tjs_int idx);
        tTJSVariant getMainTimelineLabelList();
        tjs_int countDiffTimelines();
        ttstr getDiffTimelineLabelAt(tjs_int idx);
        tTJSVariant getDiffTimelineLabelList();
        bool getLoopTimeline(ttstr label);
        tjs_int countPlayingTimelines();
        ttstr getPlayingTimelineLabelAt(tjs_int idx);
        tjs_int getPlayingTimelineFlagsAt(tjs_int idx);
        tjs_int getTimelineTotalFrameCount(ttstr label);
        void playTimeline(ttstr label, tjs_int flags);
        void stopTimeline(ttstr label);
        void setTimelineBlendRatio(ttstr label, double ratio);
        double getTimelineBlendRatio(ttstr label);
        void fadeInTimeline(ttstr label, double duration, tjs_int flags);
        void fadeOutTimeline(ttstr label, double duration, tjs_int flags);
        tTJSVariant getPlayingTimelineInfoList();

        // Selector
        bool isSelectorTarget(ttstr name);
        void deactivateSelectorTarget(ttstr name);

        // Misc
        tTJSVariant getCommandList();
        bool getD3DAvailable();
        void doAlphaMaskOperation();
        void onFindMotion(ttstr name, int flags = 0);
        // Aligned to libkrkr2.so 0x681CAC: motion property as raw callback
        // so we have objthis to call onFindMotion TJS callback.
        static tjs_error setMotionCompat(tTJSVariant *result, tjs_int numparams,
                                         tTJSVariant **param, iTJSDispatch2 *objthis);
        static tjs_error getMotionCompat(tTJSVariant *result, tjs_int numparams,
                                         tTJSVariant **param, iTJSDispatch2 *objthis);
        static tjs_error setDrawAffineTranslateMatrixCompat(
            tTJSVariant *result, tjs_int numparams, tTJSVariant **param,
            Player *nativeInstance);
        static tjs_error captureCanvasCompat(tTJSVariant *result,
                                             tjs_int numparams,
                                             tTJSVariant **param,
                                             Player *nativeInstance);
        static tjs_error drawCompat(tTJSVariant *result, tjs_int numparams,
                                    tTJSVariant **param,
                                    iTJSDispatch2 *objthis);
        static tjs_error playCompat(tTJSVariant *result, tjs_int numparams,
                                    tTJSVariant **param, iTJSDispatch2 *objthis);
        static tjs_error progressCompatMethod(tTJSVariant *result,
                                              tjs_int numparams,
                                              tTJSVariant **param,
                                              iTJSDispatch2 *objthis);
        static tjs_error setVariableCompatMethod(tTJSVariant *result,
                                                 tjs_int numparams,
                                                 tTJSVariant **param,
                                                 iTJSDispatch2 *objthis);
        static tjs_error isPlayingCompat(tTJSVariant *result, tjs_int numparams,
                                         tTJSVariant **param,
                                         iTJSDispatch2 *objthis);
        static tjs_error stopCompat(tTJSVariant *result, tjs_int numparams,
                                    tTJSVariant **param, iTJSDispatch2 *objthis);
        tTJSVariant motionList();
        void emoteEdit(tTJSVariant args);

        // Public accessor for EmotePlayer delegation
        double getActiveMotionWidth() const;
        double getActiveMotionHeight() const;
        bool hitTestLayer(ttstr name, double x, double y);

        // Root node position (x/y/left/top)
        // Aligned to libkrkr2.so:
        //   getter: Player_getRootX (0x6D98A8) reads root_node+1592
        //   setter: Player_setRootX (0x6CD028) writes root_node+1592, sets dirty
        double getX() const;
        double getY() const;
        void setX(double v);
        void setY(double v);
        double getLeft() const { return getX(); }
        double getTop() const { return getY(); }
        void setLeft(double v) { setX(v); }
        void setTop(double v) { setY(v); }

    private:
        bool ensureMotionLoaded();
        void ensureNodeTreeBuilt();
        void syncVariableKeysFromActiveMotion();
        void syncSelectorControlsLike_0x670D1C();
        const detail::TimelineState *primaryTimelineStateLike_0x66F80C() const;
        void preProgressPlayingTimelinesLike_0x671764(
            double dt, std::unordered_map<std::string, double> *prevTimes);
        void resetTimelineControlStateLike_0x671A50(
            detail::TimelineState &state,
            const detail::TimelineControlBinding &binding,
            double time);
        void scheduleTimelineControlAnimatorLike_0x671A50(
            detail::TimelineState &state,
            size_t trackIndex,
            float value,
            double transition,
            double easeWeight);
        void applyTimelineControlWindowLike_0x669E1C(
            detail::TimelineState &state,
            const detail::TimelineControlBinding &binding,
            double targetTime,
            bool inclusiveEnd);
        void applyTimelineControlFrameCrossingLike_0x67CD20(
            const std::unordered_map<std::string, double> &prevTimes);
        void stepTimelineControlAnimatorsLike_0x67D01C(double dt);
        void stepTimelineBlendAnimatorsLike_0x67D01C(double dt);
        void setTimelineBlendLike_0x6735AC(
            const std::string &label,
            bool autoStop,
            double value,
            double transition,
            double ease);
        void refreshFixedControllerEvalOutputsLike_0x67D01C();
        void accumulateTimelineContributionLike_0x67C560(
            const std::string &label,
            double &value);
        void setVariableResolvedWeightLike_0x671228(
            const std::string &key,
            double value,
            double transition,
            double easeWeight);
        void resetControllerStateLike_0x66EB8C();
        void applyEvalResultPostProcessLike_0x67CC9C();
        void applyClampControlsLike_0x67C8A8();
        bool shouldMirrorEvalLabelLike_0x67C6B0(const std::string &label);
        double &ensureEvalResultSlotLike_0x686944(const std::string &label);
        void removeEvalResultSlotLike_Reset(const std::string &label);
        void writeEvalResultValueLike_0x6C4668(const std::string &label,
                                              double value);
        bool renderToLayer(iTJSDispatch2 *layerObject,
                           bool skipUpdate = false);
        bool renderToSeparateLayerAdaptor(iTJSDispatch2 *slaObject);
        bool renderToD3DAdaptor(D3DAdaptor *adaptor);
        bool renderViaSharedD3DAdaptor(iTJSDispatch2 *targetLayerObject);
        iTJSDispatch2 *resolveSeparateLayerRenderTarget(SeparateLayerAdaptor *sla,
                                                        iTJSDispatch2 *fallbackOwner,
                                                        int &canvasWidth,
                                                        int &canvasHeight);
        bool renderMotionFrameToTarget(iTJSDispatch2 *renderTargetObject,
                                       tjs_int canvasWidth,
                                       tjs_int canvasHeight,
                                       const char *traceFunc);
        const detail::MotionClip *selectActiveClip() const;
        const std::vector<std::string> &activeLayerNames() const;
        const std::unordered_map<
            std::string, std::shared_ptr<const PSB::PSBDictionary>> *
        activeLayersByName() const;
        const std::vector<std::string> &activeSourceCandidates() const;
        void calcBounds();
        void updateLayers();
        bool prepareRenderItems();
        void appendPreparedRenderItems();
        void applyPreparedRenderItemTranslateOffsets();
        bool buildRenderCommands(tjs_int canvasWidth, tjs_int canvasHeight);
        bool executeLayerRenderCommands(iTJSDispatch2 *renderLayerObject,
                                        bool skipUpdate);
        bool updateLayerAfterDraw(iTJSDispatch2 *targetLayerObject);
        bool updateAccurateSLAAfterDraw(iTJSDispatch2 *targetLayerObject);
        // updateLayers sub-phases (aligned to libkrkr2.so sub-functions)
        void updateLayersPhase1_PreLoop(double currentTime);
        void updateLayersPhase2_MainLoop(double currentTime);
        void updateLayersPhase3_CameraConstraint();          // sub_6BC000
        void updateLayersPhase3_VertexComputation();          // sub_6BC4F0
        void updateLayersPhase3_Visibility();                 // sub_6BD8DC
        void updateLayersPhase3_CameraNode();                 // sub_6BDA28
        void updateLayersPhase3_ShapeAABB();                  // sub_6BDCC0
        void updateLayersPhase3_ShapeGeometry();              // sub_6BDE94
        void updateLayersPhase3_MotionSubNode(double currentTime);  // sub_6BE0C0
        void updateLayersPhase3_ParticleEmitter();            // sub_6BEDD0
        void updateLayersPhase3_ParticleSystem(double currentTime); // sub_6BF0DC
        void updateLayersPhase3_AnchorNode();                 // sub_6C0528

        std::shared_ptr<detail::PlayerRuntime> _runtime;
        ResourceManager _resourceManagerNative;
        int _completionType = 0;
        tTJSVariant _metadata;
        ttstr _chara;
        ttstr _motionKey;
        ttstr _outline;  // Aligned to libkrkr2.so +1032: ttstr
        double _priorDraw = 0.0;  // Aligned to libkrkr2.so +1160: double
        double _frameLastTime = 0.0;
        double _frameLoopTime = 0.0;
        double _clampedEvalTime = 0.0; // player+456: min(_frameLoopTime, totalFrames)
        double _loopTime = 0.0;    // player+1136
        double _cachedTotalFrames = 0.0; // player+1128: cached max totalFrames across timelines
        int _processedMeshVerticesNum = 0;
        bool _queuing = false;
        bool _directEdit = false;
        bool _selectorEnabled = false;
        tTJSVariant _variableKeys;
        bool _allplaying = false;
        bool _syncWaiting = false;
        bool _syncActive = false;
        bool _hasCamera = false;
        bool _cameraActive = false;
        bool _stereovisionActive = false;
        // Camera angle for stereovision (a1+472, sub_6BDA28 at 0x6BDC50)
        double _cameraAngle = 0.0;
        double _cameraPosX = 0, _cameraPosY = 0, _cameraPosZ = 0;
        double _cameraTargetX = 0, _cameraTargetY = 0, _cameraTargetZ = 0;
        bool _speed = true;           // Aligned to libkrkr2.so +1093: bool flag
        double _frameTickCount = 0.0;
        tjs_int _maskMode = 0;                         // libkrkr2.so +1148
        std::uint32_t _colorWeightPacked = 0xFF808080u; // libkrkr2.so +1156
        bool _independentLayerInherit = false;          // libkrkr2.so +1097
        double _zFactor = 1.0;
        tTJSVariant _cameraTarget;
        tTJSVariant _cameraPosition;
        double _cameraFOV = 60.0;
        bool _cameraAlive = false;
        bool _canvasCaptureEnabled = false;
        bool _clearEnabled = false;
        bool _d3dDrawMode = false; // libkrkr2.so byte_909: set when draw(D3DAdaptor) called
        double _hitThreshold = 0.0;
        bool _preview = false;
        double _outsideFactor = 0.0;
        tTJSVariant _resourceManager;
        ttstr _stealthChara;
        ttstr _stealthMotion;
        tTJSVariant _tags;
        tTJSVariant _project;
        inline static bool _useD3D;
        ttstr _meshline;  // Aligned to libkrkr2.so +1052: ttstr
        bool _busy = false;

        // Aligned to libkrkr2.so Player_updateLayers (0x6BB33C):
        // Camera velocity at player+784/792/800, damping at player+600
        double _cameraVelocityX = 0.0;   // player+784
        double _cameraVelocityY = 0.0;   // player+792
        double _cameraVelocityZ = 0.0;   // player+800
        double _cameraDamping = 1.0;     // player+600 (1.0 = no damping)
        double _rootOffsetX = 0.0;       // player+120, root layer position offset
        double _rootOffsetY = 0.0;       // player+128
        // Pending root position from TJS setter (player.x/y).
        // Applied to root node when nodes are built (deferred because
        // setter may be called before node tree exists).
        double _pendingRootX = 0.0;
        double _pendingRootY = 0.0;
        bool _hasPendingRootPos = false;
        double _rootOffsetZ = 0.0;
        float _cameraOffsetX = 0.0f;    // player+144, set by setCameraOffset (0x6D9A38)
        float _cameraOffsetY = 0.0f;    // player+148

        // Aligned to libkrkr2.so Player_calcBounds (0x6C3D04):
        // AABB stored at player+152~176
        double _boundsMinX = 1e308;
        double _boundsMinY = 1e308;
        double _boundsMaxX = -1e308;
        double _boundsMaxY = -1e308;
        bool _needsInternalAssignImages = false; // flag +613 for updateLayerAfterDraw
        std::unordered_map<std::string, double> _variableValues;
        struct VariableKeyframe {
            float value = 0.0f;
            float duration = 0.0f;
            float weight = 1.0f;
        };
        struct VariableAnimatorState {
            std::deque<VariableKeyframe> queue;
            bool active = false;
            float currentValue = 0.0f;
            float startValue = 0.0f;
            float targetValue = 0.0f;
            float progress = 1.0f;
            float duration = 0.0f;
            float weight = 1.0f;
        };
        std::unordered_map<std::string, VariableAnimatorState> _variableAnimators;
        std::unordered_map<std::string, VariableAnimatorState>
            _type4ControllerAnimators;
        std::unordered_map<std::string, VariableAnimatorState>
            _type5ControllerAnimators;
        std::unordered_map<std::string, VariableAnimatorState>
            _type6ControllerAnimators;
        std::unordered_map<std::string, VariableAnimatorState>
            _type7ControllerAnimators;
        std::unordered_map<std::string, VariableAnimatorState>
            _type8ControllerAnimators;
        std::unordered_map<std::string, VariableAnimatorState> *
        controllerAnimatorBucketLike_0x671228(int type);
        const std::unordered_map<std::string, VariableAnimatorState> *
        controllerAnimatorBucketLike_0x671228(int type) const;
        VariableAnimatorState *
        findControllerAnimatorStateLike_0x671228(const std::string &label);
        const VariableAnimatorState *
        findControllerAnimatorStateLike_0x671228(const std::string &label) const;
        void eraseControllerAnimatorStateLike_0x671228(const std::string &label);
        void clearControllerAnimatorStateLike_0x671228();
        std::unordered_map<std::string, double> _evalResultValues;
        struct EvalResultEntry {
            std::string label;
            double value = 0.0;
        };
        std::list<EvalResultEntry> _evalResultList;
        std::unordered_map<std::string, std::list<EvalResultEntry>::iterator>
            _evalResultListIndex;
        bool _rootFlipX = false;
        bool _mirrorEvalEnabled = false;
        std::unordered_set<std::string> _mirrorPositiveCache;
        std::unordered_set<std::string> _mirrorNegativeCache;

        // Parent color propagated from parent motion node (sub_6BE0C0 at 0x6BEB7C).
        // Binary: *(_DWORD *)(childPlayer + 1156) = *(_DWORD *)(node + 100)
        // Stores colorBytes[0..3] packed as RGBA uint32 (default 0xFF808080).
        uint32_t _parentColorPacked = 0xFF808080u;  // player+1156

        // Per-frame flag cleared at end of updateLayers (player+608, 0x6BBDF8).
        // Set to true in constructor; checked by sub_6BE0C0 case 2 (0x6BE664)
        // and sub_6BEDD0 case 2 (0x6BEFF4). When true, case 2 falls through
        // to interpolated derivative path instead of using deltaPos.
        bool _noUpdateYet = true;  // player+608

        // Aligned to libkrkr2.so emote scale/rotate fields:
        // sub_681F20: player+1184, sub_681F28: player+1192, sub_681F30: player+1200
        // player+1168/+1176 are the duplicated meshDivisionRatio doubles read by
        // Player_startWind (0x6709AC).
        double _emoteMeshDivisionRatio = 1.0;
        double _emoteMeshDivisionRatioDup = 1.0;
        double _hairScale = 1.0;    // player+1184
        double _partsScale = 1.0;   // player+1192
        double _bustScale = 1.0;    // player+1200
        double _rotateAngle = 0.0;  // sub_672568 rotation parameter
        bool _physicsDisabled = false;   // player+1159
        bool _emoteAnimatorFlag = false; // player+1161
        bool _emoteDirty = false;        // player+1162
        struct EmoteCoordState {
            double x = 0.0;
            double y = 0.0;
            double transition = 0.0;
            double ease = 0.0;
        } _emoteCoordState;
        struct EmoteScalarAnimatorState {
            double value = 0.0;
            double transition = 0.0;
            double ease = 0.0;
        } _emoteScaleState, _emoteRotState;
        struct EmoteColorState {
            tjs_uint32 packed = 0;
            std::array<float, 4> rgbaBytes{0.f, 0.f, 0.f, 0.f};
            double transition = 0.0;
            double ease = 0.0;
        } _emoteColorState;

        struct WindState {
            bool active = false;
            double minAngle = 0.0;
            double maxAngle = 0.0;
            double amplitude = 0.0;
            double freqX = 0.0;
            double freqY = 0.0;
            double phase = 0.0;
            double prevPhase = 0.0;
            double scaledAmplitude = 0.0;
            int counter = 0;
        } _windState;

        struct OuterForceState {
            bool active = false;
            double x = 0.0;
            double y = 0.0;
            double transition = 0.0;
            double ease = 0.0;
        };

        OuterForceState _bustOuterForce;
        OuterForceState _hairOuterForce;
        OuterForceState _partsOuterForce;

        // Aligned to libkrkr2.so player+992: TJS Math.RandomGenerator object.
        // sub_6BA7B8 calls its "random" method to get [0.0, 1.0) doubles.
        // Created via TJS eval "new Math.RandomGenerator()" during init.
        tTJSVariant _tjsRandomGenerator;  // player+992

        // Aligned to libkrkr2.so player+1012: emoteEdit TJS variant.
        // Propagated to child particle players (sub_6BF0DC at 0x6BF9C0).
        tTJSVariant _emoteEditVariant;    // player+1012
    };

} // namespace motion
