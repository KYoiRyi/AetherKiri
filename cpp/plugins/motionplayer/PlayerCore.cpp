// PlayerCore.cpp — Constructor, setMotion, serialize, core properties
// Split from Player.cpp for maintainability.
//
#include <algorithm>
#include <cctype>
#include <cmath>

#include "PlayerInternal.h"

using namespace motion::internal;

namespace {
    std::string lowerAscii(std::string value) {
        for(char &ch : value) {
            ch = static_cast<char>(
                std::tolower(static_cast<unsigned char>(ch)));
        }
        return value;
    }

    std::uint32_t swapPackedRbLike_0x6CD710(std::uint32_t packedColor) {
        return (packedColor & 0xFF00FF00u) |
            ((packedColor >> 16) & 0xFFu) |
            ((packedColor & 0xFFu) << 16);
    }
}

namespace motion {

    std::unordered_map<std::string, Player::VariableAnimatorState> *
    Player::controllerAnimatorBucketLike_0x671228(int type) {
        switch(type) {
            case 4:
                return &_type4ControllerAnimators;
            case 5:
                return &_type5ControllerAnimators;
            case 6:
                return &_type6ControllerAnimators;
            case 7:
                return &_type7ControllerAnimators;
            case 8:
                return &_type8ControllerAnimators;
            default:
                return nullptr;
        }
    }

    const std::unordered_map<std::string, Player::VariableAnimatorState> *
    Player::controllerAnimatorBucketLike_0x671228(int type) const {
        switch(type) {
            case 4:
                return &_type4ControllerAnimators;
            case 5:
                return &_type5ControllerAnimators;
            case 6:
                return &_type6ControllerAnimators;
            case 7:
                return &_type7ControllerAnimators;
            case 8:
                return &_type8ControllerAnimators;
            default:
                return nullptr;
        }
    }

    Player::VariableAnimatorState *
    Player::findControllerAnimatorStateLike_0x671228(const std::string &label) {
        const auto findInBucket =
            [&label](auto &bucket) -> VariableAnimatorState * {
            if(const auto it = bucket.find(label); it != bucket.end()) {
                return &it->second;
            }
            return nullptr;
        };

        if(auto *state = findInBucket(_type4ControllerAnimators)) {
            return state;
        }
        if(auto *state = findInBucket(_type5ControllerAnimators)) {
            return state;
        }
        if(auto *state = findInBucket(_type6ControllerAnimators)) {
            return state;
        }
        if(auto *state = findInBucket(_type8ControllerAnimators)) {
            return state;
        }
        return findInBucket(_type7ControllerAnimators);
    }

    const Player::VariableAnimatorState *
    Player::findControllerAnimatorStateLike_0x671228(
        const std::string &label) const {
        const auto findInBucket =
            [&label](const auto &bucket) -> const VariableAnimatorState * {
            if(const auto it = bucket.find(label); it != bucket.end()) {
                return &it->second;
            }
            return nullptr;
        };

        if(const auto *state = findInBucket(_type4ControllerAnimators)) {
            return state;
        }
        if(const auto *state = findInBucket(_type5ControllerAnimators)) {
            return state;
        }
        if(const auto *state = findInBucket(_type6ControllerAnimators)) {
            return state;
        }
        if(const auto *state = findInBucket(_type8ControllerAnimators)) {
            return state;
        }
        return findInBucket(_type7ControllerAnimators);
    }

    void Player::eraseControllerAnimatorStateLike_0x671228(
        const std::string &label) {
        _type4ControllerAnimators.erase(label);
        _type5ControllerAnimators.erase(label);
        _type6ControllerAnimators.erase(label);
        _type7ControllerAnimators.erase(label);
        _type8ControllerAnimators.erase(label);
    }

    void Player::clearControllerAnimatorStateLike_0x671228() {
        _type4ControllerAnimators.clear();
        _type5ControllerAnimators.clear();
        _type6ControllerAnimators.clear();
        _type7ControllerAnimators.clear();
        _type8ControllerAnimators.clear();
    }

    void Player::setSelectorEnabled(bool v) {
        if(_selectorEnabled == v) {
            return;
        }
        _selectorEnabled = v;
        syncSelectorControlsLike_0x670D1C();
    }

    tjs_int Player::getColorWeight() const {
        return static_cast<tjs_int>(
            swapPackedRbLike_0x6CD710(_colorWeightPacked));
    }

    void Player::setColorWeight(tjs_int v) {
        _colorWeightPacked = swapPackedRbLike_0x6CD710(
            static_cast<std::uint32_t>(v));
    }

    tjs_int Player::getMaskMode() const {
        return _maskMode;
    }

    void Player::setMaskMode(tjs_int v) {
        _maskMode = v;
    }

    void Player::setIndependentLayerInherit(bool v) {
        if(_independentLayerInherit == v) {
            return;
        }

        _independentLayerInherit = v;
        if(!_runtime) {
            return;
        }

        // libkrkr2.so 0x6CC9D4 compares player+1097 and marks node+1584 dirty.
        for(auto &node : _runtime->nodes) {
            node.accumulated.dirty = true;
        }
    }

    Player::Player(ResourceManager rm) :
        _runtime(detail::makePlayerRuntime()),
        _resourceManagerNative(std::move(rm)) {
        LOGGER->info("Motion.Player constructor called");
        // Aligned to sub_6A88CC (0x6A8988): create TJS Math.RandomGenerator
        // and store at player+992. Child Players inherit via sub_6CED30.
        try {
            TVPExecuteExpression(
                TJS_W("new Math.RandomGenerator()"),
                &_tjsRandomGenerator);
        } catch (...) {
            LOGGER->warn("Player: failed to create Math.RandomGenerator");
        }
    }

    Player::~Player() = default;

    // Aligned to libkrkr2.so Player_getRootX (0x6D98A8) / Player_setRootX (0x6CD028)
    double Player::getX() const {
        if (_runtime && !_runtime->nodes.empty())
            return _runtime->nodes[0].localState.posX;
        return _hasPendingRootPos ? _pendingRootX : 0.0;
    }
    void Player::setX(double v) {
        _pendingRootX = v;
        _hasPendingRootPos = true;
        if (_runtime && !_runtime->nodes.empty()) {
            auto &root = _runtime->nodes[0];
            if (root.localState.posX != v) {
                root.localState.posX = v;
                root.localState.dirty = true;
                root.accumulated.dirty = true;
            }
        }
    }
    // Aligned to libkrkr2.so Player_getRootY (0x6D98B4) / Player_setRootY (0x6CD048)
    double Player::getY() const {
        if (_runtime && !_runtime->nodes.empty())
            return _runtime->nodes[0].localState.posY;
        return _hasPendingRootPos ? _pendingRootY : 0.0;
    }
    void Player::setY(double v) {
        _pendingRootY = v;
        _hasPendingRootPos = true;
        if (_runtime && !_runtime->nodes.empty()) {
            auto &root = _runtime->nodes[0];
            if (root.localState.posY != v) {
                root.localState.posY = v;
                root.localState.dirty = true;
                root.accumulated.dirty = true;
            }
        }
    }

    // Aligned to libkrkr2.so EmoteObject_init (sub_67DBAC):
    // Sets activeMotion directly from a pre-loaded snapshot, bypassing file I/O.
    // Used by EmotePlayer.setModule() to bridge loaded PSB data into the Player pipeline.
    void Player::loadFromSnapshot(
        std::shared_ptr<detail::MotionSnapshot> snapshot) {
        _runtime->activeMotion.reset();
        _runtime->timelines.clear();
        _runtime->playingTimelineLabels.clear();
        _runtime->drawAffineMatrix = { 1.0, 0.0, 0.0, 1.0, 0.0, 0.0 };
        _variableKeys.Clear();
        _variableValues.clear();
        _variableAnimators.clear();
        clearControllerAnimatorStateLike_0x671228();
        _evalResultValues.clear();
        _evalResultList.clear();
        _evalResultListIndex.clear();
        _mirrorPositiveCache.clear();
        _mirrorNegativeCache.clear();

        if(snapshot) {
            activateMotion(*_runtime, snapshot);
            syncVariableKeysFromActiveMotion();
        }
    }

    double Player::getActiveMotionWidth() const {
        return _runtime->activeMotion ? _runtime->activeMotion->width : 0.0;
    }

    double Player::getActiveMotionHeight() const {
        return _runtime->activeMotion ? _runtime->activeMotion->height : 0.0;
    }

    void Player::setMotion(ttstr v) {
        if(_motionKey == v) {
            return;
        }
        _motionKey = v;
        _runtime->activeMotion.reset();
        _runtime->timelines.clear();
        _runtime->playingTimelineLabels.clear();
        _runtime->drawAffineMatrix = { 1.0, 0.0, 0.0, 1.0, 0.0, 0.0 };
        _variableKeys.Clear();
        _variableValues.clear();
        _variableAnimators.clear();
        clearControllerAnimatorStateLike_0x671228();
        _evalResultValues.clear();
        _evalResultList.clear();
        _evalResultListIndex.clear();
        _mirrorPositiveCache.clear();
        _mirrorNegativeCache.clear();
        ensureMotionLoaded();
    }

    // Aligned to libkrkr2.so 0x681CAC → 0x6B0F10:
    // motion setter calls objthis.onFindMotion({chara, motion}) to let
    // TJS participate in path resolution before loading the PSB.
    tjs_error Player::setMotionCompat(tTJSVariant *result, tjs_int numparams,
                                      tTJSVariant **param,
                                      iTJSDispatch2 *objthis) {
        auto *self = ncbInstanceAdaptor<Player>::GetNativeInstance(objthis, true);
        if(!self) return TJS_E_INVALIDOBJECT;

        ttstr motionValue;
        if(numparams > 0 && param[0] && param[0]->Type() != tvtVoid) {
            motionValue = *param[0];
        }

        if(self->_motionKey == motionValue) {
            return TJS_S_OK;
        }

        // Build dict {chara, motion} and call objthis.onFindMotion(dict)
        // Aligned to libkrkr2.so Player_loadMotion_guess (0x6B0F10)
        tTJSVariant dictVar = detail::makeDictionary({
            {"chara", tTJSVariant(self->_chara)},
            {"motion", tTJSVariant(motionValue)}
        });
        tTJSVariant onFindResult;
        tTJSVariant *args[] = { &dictVar };
        tjs_error hr = objthis->FuncCall(0, TJS_W("onFindMotion"),
                                          nullptr, &onFindResult, 1, args, objthis);

        // Read back (possibly modified) chara and motion from result
        if(TJS_SUCCEEDED(hr) && onFindResult.Type() == tvtObject) {
            iTJSDispatch2 *resObj = onFindResult.AsObjectNoAddRef();
            if(resObj) {
                tTJSVariant charaVal, motionVal;
                if(TJS_SUCCEEDED(resObj->PropGet(TJS_MEMBERMUSTEXIST,
                    TJS_W("chara"), nullptr, &charaVal, resObj))
                    && charaVal.Type() != tvtVoid) {
                    self->_chara = ttstr(charaVal);
                }
                if(TJS_SUCCEEDED(resObj->PropGet(TJS_MEMBERMUSTEXIST,
                    TJS_W("motion"), nullptr, &motionVal, resObj))
                    && motionVal.Type() != tvtVoid) {
                    motionValue = ttstr(motionVal);
                }
            }
        }

        // Reset state and load
        self->_motionKey = motionValue;
        self->_runtime->activeMotion.reset();
        self->_runtime->timelines.clear();
        self->_runtime->playingTimelineLabels.clear();
        self->_runtime->drawAffineMatrix = { 1.0, 0.0, 0.0, 1.0, 0.0, 0.0 };
        self->_variableKeys.Clear();
        self->_variableValues.clear();
        self->ensureMotionLoaded();

        return TJS_S_OK;
    }

    tjs_error Player::getMotionCompat(tTJSVariant *result, tjs_int,
                                      tTJSVariant **, iTJSDispatch2 *objthis) {
        auto *self = ncbInstanceAdaptor<Player>::GetNativeInstance(objthis, true);
        if(!self) return TJS_E_INVALIDOBJECT;
        if(result) *result = tTJSVariant(self->_motionKey);
        return TJS_S_OK;
    }

    bool Player::ensureMotionLoaded() {
        if(_runtime->activeMotion) {
            return true;
        }

        const auto motionKey = detail::narrow(_motionKey);
        const bool motionKeyLooksLikeStorage =
            motionKey.find('/') != std::string::npos ||
            motionKey.find('\\') != std::string::npos ||
            motionKey.find('.') != std::string::npos;

        if(_project.Type() == tvtObject) {
            if(const auto snapshot = detail::lookupModuleSnapshot(_project)) {
                activateMotion(*_runtime, snapshot);
                syncVariableKeysFromActiveMotion();
                return true;
            }
        }

        if(motionKeyLooksLikeStorage) {
            if(const auto snapshot =
                   resolveMotion(*_runtime, _motionKey, &_resourceManagerNative)) {
                activateMotion(*_runtime, snapshot);
                syncVariableKeysFromActiveMotion();
                return true;
            }
        }

        if(const auto loaded = _resourceManagerNative.getLastLoadedModule();
           loaded.Type() == tvtObject) {
            if(const auto snapshot = detail::lookupModuleSnapshot(loaded)) {
                activateMotion(*_runtime, snapshot);
                syncVariableKeysFromActiveMotion();
                return true;
            }
        }

        if(_motionKey.IsEmpty()) {
            return false;
        }

        if(const auto snapshot =
               resolveMotion(*_runtime, _motionKey, &_resourceManagerNative)) {
            activateMotion(*_runtime, snapshot);
            syncVariableKeysFromActiveMotion();
            return true;
        }

        return false;
    }

    void Player::syncVariableKeysFromActiveMotion() {
        if(!_runtime->activeMotion) {
            _variableKeys = detail::makeArray({});
            return;
        }

        _variableKeys = detail::makeArray(
            detail::stringsToVariants(_runtime->activeMotion->variableLabels));
        syncSelectorControlsLike_0x670D1C();
    }

    void Player::syncSelectorControlsLike_0x670D1C() {
        const auto *activeMotion = _runtime->activeMotion.get();
        if(!activeMotion) {
            return;
        }

        const auto removeRuntimeState =
            [this](const std::string &label) {
                if(label.empty()) {
                    return;
                }
                _variableAnimators.erase(label);
                eraseControllerAnimatorStateLike_0x671228(label);
                _variableValues.erase(label);
                _evalResultValues.erase(label);
                removeEvalResultSlotLike_Reset(label);
            };

        for(const auto &[selectorLabel, binding] : activeMotion->selectorControls) {
            removeRuntimeState(selectorLabel);
            for(const auto &option : binding.options) {
                removeRuntimeState(option.label);
            }

            if(!_selectorEnabled) {
                continue;
            }

            // Aligned to libkrkr2.so sub_670D1C:
            // selector-enabled path resets each selector controller and
            // immediately applies sub_6680B0(..., index=0, transition=0, ease=0).
            setVariable(detail::widen(selectorLabel), 0.0, 0.0, 0.0);
        }

        _emoteDirty = true;
    }

    const detail::TimelineState *Player::primaryTimelineStateLike_0x66F80C() const {
        if(!_runtime->activeMotion) {
            return nullptr;
        }

        const auto &primaryLabels =
            !_runtime->activeMotion->mainTimelineLabels.empty()
                ? _runtime->activeMotion->mainTimelineLabels
                : _runtime->activeMotion->diffTimelineLabels;
        for(const auto &label : primaryLabels) {
            if(const auto it = _runtime->timelines.find(label);
               it != _runtime->timelines.end()) {
                return &it->second;
            }
        }

        if(!_motionKey.IsEmpty()) {
            if(const auto it = _runtime->timelines.find(detail::narrow(_motionKey));
               it != _runtime->timelines.end()) {
                return &it->second;
            }
        }

        return !_runtime->timelines.empty()
            ? &(_runtime->timelines.begin()->second)
            : nullptr;
    }

    void Player::resetControllerStateLike_0x66EB8C() {
        // Aligned to libkrkr2.so sub_66EB8C:
        // the binary performs a broad controller/reset sweep after wrapper-side
        // setMirror(). Keep the local reset focused on runtime controller state,
        // eval sinks, and root-node dirty propagation.
        _variableAnimators.clear();
        clearControllerAnimatorStateLike_0x671228();
        _evalResultValues.clear();
        _evalResultList.clear();
        _evalResultListIndex.clear();
        _mirrorPositiveCache.clear();
        _mirrorNegativeCache.clear();

        if(_runtime && !_runtime->nodes.empty()) {
            auto &root = _runtime->nodes.front();
            root.localState.flipX = _rootFlipX;
            root.localState.dirty = true;
            root.accumulated.dirty = true;
            root.interpolatedCache.flipX = _rootFlipX;
        }

        if(_selectorEnabled) {
            syncSelectorControlsLike_0x670D1C();
        }
        _emoteDirty = true;
    }

    const detail::MotionClip *Player::selectActiveClip() const {
        if(!_runtime->activeMotion) {
            return nullptr;
        }

        const auto selectByLabel =
            [this](const std::string &label) -> const detail::MotionClip * {
                if(label.empty()) {
                    return nullptr;
                }
                const auto it = _runtime->activeMotion->clipsByLabel.find(label);
                return it != _runtime->activeMotion->clipsByLabel.end()
                    ? &it->second
                    : nullptr;
            };

        for(const auto &label : _runtime->playingTimelineLabels) {
            if(const auto *clip = selectByLabel(label)) {
                return clip;
            }
        }

        const auto &primaryLabels =
            !_runtime->activeMotion->mainTimelineLabels.empty()
                ? _runtime->activeMotion->mainTimelineLabels
                : _runtime->activeMotion->diffTimelineLabels;
        for(const auto &label : primaryLabels) {
            if(const auto *clip = selectByLabel(label)) {
                return clip;
            }
        }

        if(_runtime->activeMotion->clipsByLabel.size() == 1) {
            return &_runtime->activeMotion->clipsByLabel.begin()->second;
        }

        return nullptr;
    }

    const std::vector<std::string> &Player::activeLayerNames() const {
        static const std::vector<std::string> empty;
        if(!_runtime->activeMotion) {
            return empty;
        }

        if(const auto *clip = selectActiveClip();
           clip && !clip->layerNames.empty()) {
            return clip->layerNames;
        }

        return _runtime->activeMotion->layerNames;
    }

    const std::unordered_map<
        std::string, std::shared_ptr<const PSB::PSBDictionary>> *
    Player::activeLayersByName() const {
        if(!_runtime->activeMotion) {
            return nullptr;
        }

        if(const auto *clip = selectActiveClip();
           clip && !clip->layersByName.empty()) {
            return &clip->layersByName;
        }

        return &_runtime->activeMotion->layersByName;
    }

    const std::vector<std::string> &Player::activeSourceCandidates() const {
        static const std::vector<std::string> empty;
        if(!_runtime->activeMotion) {
            return empty;
        }

        if(const auto *clip = selectActiveClip();
           clip && !clip->sourceCandidates.empty()) {
            return clip->sourceCandidates;
        }

        return _runtime->activeMotion->sourceCandidates;
    }

    tTJSVariant Player::getVariableKeys() {
        ensureMotionLoaded();
        if(_variableKeys.Type() == tvtVoid) {
            return detail::makeArray({});
        }
        return _variableKeys;
    }

    void Player::setProgressCompat(double v) {
        ensureMotionLoaded();
        const auto progress = std::clamp(v, 0.0, 1.0);
        _runtime->playingTimelineLabels.clear();
        for(auto &[_, state] : _runtime->timelines) {
            if(state.totalFrames > 0.0) {
                state.currentTime = state.totalFrames * progress;
            } else {
                state.currentTime = progress;
            }
            if(progress >= 1.0 && !state.loop) {
                state.playing = false;
            }
            state.controlInitialized = false;
            state.controlLastAppliedTime = state.currentTime;
            state.controlFrameCursor.clear();
            state.controlTrackValues.clear();
            state.controlTrackAnimators.clear();
            if(state.playing) {
                _runtime->playingTimelineLabels.push_back(state.label);
            }
        }
        _allplaying = !_runtime->playingTimelineLabels.empty();
    }

    double Player::getProgressCompat() const {
        bool sawTimeline = false;
        bool anyPlaying = false;
        double progress = 0.0;

        for(const auto &[_, state] : _runtime->timelines) {
            sawTimeline = true;
            anyPlaying = anyPlaying || state.playing;
            if(state.totalFrames > 0.0) {
                progress = std::max(
                    progress,
                    std::clamp(state.currentTime / state.totalFrames, 0.0, 1.0));
            } else if(!state.playing) {
                progress = std::max(progress, 1.0);
            }
        }

        if(!sawTimeline) {
            return _allplaying ? 0.0 : 1.0;
        }
        if(!anyPlaying) {
            return 1.0;
        }
        return progress;
    }

    // --- Core methods ---
    // Aligned to libkrkr2.so sub_6BA7B8 at 0x6BA7B8:
    // 1. sub_A0F5E0(v9, a1+992) — read TJS dispatch from player+992
    // 2. FuncCall(obj, 0, L"random", ...) — call "random" method
    // 3. Convert result variant to double (case 2→real, case 4→int→double, case 5→raw)
    //
    // player+992 is initialized once via "new Math.RandomGenerator()" (sub_6A88CC at 0x6A8988).
    // Child Players inherit the same object from parent (sub_6CED30 at 0x6CED30: a1+992 = a2).
    double Player::random() {
        if (_tjsRandomGenerator.Type() == tvtObject) {
            iTJSDispatch2 *obj = _tjsRandomGenerator.AsObjectNoAddRef();
            if (obj) {
                tTJSVariant result;
                static tjs_uint32 hint = 0;
                tjs_error hr = obj->FuncCall(0, TJS_W("random"), &hint,
                                             &result, 0, nullptr, obj);
                if (TJS_SUCCEEDED(hr))
                    return static_cast<double>(result);
            }
        }
        return 0.0;
    }

    // Aligned to libkrkr2.so sub_6709AC at 0x6709AC:
    // initPhysics(min, max, amp, freq1, freq2) creates a Spring physics object
    // at player+1128 (1564 bytes, sub_670AFC). Stores params at player+1136..1152.
    // The Spring physics system drives emote hair/bust/parts oscillation.
    // Full spring simulation not yet implemented for web port — store params only.
    void Player::initPhysics() {
        // Parameters come from NCB: initPhysics(min, max, amp, freq1, freq2)
        // In the NCB binding this is a raw callback. The actual parameters
        // are parsed by the NCB wrapper. Since we store the scale values
        // (hairScale/partsScale/bustScale) and these physics params would
        // drive them, we log but accept the call.
        // player+1128 = physics object (not created)
        // player+1136..1152 = min, max, amplitude, freq1, freq2
    }
    tTJSVariant Player::serialize() {
        ensureMotionLoaded();

        std::vector<std::pair<std::string, tTJSVariant>> variables;
        std::unordered_set<std::string> seenVariables;
        if(_runtime->activeMotion) {
            for(const auto &label : _runtime->activeMotion->variableLabels) {
                seenVariables.insert(label);
                variables.emplace_back(label, getVariable(detail::widen(label)));
            }
        }
        for(const auto &[label, value] : _variableValues) {
            if(seenVariables.insert(label).second) {
                variables.emplace_back(label, value);
            }
        }

        return detail::makeDictionary({
            { "chara", _chara },
            { "motion", _motionKey },
            { "tickcount", getTickCount() },
            { "speed", _speed },
            { "outline", tTJSVariant(_outline) },
            { "variables", detail::makeDictionary(variables) },
            { "timelines", getPlayingTimelineInfoList() },
        });
    }

    void Player::unserialize(tTJSVariant data) {
        if(data.Type() != tvtObject || data.AsObjectNoAddRef() == nullptr) {
            return;
        }

        tTJSVariant value;
        if(getObjectProperty(data, TJS_W("chara"), value) &&
           value.Type() != tvtVoid) {
            _chara = value;
        }

        if(getObjectProperty(data, TJS_W("motion"), value) &&
           value.Type() != tvtVoid) {
            _motionKey = value;
            ensureMotionLoaded();
        }

        if(getObjectProperty(data, TJS_W("tickcount"), value) &&
           value.Type() != tvtVoid) {
            setTickCount(value.AsReal());
        }

        if(getObjectProperty(data, TJS_W("speed"), value) &&
           value.Type() != tvtVoid) {
            _speed = value.AsReal();
        }

        if(getObjectProperty(data, TJS_W("outline"), value) &&
           value.Type() != tvtVoid) {
            _outline = ttstr(value);
        }

        if(getObjectProperty(data, TJS_W("variables"), value) &&
           value.Type() == tvtObject && value.AsObjectNoAddRef() != nullptr) {
            DictionaryEnumerator callback;
            tTJSVariantClosure closure(&callback, nullptr);
            value.AsObjectNoAddRef()->EnumMembers(TJS_IGNOREPROP, &closure,
                                                  value.AsObjectNoAddRef());
            for(const auto &[label, stored] : callback.entries) {
                if(stored.Type() != tvtVoid) {
                    setVariable(label, stored.AsReal());
                }
            }
        }

        bool restoredTimelines = false;
        if(getObjectProperty(data, TJS_W("timelines"), value) &&
           value.Type() == tvtObject && value.AsObjectNoAddRef() != nullptr) {
            ensureMotionLoaded();
            if(_runtime->activeMotion && _runtime->timelines.empty()) {
                detail::primeTimelineStates(_runtime->timelines,
                                            *_runtime->activeMotion);
            }
            _runtime->playingTimelineLabels.clear();

            const auto count = getObjectCount(value);
            for(tjs_int index = 0; index < count; ++index) {
                tTJSVariant item;
                if(!getArrayItem(value, index, item) || item.Type() != tvtObject ||
                   item.AsObjectNoAddRef() == nullptr) {
                    continue;
                }

                tTJSVariant labelValue;
                if(!getObjectProperty(item, TJS_W("label"), labelValue) ||
                   labelValue.Type() == tvtVoid) {
                    continue;
                }

                const auto key = detail::narrow(labelValue);
                auto it = _runtime->timelines.find(key);
                if(it == _runtime->timelines.end()) {
                    continue;
                }

                restoredTimelines = true;
                it->second.playing = true;
                _runtime->playingTimelineLabels.push_back(key);
                it->second.controlInitialized = false;
                it->second.controlLastAppliedTime = it->second.currentTime;
                it->second.controlFrameCursor.clear();
                it->second.controlTrackValues.clear();
                it->second.controlTrackAnimators.clear();

                tTJSVariant flagsValue;
                if(getObjectProperty(item, TJS_W("flags"), flagsValue) &&
                   flagsValue.Type() != tvtVoid) {
                    it->second.flags = flagsValue.AsInteger();
                }

                tTJSVariant currentTimeValue;
                if(getObjectProperty(item, TJS_W("currentTime"), currentTimeValue) &&
                   currentTimeValue.Type() != tvtVoid) {
                    it->second.currentTime = currentTimeValue.AsReal();
                }

                tTJSVariant blendRatioValue;
                if(getObjectProperty(item, TJS_W("blendRatio"), blendRatioValue) &&
                   blendRatioValue.Type() != tvtVoid) {
                    it->second.blendRatio = blendRatioValue.AsReal();
                }
            }
        }

        if(!restoredTimelines && ensureMotionLoaded()) {
            if(_runtime->timelines.empty()) {
                detail::primeTimelineStates(_runtime->timelines,
                                            *_runtime->activeMotion);
            }
            const auto &primary = !_runtime->activeMotion->mainTimelineLabels.empty()
                ? _runtime->activeMotion->mainTimelineLabels
                : _runtime->activeMotion->diffTimelineLabels;
            for(const auto &label : primary) {
                playTimeline(detail::widen(label), PlayFlagForce);
            }
        }

        _allplaying = !_runtime->playingTimelineLabels.empty();
    }

    // Aligned to libkrkr2.so D3DEmotePlayer_setCoord (0x5301EC):
    // store the coord animator payload on Player and keep root x/y in sync.
    void Player::setEmoteCoord(double x, double y, double transition,
                               double ease) {
        _emoteCoordState.x = x;
        _emoteCoordState.y = y;
        _emoteCoordState.transition = transition;
        _emoteCoordState.ease = ease;
        setX(x);
        setY(y);
        _emoteDirty = true;
    }

    // Aligned to libkrkr2.so D3DEmotePlayer_setScale (0x530260):
    // the wrapper multiplies baseScale * userScale, then forwards the final
    // scalar plus transition/ease to the inner Player scale animator.
    void Player::setEmoteScale(double scale, double transition, double ease) {
        _emoteScaleState.value = scale;
        _emoteScaleState.transition = transition;
        _emoteScaleState.ease = ease;
        _emoteDirty = true;
    }

    // Aligned to libkrkr2.so D3DEmotePlayer_setRot (0x5302E4):
    // read player+1161, set player+1162=1, then forward rot/transition/ease
    // to the Player rot animator sink.
    void Player::setRotate(double rot, double transition, double ease) {
        _rotateAngle = rot;
        _emoteRotState.value = rot;
        _emoteRotState.transition = transition;
        _emoteRotState.ease = ease;
        _emoteDirty = true;
    }

    // Aligned to libkrkr2.so D3DEmotePlayer_setColor (0x530314):
    // unpack AARRGGBB into four float byte values and forward them to the
    // Player color animator sink together with transition/ease.
    void Player::setEmoteColor(tjs_uint32 color, double transition,
                               double ease) {
        _emoteColorState.packed = color;
        _emoteColorState.rgbaBytes[0] =
            static_cast<float>(static_cast<std::uint8_t>(color));
        _emoteColorState.rgbaBytes[1] =
            static_cast<float>(static_cast<std::uint8_t>(color >> 8));
        _emoteColorState.rgbaBytes[2] =
            static_cast<float>(static_cast<std::uint8_t>(color >> 16));
        _emoteColorState.rgbaBytes[3] =
            static_cast<float>(static_cast<std::uint8_t>(color >> 24));
        _emoteColorState.transition = transition;
        _emoteColorState.ease = ease;
        _emoteDirty = true;
    }

    void Player::setMirror(bool mirror) {
        // Aligned to libkrkr2.so Player_setRootFlipX (0x6CD068):
        // update the synthetic root node's flipX flag and mark it dirty.
        if(_rootFlipX == mirror && _mirrorEvalEnabled == mirror) {
            return;
        }

        _rootFlipX = mirror;
        _mirrorEvalEnabled = mirror;
        resetControllerStateLike_0x66EB8C();
    }

    void Player::setEmoteMeshDivisionRatio(double v) {
        _emoteMeshDivisionRatio = v;
        _emoteMeshDivisionRatioDup = v;
    }

    // Aligned to libkrkr2.so:
    // sub_681F20: player+1184 = a2
    void Player::setHairScale(double s) { _hairScale = s; }
    // sub_681F28: player+1192 = a2
    void Player::setPartsScale(double s) { _partsScale = s; }
    // sub_681F30: player+1200 = a2
    void Player::setBustScale(double s) { _bustScale = s; }

    // Aligned to D3DEmotePlayer_startWind (0x530680) -> Player_startWind (0x6709AC):
    // normalize amplitude, optionally destroy/rebuild wind simulator state, then
    // store min/max/amplitude/freq and reset the active counter.
    void Player::startWind(double minAngle, double maxAngle, double amplitude,
                           double freqX, double freqY) {
        const double absAmplitude = std::abs(amplitude);
        const double normalizedMin = amplitude >= 0.0 ? minAngle : maxAngle;
        const double normalizedMax = amplitude >= 0.0 ? maxAngle : minAngle;

        if(absAmplitude == 0.0 || normalizedMin == normalizedMax ||
           (freqX == 0.0 && freqY == 0.0)) {
            stopWind();
            return;
        }

        const bool rebuild = !_windState.active ||
            _windState.minAngle != normalizedMin ||
            _windState.maxAngle != normalizedMax;
        if(rebuild) {
            _windState = {};
            _windState.active = true;
        }

        _windState.active = true;
        _windState.minAngle = normalizedMin;
        _windState.maxAngle = normalizedMax;
        _windState.amplitude = absAmplitude;
        _windState.freqX = freqX;
        _windState.freqY = freqY;
        const double direction = _windState.prevPhase > _windState.phase
            ? -1.0
            : 1.0;
        const double ratio = _emoteMeshDivisionRatio != 0.0
            ? _emoteMeshDivisionRatio
            : 1.0;
        _windState.scaledAmplitude = direction * (absAmplitude / ratio);
        _windState.counter = 0;
        _emoteDirty = true;
    }

    // Aligned to sub_681A38: delete wind simulator and clear player+1128.
    void Player::stopWind() {
        _windState = {};
        _emoteDirty = true;
    }

    // Aligned to D3DEmotePlayer_setOuterForce (0x530A8C) ->
    // Player_setOuterForce (0x672D58): case-insensitive label dispatch for
    // "bust", "h", and "parts", carrying transition/ease through the sink.
    void Player::setOuterForce(ttstr label, double x, double y,
                               double transition, double ease) {
        const auto key = lowerAscii(detail::narrow(label));
        OuterForceState *target = nullptr;
        if(key == "bust") {
            target = &_bustOuterForce;
        } else if(key == "h") {
            target = &_hairOuterForce;
        } else if(key == "parts") {
            target = &_partsOuterForce;
        } else {
            return;
        }

        target->active = true;
        target->x = x;
        target->y = y;
        target->transition = transition;
        target->ease = ease;
        _emoteDirty = true;
    }

    // Aligned to libkrkr2.so sub_681EF8 at 0x681EF8:
    // Stores translate (x,y) to runtime+144/148 (cameraOffsetX/Y).
    // The full 6-param matrix version is handled by setDrawAffineTranslateMatrixCompat.
    void Player::setDrawAffineTranslateMatrix(tTJSVariant) {
        // Single-param variant: compat handler does the real work via NCB_METHOD_RAW
    }

    tTJSVariant Player::getCameraOffset() { return _cameraPosition; }

    void Player::setCameraOffset(tTJSVariant offset) {
        _cameraPosition = offset;
        // Aligned to libkrkr2.so sub_6D9A38: setCameraOffset(x, y)
        // Stores as float at Player+144/148. NCB passes a Point with x,y.
        if(offset.Type() == tvtObject) {
            auto *obj = offset.AsObjectNoAddRef();
            if(obj) {
                tTJSVariant xv, yv;
                if(obj->PropGet(0, TJS_W("x"), nullptr, &xv, obj) == TJS_S_OK)
                    _cameraOffsetX = static_cast<float>(xv.AsReal());
                if(obj->PropGet(0, TJS_W("y"), nullptr, &yv, obj) == TJS_S_OK)
                    _cameraOffsetY = static_cast<float>(yv.AsReal());
            }
        }
    }

    void Player::modifyRoot(tTJSVariant data) { _project = data; }

    void Player::debugPrint() {
        LOGGER->info("motionKey={}, motions={}, sources={}, timelines={}",
                     _motionKey.AsStdString(), _runtime->motionsByKey.size(),
                     _runtime->sourcesByKey.size(), _runtime->timelines.size());
    }


} // namespace motion
