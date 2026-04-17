//
// Internal helpers for motionplayer/emoteplayer runtime state.
//
#pragma once

#include <array>
#include <cstdint>
#include <deque>
#include <map>
#include <memory>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include <spdlog/fmt/fmt.h>

#include "tjs.h"
#include "psbfile/PSBFile.h"
#include "MotionNode.h"

namespace motion::detail {

    struct VariableFrameInfo {
        std::string label;
        double value = 0.0;
    };

    struct VariableControllerBinding {
        int type = -1;
        int index = -1;
        std::string source;
        std::string role;
    };

    struct SelectorControlOption {
        std::string label;
        double offValue = 0.0;
        double onValue = 0.0;
    };

    struct SelectorControlBinding {
        std::string label;
        std::vector<SelectorControlOption> options;
    };

    struct FixedControllerOutputBinding {
        std::string label;
        int type = -1;
        int index = -1;
        std::string role;
    };

    struct ClampControlBinding {
        int type = 0;
        std::string varLr;
        std::string varUd;
        double minValue = 0.0;
        double maxValue = 0.0;
    };

    struct TimelineControlFrame {
        double time = 0.0;
        bool isTypeZero = true;
        float value = 0.0f;
        double easingWeight = 1.0;
    };

    struct TimelineControlTrack {
        std::string label;
        // Aligned to libkrkr2.so sub_66FC5C byte at track+8:
        // set when label is present in instantVariableList (player+0x4F8).
        bool instantVariable = false;
        std::vector<TimelineControlFrame> frames;
    };

    struct TimelineControlBinding {
        std::string label;
        double loopBegin = -1.0;
        double loopEnd = -1.0;
        double lastTime = -1.0;
        std::vector<TimelineControlTrack> tracks;
    };

    struct TimelineControlKeyframe {
        float value = 0.0f;
        float duration = 0.0f;
        float weight = 1.0f;
    };

    struct TimelineControlAnimatorState {
        std::deque<TimelineControlKeyframe> queue;
        bool active = false;
        float currentValue = 0.0f;
        float startValue = 0.0f;
        float targetValue = 0.0f;
        float progress = 1.0f;
        float duration = 0.0f;
        float weight = 1.0f;
    };

    struct MotionClip {
        std::string label;
        std::string owner;
        bool loop = false;
        double loopTime = -1.0;   // from PSB; >=0 means loop restart point
        double totalFrames = 0.0;
        std::vector<std::string> layerNames;
        std::unordered_map<std::string, std::shared_ptr<const PSB::PSBDictionary>>
            layersByName;
        std::vector<std::string> sourceCandidates;
    };

    struct TimelineState {
        std::string label;
        int flags = 0;
        bool playing = false;
        bool loop = false;
        double loopTime = -1.0;   // from PSB; >=0 means loop, <0 means stop at end
        double totalFrames = 0.0;
        double currentTime = 0.0;
        double blendRatio = 1.0;
        bool wasPlaying = false;  // for edge detection in dispatchEvents
        bool controlInitialized = false;
        double controlLastAppliedTime = 0.0;
        std::vector<int> controlFrameCursor;
        std::vector<float> controlTrackValues;
        std::vector<TimelineControlAnimatorState> controlTrackAnimators;
        TimelineControlAnimatorState blendAnimator;
        bool blendAutoStop = false;
    };

    // Aligned to libkrkr2.so Player_dispatchEvents (0x6C4490):
    // type=0: onAction(param1, param2), type=1: onSync()
    struct MotionEvent {
        int type = 0;
        std::string param1;
        std::string param2;
    };

    struct MotionSnapshot {
        std::string path;
        std::shared_ptr<PSB::PSBFile> file;
        std::shared_ptr<const PSB::PSBDictionary> root;
        std::unordered_map<std::string, std::shared_ptr<const PSB::PSBResource>>
            resourcesByPath;
        tTJSVariant moduleValue;
        std::vector<std::string> mainTimelineLabels;
        std::vector<std::string> diffTimelineLabels;
        std::vector<std::string> variableLabels;
        std::unordered_map<std::string, bool> loopTimelines;
        std::unordered_map<std::string, double> timelineLoopTimes;
        std::unordered_map<std::string, double> timelineTotalFrames;
        std::unordered_map<std::string, std::pair<double, double>> variableRanges;
        std::unordered_map<std::string, std::vector<VariableFrameInfo>> variableFrames;
        std::unordered_map<std::string, VariableControllerBinding> controllerBindings;
        std::unordered_set<std::string> instantVariableLabels;
        std::unordered_map<std::string, SelectorControlBinding> selectorControls;
        std::vector<FixedControllerOutputBinding> fixedControllerOutputs;
        std::vector<ClampControlBinding> clampControls;
        std::vector<std::string> mirrorVariableMatchList;
        std::vector<std::string> layerNames;
        std::unordered_map<std::string, std::shared_ptr<const PSB::PSBDictionary>> layersByName;
        std::vector<std::string> sourceCandidates;
        std::unordered_map<std::string, MotionClip> clipsByLabel;
        std::unordered_map<std::string, TimelineControlBinding>
            timelineControlByLabel;
        std::vector<std::string> resourceAliases;
        double width = 0.0;
        double height = 0.0;
    };

    struct PlayerRuntime {
        std::unordered_map<std::string, std::shared_ptr<MotionSnapshot>> motionsByKey;
        std::unordered_map<std::string, tTJSVariant> sourcesByKey;
        std::shared_ptr<MotionSnapshot> activeMotion;
        std::unordered_map<std::string, TimelineState> timelines;
        std::vector<std::string> playingTimelineLabels;
        std::unordered_map<std::string, tjs_int> layerIdsByName;
        std::unordered_map<tjs_int, std::string> layerNamesById;
        std::vector<tTJSVariant> backgrounds;
        std::vector<tTJSVariant> captions;
        std::unordered_map<std::string, bool> disabledSelectorTargets;
        tTJSVariant lastCanvas;
        tTJSVariant lastViewParam;
        // Aligned to libkrkr2.so player+696: internal render layer consumed by
        // sub_6CE7D8 / sub_6CE938 style post-draw update.
        tTJSVariant internalRenderLayer;
        // Reusable work layer for sub_6C4E28-style per-item local clipping.
        tTJSVariant scratchWorkLayer;
        std::array<double, 6> drawAffineMatrix{ 1.0, 0.0, 0.0,
                                                1.0, 0.0, 0.0 };
        tjs_int nextLayerId = 1;
        tjs_int clearColor = 0;
        tjs_int width = 0;
        tjs_int height = 0;
        int alphaOpCounter = 0;
        bool resizable = false;
        bool flip = false;
        bool visible = true;
        double opacity = 1.0;
        double slant = 0.0;
        double zoom = 1.0;
        std::vector<MotionEvent> pendingEvents;
        // Persistent node tree for updateLayers pipeline
        std::vector<MotionNode> nodes;
        bool nodesBuilt = false;
        // Node label → index map. Aligned to binary's std::map<ttstr,int> at player+24.
        // Populated after buildNodeTree, queried by sub_6F2228 equivalent.
        std::map<std::string, int> nodeLabelMap;

        struct PreparedRenderItem {
            int nodeIndex = 0;
            tTJSVariant srcRef;
            std::string sourceKey;
            bool hasOwnSource = false;
            bool groupOnly = false;
            bool skipFlag0 = false;
            bool skipFlag1 = false;
            bool clipFlag = false;
            bool drawFlag = false;
            double sortKey = 0.0;
            int blendMode = 16;
            std::array<float, 8> corners{};
            std::array<std::uint32_t, 4> packedColors{
                0xFF808080u, 0xFF808080u, 0xFF808080u, 0xFF808080u
            };
            std::array<float, 4> paintBox{0.f, 0.f, 0.f, 0.f};
            std::array<float, 4> viewport{1.f, 1.f, -1.f, -1.f};
            bool hasViewport = false;
            int opacity = 255;
            int updateCount = 0;
            int visibleAncestorIndex = -1;
            int meshDivX = 0;
            int meshDivY = 0;
            int meshType = 0;
            std::vector<float> meshPoints;
            int layerId = 0;
        };
        struct RenderCommand {
            int nodeIndex = 0;
            tTJSVariant srcRef;
            std::string sourceKey;
            bool hasOwnSource = false;
            bool groupOnly = false;
            int blendMode = 16;
            int opacity = 255;
            int itemFlags = 0;
            int parentNodeIndex = -1;
            bool hasRenderParent = false;
            std::array<std::uint32_t, 4> packedColors{
                0xFF808080u, 0xFF808080u, 0xFF808080u, 0xFF808080u
            };
            int visibleAncestorIndex = -1;
            bool clearEnabled = false;
            std::array<int, 4> clipRect{0, 0, 0, 0};
            std::array<int, 4> dirtyRect{0, 0, 0, 0};
            std::array<float, 8> worldCorners{};
            std::array<float, 8> localCorners{};
            std::vector<float> worldMeshPoints;
            std::vector<float> localMeshPoints;
            int meshDivX = 0;
            int meshDivY = 0;
            int meshType = 0;
            int layerId = 0;
            std::vector<int> childCommandIndices;
            tTJSVariant leafLayer;
            tTJSVariant composedLayer;
            std::array<int, 4> builtRect{0, 0, 0, 0};
            bool leafBuilt = false;
            bool composedBuilt = false;
            bool executedDirect = false;
        };
        std::vector<PreparedRenderItem> preparedRenderItems;  // player+936/944
        std::vector<RenderCommand> renderCommands;

        // Per-node evaluation time array.
        // Aligned to libkrkr2.so player+384: 56-byte-per-node entries.
        // Each node's node+8 pointer points into this array.
        // Offset 40 within each entry stores the per-node eval time.
        // Offset 48 stores the per-node dirty flag (cleared in post-loop).
        struct PerNodeEvalData {
            double padding[5] = {};   // offsets 0-39 (unused in our current scope)
            double evalTime = 0.0;    // offset 40: per-node evaluation time
            int dirtyFlag = 0;        // offset 48: cleared in post-loop
        };
        std::vector<PerNodeEvalData> perNodeEvalData;  // player+384/392
        // Aligned to libkrkr2.so Player_playImpl (0x6B2284):
        // PSB root "type" field: 0=non-emote (motion), 1=emote
        bool isEmoteMode = false;
    };

    std::shared_ptr<PlayerRuntime> makePlayerRuntime();

    std::string narrow(const ttstr &value);
    ttstr widen(const std::string &value);

    std::vector<ttstr> buildMotionLookupCandidates(const ttstr &name);
    bool resolveExistingPath(const std::vector<ttstr> &candidates, ttstr &resolved);
    void appendEmbeddedSourceCandidates(const MotionSnapshot &snapshot,
                                        const std::string &source,
                                        std::vector<ttstr> &candidates);

    std::shared_ptr<MotionSnapshot> loadMotionSnapshot(const ttstr &path,
                                                       tjs_int decryptSeed);
    tTJSVariant loadPSBVariant(const ttstr &path, tjs_int decryptSeed);

    void registerModuleSnapshot(const tTJSVariant &module,
                                const std::shared_ptr<MotionSnapshot> &snapshot);
    std::shared_ptr<MotionSnapshot> lookupModuleSnapshot(const tTJSVariant &module);

    tTJSVariant makeArray(const std::vector<tTJSVariant> &items);
    tTJSVariant makeDictionary(
        const std::vector<std::pair<std::string, tTJSVariant>> &entries);
    std::vector<tTJSVariant> stringsToVariants(
        const std::vector<std::string> &values);

    void primeTimelineStates(std::unordered_map<std::string, TimelineState> &states,
                             const MotionSnapshot &snapshot);
    void stepTimelines(std::unordered_map<std::string, TimelineState> &states,
                       double dt,
                       std::vector<MotionEvent> *events = nullptr);

    bool logoChainTraceEnabled();
    bool logoChainTraceEnabledForPath(const std::string &motionPath);
    bool logoChainTraceEnabled(const std::shared_ptr<MotionSnapshot> &snapshot);
    bool logoSnapshotMarkEnabled();
    bool logoSnapshotMarkEnabledForPath(const std::string &motionPath);
    void resetLogoChainTraceSession(const std::string &motionPath);
    void logoChainTraceLog(const std::string &motionPath,
                           const char *stage,
                           const char *func,
                           double frameTime,
                           const std::string &message);
    void logoChainTraceCheck(const std::string &motionPath,
                             const char *stage,
                             const char *func,
                             double frameTime,
                             const std::string &expected,
                             const std::string &actual,
                             bool ok,
                             const std::string &likelyRootCause = {});
    void logoChainTraceSummary(const std::string &motionPath,
                               const char *func,
                               double frameTime,
                               const std::string &note = {});

    template <typename... Args>
    inline void logoChainTraceLogf(const std::string &motionPath,
                                   const char *stage,
                                   const char *func,
                                   double frameTime,
                                   fmt::format_string<Args...> format,
                                   Args &&...args) {
        if(!logoChainTraceEnabledForPath(motionPath)) {
            return;
        }
        logoChainTraceLog(motionPath, stage, func, frameTime,
                          fmt::format(format, std::forward<Args>(args)...));
    }

    // Scan PSB layer tree for action/sync events between prevTime and newTime.
    // Aligned to libkrkr2.so: updateLayers queues events during tree evaluation.
    void scanLayerActions(const MotionSnapshot &snapshot,
                          double prevTime, double newTime,
                          std::vector<MotionEvent> &events);

} // namespace motion::detail
