// PlayerQuery.cpp — Viewport, timeline/variable queries, selector, misc, compat
// Split from Player.cpp for maintainability.
//
#include "PlayerInternal.h"
#include "HitTestInternal.h"

using namespace motion::internal;

namespace {
    float variableEaseWeightLike_0x671228(double ease) {
        if(ease > 0.0) {
            return static_cast<float>(ease + 1.0);
        }
        if(ease < 0.0) {
            return static_cast<float>(1.0 / (1.0 - ease));
        }
        return 1.0f;
    }

    bool hitTestMotionNodeShape(const motion::detail::MotionNode &node,
                                double x, double y) {
        motion::detail::HitData hit{};
        hit.type = node.shapeGeomType;
        for(size_t i = 0; i < std::size(node.shapeVertices) &&
                          i < hit.values.size();
            ++i) {
            hit.values[i] = node.shapeVertices[i];
        }
        return motion::detail::hitTestHitData(hit, x, y);
    }
}

namespace motion {

    // --- Viewport/display ---
    void Player::setFlip(bool v) { _runtime->flip = v; }

    bool Player::shouldMirrorEvalLabelLike_0x67C6B0(const std::string &label) {
        if(!_mirrorEvalEnabled || label.empty() || !_runtime->activeMotion) {
            return false;
        }

        if(_mirrorPositiveCache.find(label) != _mirrorPositiveCache.end()) {
            return true;
        }
        if(_mirrorNegativeCache.find(label) != _mirrorNegativeCache.end()) {
            return false;
        }

        const auto &matchList = _runtime->activeMotion->mirrorVariableMatchList;
        const bool matched =
            std::find(matchList.begin(), matchList.end(), label) !=
            matchList.end();
        if(matched) {
            _mirrorPositiveCache.insert(label);
        } else {
            _mirrorNegativeCache.insert(label);
        }
        return matched;
    }

    double &Player::ensureEvalResultSlotLike_0x686944(const std::string &label) {
        if(const auto it = _evalResultListIndex.find(label);
           it != _evalResultListIndex.end()) {
            return it->second->value;
        }

        _evalResultList.push_back(EvalResultEntry{label, 0.0});
        auto it = _evalResultList.end();
        --it;
        _evalResultListIndex[label] = it;
        return it->value;
    }

    void Player::removeEvalResultSlotLike_Reset(const std::string &label) {
        if(const auto it = _evalResultListIndex.find(label);
           it != _evalResultListIndex.end()) {
            _evalResultList.erase(it->second);
            _evalResultListIndex.erase(it);
        }
    }

    void Player::writeEvalResultValueLike_0x6C4668(const std::string &label,
                                                   double value) {
        if(label.empty()) {
            return;
        }
        ensureEvalResultSlotLike_0x686944(label) = value;
        _variableValues[label] = value;
        _evalResultValues[label] = value;
    }

    void Player::setOpacity(double v) { _runtime->opacity = v; }

    void Player::setVisible(bool v) { _runtime->visible = v; }

    void Player::setSlant(double v) { _runtime->slant = v; }

    void Player::setZoom(double v) { _runtime->zoom = v; }

    tTJSVariant Player::getLayerNames() {
        if(!_runtime->activeMotion) {
            return detail::makeArray({});
        }
        return detail::makeArray(detail::stringsToVariants(activeLayerNames()));
    }

    void Player::releaseSyncWait() {
        _syncWaiting = false;
        _syncActive = false;
    }

    void Player::calcViewParam() {
        _runtime->lastViewParam = detail::makeDictionary({
            { "flip", _runtime->flip },
            { "opacity", _runtime->opacity },
            { "visible", _runtime->visible },
            { "slant", _runtime->slant },
            { "zoom", _runtime->zoom },
            { "zFactor", _zFactor },
            { "colorWeight", getColorWeight() },
        });
    }

    tTJSVariant Player::getLayerMotion(ttstr name) {
        const auto *layers = activeLayersByName();
        if(!layers) {
            return {};
        }

        const auto key = detail::narrow(name);
        if(const auto it = layers->find(key); it != layers->end()) {
            return it->second->toTJSVal();
        }

        return {};
    }

    tTJSVariant Player::getLayerGetter(ttstr name) {
        const auto layer = getLayerMotion(name);
        if(layer.Type() == tvtVoid) {
            return {};
        }

        const auto layerId = requireLayerId(name);
        return detail::makeDictionary({
            { "name", name },
            { "id", layerId },
            { "motion", layer },
        });
    }

    tTJSVariant Player::getLayerGetterList() {
        if(!_runtime->activeMotion) {
            return detail::makeArray({});
        }

        std::vector<tTJSVariant> items;
        for(const auto &layerName : activeLayerNames()) {
            const auto getter = getLayerGetter(detail::widen(layerName));
            if(getter.Type() != tvtVoid) {
                items.push_back(getter);
            }
        }
        return detail::makeArray(items);
    }

    void Player::skipToSync() {
        for(auto &[_, state] : _runtime->timelines) {
            if(state.totalFrames > 0.0) {
                state.currentTime = state.totalFrames;
            }
            if(!state.loop) {
                state.playing = false;
            }
        }
        if(const auto it = std::remove_if(_runtime->playingTimelineLabels.begin(),
                                          _runtime->playingTimelineLabels.end(),
                                          [this](const std::string &label) {
                                              const auto found =
                                                  _runtime->timelines.find(label);
                                              return found ==
                                                      _runtime->timelines.end() ||
                                                  !found->second.playing;
                                          });
           it != _runtime->playingTimelineLabels.end()) {
            _runtime->playingTimelineLabels.erase(
                it, _runtime->playingTimelineLabels.end());
        }
        _syncWaiting = false;
        _syncActive = false;
        _allplaying = !_runtime->playingTimelineLabels.empty();
    }

    void Player::setStereovisionCameraPosition(double x, double y, double z) {
        iTJSDispatch2 *array = TJSCreateArrayObject();
        tTJSVariant vx = x;
        tTJSVariant vy = y;
        tTJSVariant vz = z;
        static tjs_uint addHint = 0;
        tTJSVariant *argsX[] = { &vx };
        tTJSVariant *argsY[] = { &vy };
        tTJSVariant *argsZ[] = { &vz };
        array->FuncCall(0, TJS_W("add"), &addHint, nullptr, 1, argsX, array);
        array->FuncCall(0, TJS_W("add"), &addHint, nullptr, 1, argsY, array);
        array->FuncCall(0, TJS_W("add"), &addHint, nullptr, 1, argsZ, array);
        _cameraPosition = tTJSVariant(array, array);
        array->Release();
    }

    // --- Timeline/variable queries ---
    void Player::setVariableResolvedWeightLike_0x671228(
        const std::string &key, double value, double transition,
        double easeWeight) {
        const auto *activeMotion = _runtime->activeMotion.get();
        const auto bindingIt = activeMotion
            ? activeMotion->controllerBindings.find(key)
            : decltype(activeMotion->controllerBindings.find(key)){};
        const bool hasBinding =
            activeMotion && bindingIt != activeMotion->controllerBindings.end();

        if(hasBinding) {
            const auto queueControllerStateLikeBinary =
                [&](const std::string &targetKey,
                    VariableAnimatorState &state,
                    double currentValueInput,
                    double requestedValue,
                    double requestedTransition,
                    double requestedEaseWeight) {
                    const auto currentValue =
                        static_cast<float>(currentValueInput);
                    const auto targetValue =
                        static_cast<float>(requestedValue);
                    if(requestedTransition <= 0.0) {
                        state.queue.clear();
                        state.active = false;
                        state.currentValue = targetValue;
                        state.startValue = targetValue;
                        state.targetValue = targetValue;
                        state.progress = 1.0f;
                        state.duration = 0.0f;
                        state.weight =
                            static_cast<float>(requestedEaseWeight);
                        _variableValues[targetKey] = requestedValue;
                        ensureEvalResultSlotLike_0x686944(targetKey) =
                            requestedValue;
                        _evalResultValues[targetKey] = requestedValue;
                        return;
                    }

                    if(!_emoteAnimatorFlag) {
                        state.queue.clear();
                        state.active = false;
                        state.currentValue = currentValue;
                        state.startValue = currentValue;
                        state.targetValue = currentValue;
                        state.progress = 1.0f;
                        state.duration = 0.0f;
                    }

                    state.queue.push_back(VariableKeyframe{
                        targetValue,
                        static_cast<float>(requestedTransition),
                        static_cast<float>(requestedEaseWeight),
                    });
                    _variableValues[targetKey] = state.currentValue;
                    ensureEvalResultSlotLike_0x686944(targetKey) =
                        state.currentValue;
                    _evalResultValues[targetKey] = state.currentValue;
                };

            const auto queueControllerLikeBinary =
                [&](VariableAnimatorState &state,
                    double requestedValue,
                    double requestedTransition,
                    double requestedEaseWeight) {
                    queueControllerStateLikeBinary(
                        key, state,
                        _variableValues.count(key) ? _variableValues[key]
                                                   : getVariable(detail::widen(key)),
                        requestedValue, requestedTransition,
                        requestedEaseWeight);
                };

            switch(bindingIt->second.type) {
                case 0:
                case 1:
                case 2:
                    // Aligned to 0x671228 cases 0/1/2:
                    // these labels are routed to physics control groups, not to
                    // the generic eval-result map / animator sink.
                    _emoteDirty = true;
                    return;
                case 3:
                    // Aligned to 0x671228 default route for loopControl-built
                    // entries: no generic eval-result write happens here.
                    _emoteDirty = true;
                    return;
                case 4:
                case 5:
                case 7:
                case 8: {
                    if(bindingIt->second.type == 8 && activeMotion) {
                        const auto selectorIt =
                            activeMotion->selectorControls.find(key);
                        if(selectorIt != activeMotion->selectorControls.end()) {
                            const int selectedIndex =
                                static_cast<int>(value);
                            eraseControllerAnimatorStateLike_0x671228(key);
                            _variableValues[key] =
                                static_cast<double>(selectedIndex);
                            ensureEvalResultSlotLike_0x686944(key) =
                                static_cast<double>(selectedIndex);
                            _evalResultValues[key] =
                                static_cast<double>(selectedIndex);

                            const double resolvedEaseWeight = easeWeight;
                            int optionIndex = 0;
                            for(const auto &option : selectorIt->second.options) {
                                if(option.label.empty()) {
                                    ++optionIndex;
                                    continue;
                                }
                                const double targetValue =
                                    optionIndex == selectedIndex
                                        ? option.onValue
                                        : option.offValue;
                                const auto currentIt =
                                    _evalResultValues.find(option.label);
                                const double currentValue =
                                    currentIt != _evalResultValues.end()
                                        ? currentIt->second
                                        : (_variableValues.count(option.label)
                                               ? _variableValues[option.label]
                                               : getVariable(
                                                     detail::widen(option.label)));
                                const double range =
                                    std::abs(option.onValue - option.offValue);
                                const double scaledTransition =
                                    transition > 0.0 && range > 0.0000001
                                        ? std::abs(targetValue - currentValue) /
                                              range * transition
                                        : 0.0;
                                auto &optionState =
                                    _type8ControllerAnimators[option.label];
                                queueControllerStateLikeBinary(
                                    option.label, optionState, currentValue,
                                    targetValue, scaledTransition,
                                    resolvedEaseWeight);
                                ++optionIndex;
                            }
                            _emoteDirty = true;
                            return;
                        }
                    }
                    auto *bucket =
                        controllerAnimatorBucketLike_0x671228(
                            bindingIt->second.type);
                    if(!bucket) {
                        _emoteDirty = true;
                        return;
                    }
                    auto &state = (*bucket)[key];
                    ensureEvalResultSlotLike_0x686944(key);
                    queueControllerLikeBinary(state, value, transition,
                                              easeWeight);
                    _emoteDirty = true;
                    return;
                }
                case 6: {
                    if(bindingIt->second.role == "label") {
                        eraseControllerAnimatorStateLike_0x671228(key);
                        const double directValue =
                            static_cast<double>(static_cast<int>(value));
                        _variableValues[key] = directValue;
                        ensureEvalResultSlotLike_0x686944(key) = directValue;
                        _evalResultValues[key] = directValue;
                        _emoteDirty = true;
                        return;
                    }
                    auto &state = _type6ControllerAnimators[key];
                    ensureEvalResultSlotLike_0x686944(key);
                    queueControllerLikeBinary(state, value, transition,
                                              easeWeight);
                    _emoteDirty = true;
                    return;
                }
                default:
                    _emoteDirty = true;
                    return;
            }
        }

        // Aligned to Player_setVariable (0x671228): labels without a controller
        // binding bypass animator queues and write the eval map immediately.
        _variableAnimators.erase(key);
        _variableValues[key] = value;
        ensureEvalResultSlotLike_0x686944(key) = value;
        _evalResultValues[key] = value;
        _emoteDirty = true;
    }

    void Player::setVariable(ttstr label, double value, double transition,
                             double ease) {
        const auto key = detail::narrow(label);
        if(key.empty()) {
            return;
        }

        setVariableResolvedWeightLike_0x671228(
            key, value, transition, variableEaseWeightLike_0x671228(ease));
    }

    double Player::getVariable(ttstr label) {
        ensureMotionLoaded();
        const auto key = detail::narrow(label);
        if(key.empty()) {
            return 0.0;
        }

        if(const auto it = _variableValues.find(key); it != _variableValues.end()) {
            return it->second;
        }

        if(!_runtime->activeMotion) {
            return 0.0;
        }

        if(const auto it = _runtime->activeMotion->variableFrames.find(key);
           it != _runtime->activeMotion->variableFrames.end() &&
           !it->second.empty()) {
            return it->second.front().value;
        }

        if(const auto it = _runtime->activeMotion->variableRanges.find(key);
           it != _runtime->activeMotion->variableRanges.end()) {
            return it->second.first;
        }

        return 0.0;
    }

    tjs_int Player::countVariables() {
        ensureMotionLoaded();
        return _runtime->activeMotion
            ? static_cast<tjs_int>(_runtime->activeMotion->variableLabels.size())
            : 0;
    }

    ttstr Player::getVariableLabelAt(tjs_int idx) {
        ensureMotionLoaded();
        if(!_runtime->activeMotion || idx < 0 ||
           static_cast<size_t>(idx) >= _runtime->activeMotion->variableLabels.size()) {
            return {};
        }
        return detail::widen(_runtime->activeMotion->variableLabels[idx]);
    }

    tjs_int Player::countVariableFrameAt(tjs_int idx) {
        const auto label = getVariableLabelAt(idx);
        if(label.IsEmpty()) {
            return 0;
        }
        const auto frames = getVariableFrameList(label);
        return getObjectCount(frames);
    }

    ttstr Player::getVariableFrameLabelAt(tjs_int idx, tjs_int frameIdx) {
        const auto label = getVariableLabelAt(idx);
        if(label.IsEmpty()) {
            return {};
        }

        const auto key = detail::narrow(label);
        if(!_runtime->activeMotion) {
            return {};
        }
        const auto it = _runtime->activeMotion->variableFrames.find(key);
        if(it == _runtime->activeMotion->variableFrames.end() || frameIdx < 0 ||
           static_cast<size_t>(frameIdx) >= it->second.size()) {
            return {};
        }
        return detail::widen(it->second[frameIdx].label);
    }

    double Player::getVariableFrameValueAt(tjs_int idx, tjs_int frameIdx) {
        const auto label = getVariableLabelAt(idx);
        if(label.IsEmpty()) {
            return 0.0;
        }

        const auto key = detail::narrow(label);
        if(!_runtime->activeMotion) {
            return 0.0;
        }
        const auto it = _runtime->activeMotion->variableFrames.find(key);
        if(it == _runtime->activeMotion->variableFrames.end() || frameIdx < 0 ||
           static_cast<size_t>(frameIdx) >= it->second.size()) {
            return 0.0;
        }
        return it->second[frameIdx].value;
    }

    bool Player::getTimelinePlaying(ttstr label) {
        ensureMotionLoaded();
        const auto key = detail::narrow(label);
        if(const auto it = _runtime->timelines.find(key);
           it != _runtime->timelines.end()) {
            return it->second.playing;
        }
        return false;
    }

    tTJSVariant Player::getVariableRange(ttstr label) {
        ensureMotionLoaded();
        if(!_runtime->activeMotion) {
            return {};
        }

        const auto key = detail::narrow(label);
        if(const auto it = _runtime->activeMotion->variableRanges.find(key);
           it != _runtime->activeMotion->variableRanges.end()) {
            return detail::makeArray(
                { tTJSVariant(it->second.first), tTJSVariant(it->second.second) });
        }
        return {};
    }

    tTJSVariant Player::getVariableFrameList(ttstr label) {
        ensureMotionLoaded();
        if(!_runtime->activeMotion) {
            return detail::makeArray({});
        }

        const auto key = detail::narrow(label);
        if(const auto it = _runtime->activeMotion->variableFrames.find(key);
           it == _runtime->activeMotion->variableFrames.end()) {
            return detail::makeArray({});
        } else {
            std::vector<tTJSVariant> frames;
            for(const auto &frame : it->second) {
                frames.push_back(detail::makeDictionary({
                    { "label", detail::widen(frame.label) },
                    { "frame", frame.value },
                    { "value", frame.value },
                }));
            }
            return detail::makeArray(frames);
        }
    }

    bool Player::hitTestLayer(ttstr name, double x, double y) {
        ensureMotionLoaded();
        ensureNodeTreeBuilt();
        if(!_runtime || !_runtime->activeMotion) {
            return false;
        }

        if(!_runtime->nodes.empty()) {
            updateLayers();
            calcBounds();
        }

        const auto key = detail::narrow(name);
        if(key.empty()) {
            return false;
        }

        auto findNodeRecursive =
            [&](auto &&self, Player *player) -> const detail::MotionNode * {
            if(!player || !player->_runtime) {
                return nullptr;
            }

            if(const auto it = player->_runtime->nodeLabelMap.find(key);
               it != player->_runtime->nodeLabelMap.end()) {
                const auto index = it->second;
                if(index >= 0 &&
                   index < static_cast<int>(player->_runtime->nodes.size())) {
                    return &player->_runtime->nodes[static_cast<size_t>(index)];
                }
            }

            for(auto &node : player->_runtime->nodes) {
                if(node.nodeType == 3) {
                    if(auto *child = node.getChildPlayer()) {
                        if(const auto *found = self(self, child)) {
                            return found;
                        }
                    }
                } else if(node.nodeType == 4) {
                    const int particleCount = node.getParticleCount();
                    for(int i = 0; i < particleCount; ++i) {
                        if(auto *child = node.getParticleChild(i)) {
                            if(const auto *found = self(self, child)) {
                                return found;
                            }
                        }
                    }
                }
            }

            return nullptr;
        };

        if(const auto *node = findNodeRecursive(findNodeRecursive, this)) {
            return hitTestMotionNodeShape(*node, x, y);
        }
        return false;
    }

    tjs_int Player::countMainTimelines() {
        ensureMotionLoaded();
        return _runtime->activeMotion
            ? static_cast<tjs_int>(_runtime->activeMotion->mainTimelineLabels.size())
            : 0;
    }

    ttstr Player::getMainTimelineLabelAt(tjs_int idx) {
        ensureMotionLoaded();
        if(!_runtime->activeMotion || idx < 0 ||
           static_cast<size_t>(idx) >=
               _runtime->activeMotion->mainTimelineLabels.size()) {
            return {};
        }
        return detail::widen(_runtime->activeMotion->mainTimelineLabels[idx]);
    }

    tTJSVariant Player::getMainTimelineLabelList() {
        ensureMotionLoaded();
        if(!_runtime->activeMotion) {
            return detail::makeArray({});
        }
        return detail::makeArray(detail::stringsToVariants(
            _runtime->activeMotion->mainTimelineLabels));
    }

    tjs_int Player::countDiffTimelines() {
        ensureMotionLoaded();
        return _runtime->activeMotion
            ? static_cast<tjs_int>(_runtime->activeMotion->diffTimelineLabels.size())
            : 0;
    }

    ttstr Player::getDiffTimelineLabelAt(tjs_int idx) {
        ensureMotionLoaded();
        if(!_runtime->activeMotion || idx < 0 ||
           static_cast<size_t>(idx) >=
               _runtime->activeMotion->diffTimelineLabels.size()) {
            return {};
        }
        return detail::widen(_runtime->activeMotion->diffTimelineLabels[idx]);
    }

    tTJSVariant Player::getDiffTimelineLabelList() {
        ensureMotionLoaded();
        if(!_runtime->activeMotion) {
            return detail::makeArray({});
        }
        return detail::makeArray(detail::stringsToVariants(
            _runtime->activeMotion->diffTimelineLabels));
    }

    bool Player::getLoopTimeline(ttstr label) {
        ensureMotionLoaded();
        if(!_runtime->activeMotion) {
            return false;
        }
        const auto key = detail::narrow(label);
        if(const auto it = _runtime->activeMotion->loopTimelines.find(key);
           it != _runtime->activeMotion->loopTimelines.end()) {
            return it->second;
        }
        return false;
    }

    tjs_int Player::countPlayingTimelines() {
        ensureMotionLoaded();
        return static_cast<tjs_int>(_runtime->playingTimelineLabels.size());
    }

    ttstr Player::getPlayingTimelineLabelAt(tjs_int idx) {
        ensureMotionLoaded();
        if(idx >= 0 &&
           static_cast<size_t>(idx) < _runtime->playingTimelineLabels.size()) {
            return detail::widen(_runtime->playingTimelineLabels[idx]);
        }
        return {};
    }

    tjs_int Player::getPlayingTimelineFlagsAt(tjs_int idx) {
        ensureMotionLoaded();
        if(idx >= 0 &&
           static_cast<size_t>(idx) < _runtime->playingTimelineLabels.size()) {
            const auto &label = _runtime->playingTimelineLabels[idx];
            if(const auto it = _runtime->timelines.find(label);
               it != _runtime->timelines.end()) {
                return it->second.flags;
            }
        }
        return 0;
    }

    tjs_int Player::getTimelineTotalFrameCount(ttstr label) {
        ensureMotionLoaded();
        const auto key = detail::narrow(label);
        if(const auto it = _runtime->timelines.find(key);
           it != _runtime->timelines.end()) {
            return static_cast<tjs_int>(it->second.totalFrames);
        }
        if(_runtime->activeMotion) {
            if(const auto it = _runtime->activeMotion->timelineTotalFrames.find(key);
               it != _runtime->activeMotion->timelineTotalFrames.end()) {
                return static_cast<tjs_int>(it->second);
            }
        }
        return 0;
    }

    void Player::playTimeline(ttstr label, tjs_int flags) {
        ensureMotionLoaded();
        if(!_runtime->activeMotion) {
            return;
        }
        if(_runtime->timelines.empty()) {
            detail::primeTimelineStates(_runtime->timelines, *_runtime->activeMotion);
        }

        const auto key = detail::narrow(label);
        auto it = _runtime->timelines.find(key);
        if(it == _runtime->timelines.end()) {
            return;
        }

        // Aligned to libkrkr2.so Player_playTimeline (0x672F70):
        // parallel flag first clears the playing-timeline list.
        if((flags & 1) != 0) {
            stopTimeline(TJS_W(""));
        }

        if(!label.IsEmpty()) {
            if(std::find(_runtime->playingTimelineLabels.begin(),
                         _runtime->playingTimelineLabels.end(),
                         key) == _runtime->playingTimelineLabels.end()) {
                _runtime->playingTimelineLabels.push_back(key);
            }
        }

        it->second.flags = flags;
        it->second.playing = true;
        it->second.currentTime = 0.0;
        it->second.blendRatio = 1.0;
        it->second.blendAnimator = {};
        it->second.blendAutoStop = false;
        it->second.controlInitialized = false;
        it->second.controlLastAppliedTime = 0.0;
        it->second.controlFrameCursor.clear();
        it->second.controlTrackValues.clear();
        it->second.controlTrackAnimators.clear();
        if(const auto controlIt =
               _runtime->activeMotion->timelineControlByLabel.find(key);
           controlIt != _runtime->activeMotion->timelineControlByLabel.end()) {
            resetTimelineControlStateLike_0x671A50(
                it->second, controlIt->second, 0.0);
        }
        _allplaying = !_runtime->playingTimelineLabels.empty();
    }

    void Player::stopTimeline(ttstr label) {
        const auto key = detail::narrow(label);
        if(label.IsEmpty()) {
            for(auto &[_, state] : _runtime->timelines) {
                state.playing = false;
                state.blendRatio = 1.0;
                state.blendAnimator = {};
                state.blendAutoStop = false;
                state.controlInitialized = false;
                state.controlFrameCursor.clear();
                state.controlTrackValues.clear();
                state.controlTrackAnimators.clear();
            }
            _runtime->playingTimelineLabels.clear();
        } else {
            if(const auto it = _runtime->timelines.find(key);
               it != _runtime->timelines.end()) {
                it->second.playing = false;
                it->second.blendRatio = 1.0;
                it->second.blendAnimator = {};
                it->second.blendAutoStop = false;
                it->second.controlInitialized = false;
                it->second.controlFrameCursor.clear();
                it->second.controlTrackValues.clear();
                it->second.controlTrackAnimators.clear();
            }
            if(const auto it = std::remove(_runtime->playingTimelineLabels.begin(),
                                           _runtime->playingTimelineLabels.end(),
                                           key);
               it != _runtime->playingTimelineLabels.end()) {
                _runtime->playingTimelineLabels.erase(
                    it, _runtime->playingTimelineLabels.end());
            }
        }

        _allplaying = !_runtime->playingTimelineLabels.empty();
    }

    void Player::setTimelineBlendRatio(ttstr label, double ratio) {
        ensureMotionLoaded();
        if(_runtime->timelines.empty() && _runtime->activeMotion) {
            detail::primeTimelineStates(_runtime->timelines, *_runtime->activeMotion);
        }

        const auto key = detail::narrow(label);
        auto &state = _runtime->timelines[key];
        state.label = key;
        state.blendRatio = ratio;
        state.blendAnimator = {};
        state.blendAutoStop = false;
    }

    double Player::getTimelineBlendRatio(ttstr label) {
        const auto key = detail::narrow(label);
        if(const auto it = _runtime->timelines.find(key);
           it != _runtime->timelines.end()) {
            return it->second.blendRatio;
        }
        return 1.0;
    }

    void Player::fadeInTimeline(ttstr label, double duration, tjs_int flags) {
        const auto key = detail::narrow(label);
        const bool alreadyPlaying =
            std::find(_runtime->playingTimelineLabels.begin(),
                      _runtime->playingTimelineLabels.end(),
                      key) != _runtime->playingTimelineLabels.end();
        if(!alreadyPlaying) {
            playTimeline(label, 3);
            setTimelineBlendLike_0x6735AC(key, false, 0.0, 0.0, 0.0);
        }
        setTimelineBlendLike_0x6735AC(key, false, 1.0, duration, 0.0);
    }

    void Player::fadeOutTimeline(ttstr label, double duration, tjs_int) {
        setTimelineBlendLike_0x6735AC(detail::narrow(label), true, 0.0,
                                      duration, 0.0);
    }

    tTJSVariant Player::getPlayingTimelineInfoList() {
        ensureMotionLoaded();
        return detail::makeArray(timelineInfoVariants(*_runtime));
    }

    // --- Selector ---
    bool Player::isSelectorTarget(ttstr name) {
        const auto *layers = activeLayersByName();
        if(!layers) {
            return false;
        }
        const auto key = detail::narrow(name);
        return layers->find(key) != layers->end() &&
            _runtime->disabledSelectorTargets.find(key) ==
                _runtime->disabledSelectorTargets.end();
    }

    void Player::deactivateSelectorTarget(ttstr name) {
        _runtime->disabledSelectorTargets[detail::narrow(name)] = true;
    }

    // --- Misc ---
    tTJSVariant Player::getCommandList() {
        if(!_runtime->activeMotion) {
            return detail::makeArray({});
        }
        return detail::makeArray(
            detail::stringsToVariants(activeSourceCandidates()));
    }

    bool Player::getD3DAvailable() { return true; }

    void Player::doAlphaMaskOperation() {}

    // Aligned to libkrkr2.so Player_playImpl (0x6B21E8):
    // Called from sub_6BE0C0 at 0x6BE46C with flags = motionFlags | v12.
    // flags: PlayFlagForce(1)=force reload, PlayFlagStealth(16)=set stealth fields only.
    void Player::onFindMotion(ttstr name, int flags) {
        // PlayFlagStealth (0x10): store as stealth motion, don't load
        // Binary: if ((flags & 0x10) && !player->project) { player->motionKey = name; return; }
        if ((flags & PlayFlagStealth) && _project.Type() == tvtVoid) {
            _stealthMotion = name;
            return;
        }

        // PlayFlagForce (0x01): force reload even if same motion is loaded
        // Binary: Player_setMotionImpl skips reload guard when force flag set
        if ((flags & PlayFlagForce) && _motionKey == name) {
            _motionKey = ttstr();  // clear to bypass same-motion guard in findMotion
        }

        // Load the motion (equivalent to Player_setMotionImpl → loadMotion)
        (void)findMotion(name);

        // After loading, prime timelines and start playback
        // (aligned to Player_setMotionImpl post-load behavior)
        if (_runtime->activeMotion && _runtime->timelines.empty()) {
            detail::primeTimelineStates(_runtime->timelines,
                                        *_runtime->activeMotion);
        }

        // Start all timelines playing (equivalent to playCompat's playOne loop)
        if (_runtime->activeMotion && !_runtime->timelines.empty()) {
            double maxTF = 0.0;
            _runtime->playingTimelineLabels.clear();
            const auto &primary =
                !_runtime->activeMotion->mainTimelineLabels.empty()
                    ? _runtime->activeMotion->mainTimelineLabels
                    : _runtime->activeMotion->diffTimelineLabels;
            for (const auto &timelineLabel : primary) {
                auto &state = _runtime->timelines[timelineLabel];
                state.flags = flags & ~PlayFlagStealth;  // pass flags minus stealth
                state.playing = true;
                state.blendRatio = 1.0;
                state.controlInitialized = false;
                state.controlLastAppliedTime = state.currentTime;
                state.controlFrameCursor.clear();
                state.controlTrackValues.clear();
                state.controlTrackAnimators.clear();
                _runtime->playingTimelineLabels.push_back(timelineLabel);
                if (state.totalFrames > maxTF) maxTF = state.totalFrames;
            }
            _cachedTotalFrames = maxTF;  // player+1128 cached value
            _allplaying = !_runtime->playingTimelineLabels.empty();
        }

        // Handle pending stealth motion (0x6B226C..0x6B2280)
        if (!_stealthMotion.IsEmpty()) {
            _stealthChara = _chara;
            // stealthMotion is consumed — binary nulls it after use
            _stealthMotion = ttstr();
        }
    }

    tjs_error Player::setDrawAffineTranslateMatrixCompat(
        tTJSVariant *result, tjs_int numparams, tTJSVariant **param,
        Player *nativeInstance) {
        if(result) {
            result->Clear();
        }
        if(!nativeInstance) {
            return TJS_E_INVALIDOBJECT;
        }

        std::array<double, 6> matrix{ 1.0, 0.0, 0.0, 1.0, 0.0, 0.0 };
        if(numparams >= 6) {
            for(size_t index = 0; index < matrix.size(); ++index) {
                if(!param[index] || param[index]->Type() == tvtVoid) {
                    return TJS_E_INVALIDPARAM;
                }
                matrix[index] = param[index]->AsReal();
            }
        } else if(numparams == 1 && param[0] && param[0]->Type() == tvtObject &&
                  param[0]->AsObjectNoAddRef() != nullptr) {
            const auto object = *param[0];
            tTJSVariant value;
            if(getObjectProperty(object, TJS_W("m11"), value) &&
               value.Type() != tvtVoid) {
                matrix[0] = value.AsReal();
            }
            if(getObjectProperty(object, TJS_W("m21"), value) &&
               value.Type() != tvtVoid) {
                matrix[1] = value.AsReal();
            }
            if(getObjectProperty(object, TJS_W("m12"), value) &&
               value.Type() != tvtVoid) {
                matrix[2] = value.AsReal();
            }
            if(getObjectProperty(object, TJS_W("m22"), value) &&
               value.Type() != tvtVoid) {
                matrix[3] = value.AsReal();
            }
            if(getObjectProperty(object, TJS_W("m14"), value) &&
               value.Type() != tvtVoid) {
                matrix[4] = value.AsReal();
            }
            if(getObjectProperty(object, TJS_W("m24"), value) &&
               value.Type() != tvtVoid) {
                matrix[5] = value.AsReal();
            }
        } else {
            return TJS_E_BADPARAMCOUNT;
        }

        nativeInstance->_runtime->drawAffineMatrix = matrix;
        const auto motionPath =
            nativeInstance->_runtime && nativeInstance->_runtime->activeMotion
                ? nativeInstance->_runtime->activeMotion->path
                : std::string{};
        const bool isIdentity =
            matrix[0] == 1.0 && matrix[1] == 0.0 && matrix[2] == 0.0 &&
            matrix[3] == 1.0 && matrix[4] == 0.0 && matrix[5] == 0.0;
        detail::logoChainTraceLogf(
            motionPath, "setDrawAffine", "0x6D4F14",
            nativeInstance->_clampedEvalTime,
            "numparams={} matrix=[{:.3f},{:.3f},{:.3f},{:.3f},{:.3f},{:.3f}] nonIdentityFlag={} routeSource={}",
            numparams, matrix[0], matrix[1], matrix[2], matrix[3], matrix[4],
            matrix[5], isIdentity ? 0 : 1,
            (numparams >= 6) ? "six-params"
                             : ((numparams == 1) ? "matrix-object" : "invalid"));
        return TJS_S_OK;
    }

    tjs_error Player::captureCanvasCompat(tTJSVariant *result, tjs_int numparams,
                                          tTJSVariant **param,
                                          Player *nativeInstance) {
        if(result) {
            result->Clear();
        }
        if(!nativeInstance) {
            return TJS_E_INVALIDOBJECT;
        }

        if(numparams > 0 && param[0] && param[0]->Type() == tvtObject &&
           param[0]->AsObjectNoAddRef() != nullptr) {
            if(nativeInstance->renderToLayer(param[0]->AsObjectNoAddRef())) {
                if(result) {
                    *result = *param[0];
                }
                return TJS_S_OK;
            }
        }

        if(result) {
            *result = nativeInstance->captureCanvas();
        }
        return TJS_S_OK;
    }

    // drawCompat — aligned to libkrkr2.so sub_6D5FB8 / Player_drawD3D (0x6D5B90).
    // Logic:
    //   1. param is D3DAdaptor → set _d3dDrawMode and render via D3D path immediately
    //   2. param is SLA → route to SLA target
    //   3. param is Layer → if _d3dDrawMode, render via shared D3DAdaptor+captureCanvas;
    //      else render directly to Layer
    tjs_error Player::drawCompat(tTJSVariant *result, tjs_int numparams,
                                 tTJSVariant **param, iTJSDispatch2 *objthis) {
        if(result) {
            result->Clear();
        }
        auto *nativeInstance = ncbInstanceAdaptor<Player>::GetNativeInstance(objthis, true);
        if(!nativeInstance) {
            return TJS_E_INVALIDOBJECT;
        }

        const auto motionPath =
            nativeInstance->_runtime && nativeInstance->_runtime->activeMotion
                ? nativeInstance->_runtime->activeMotion->path
                : std::string{};
        tTJSVariant *arg = (numparams > 0 && param) ? param[0] : nullptr;
        iTJSDispatch2 *paramObj =
            (arg && arg->Type() == tvtObject) ? arg->AsObjectNoAddRef() : nullptr;

        if(!paramObj) {
            detail::logoChainTraceLogf(
                motionPath, "drawCompat.dispatch", "0x6D5FB8",
                nativeInstance->_clampedEvalTime,
                "route=no-param drawAffine=[{:.3f},{:.3f},{:.3f},{:.3f},{:.3f},{:.3f}] cameraOffset=({:.3f},{:.3f})",
                nativeInstance->_runtime->drawAffineMatrix[0],
                nativeInstance->_runtime->drawAffineMatrix[1],
                nativeInstance->_runtime->drawAffineMatrix[2],
                nativeInstance->_runtime->drawAffineMatrix[3],
                nativeInstance->_runtime->drawAffineMatrix[4],
                nativeInstance->_runtime->drawAffineMatrix[5],
                nativeInstance->_cameraOffsetX, nativeInstance->_cameraOffsetY);
            if(result) {
                *result = nativeInstance->_runtime->lastCanvas;
            }
            return TJS_S_OK;
        }

        // Step 1: Check if param is D3DAdaptor (libkrkr2.so checks NIS with
        // D3DAdaptor classID). If so, set _d3dDrawMode and render immediately.
        {
            auto *d3dAdaptor =
                ncbInstanceAdaptor<D3DAdaptor>::GetNativeInstance(paramObj, false);
            if(d3dAdaptor) {
                detail::logoChainTraceCheck(
                    motionPath, "drawCompat.dispatch", "0x6D5FB8",
                    nativeInstance->_clampedEvalTime,
                    "D3DAdaptor -> Player_drawD3D",
                    "D3DAdaptor -> Player_drawD3D", true,
                    "drawCompat D3D routing mismatch");
                detail::logoChainTraceLogf(
                    motionPath, "drawCompat.matrix", "0x6D5FB8",
                    nativeInstance->_clampedEvalTime,
                    "route=d3d drawAffine=[{:.3f},{:.3f},{:.3f},{:.3f},{:.3f},{:.3f}] cameraOffset=({:.3f},{:.3f}) sampleExpectedYuzu=[1,0,0,1,960,540]",
                    nativeInstance->_runtime->drawAffineMatrix[0],
                    nativeInstance->_runtime->drawAffineMatrix[1],
                    nativeInstance->_runtime->drawAffineMatrix[2],
                    nativeInstance->_runtime->drawAffineMatrix[3],
                    nativeInstance->_runtime->drawAffineMatrix[4],
                    nativeInstance->_runtime->drawAffineMatrix[5],
                    nativeInstance->_cameraOffsetX, nativeInstance->_cameraOffsetY);
                nativeInstance->_d3dDrawMode = true;
                nativeInstance->renderToD3DAdaptor(d3dAdaptor);
                if(result && arg) *result = *arg;
                return TJS_S_OK;
            }
        }

        // Step 2: Check if param is SLA.
        // Aligned to libkrkr2.so Player_drawCompat (0x6D5FB8):
        // the native code only checks the SeparateLayerAdaptor class ID here.
        // It does not route plain Layer objects through the SLA backend just
        // because they resolve to an owner/target layer.
        {
            auto *sla =
                ncbInstanceAdaptor<SeparateLayerAdaptor>::GetNativeInstance(
                    paramObj, false);
            if(sla) {
                detail::logoChainTraceCheck(
                    motionPath, "drawCompat.dispatch", "0x6D5FB8",
                    nativeInstance->_clampedEvalTime,
                    "SeparateLayerAdaptor -> Player_DrawSLA",
                    "SeparateLayerAdaptor -> Player_DrawSLA", true,
                    "drawCompat SLA routing mismatch");
                detail::logoChainTraceLogf(
                    motionPath, "drawCompat.matrix", "0x6D5FB8",
                    nativeInstance->_clampedEvalTime,
                    "route=sla drawAffine=[{:.3f},{:.3f},{:.3f},{:.3f},{:.3f},{:.3f}] cameraOffset=({:.3f},{:.3f}) sampleExpectedYuzu=[1,0,0,1,960,540]",
                    nativeInstance->_runtime->drawAffineMatrix[0],
                    nativeInstance->_runtime->drawAffineMatrix[1],
                    nativeInstance->_runtime->drawAffineMatrix[2],
                    nativeInstance->_runtime->drawAffineMatrix[3],
                    nativeInstance->_runtime->drawAffineMatrix[4],
                    nativeInstance->_runtime->drawAffineMatrix[5],
                    nativeInstance->_cameraOffsetX, nativeInstance->_cameraOffsetY);
                nativeInstance->renderToSeparateLayerAdaptor(paramObj);
                if(result && arg) {
                    *result = *arg;
                }
                return TJS_S_OK;
            }
        }

        // Step 3: param is a Layer (or resolves to one)
        tTJSNI_BaseLayer *layer = nullptr;
        if(tryGetLayerObject(*arg, layer)) {
            detail::logoChainTraceCheck(
                motionPath, "drawCompat.dispatch", "0x6D5FB8",
                nativeInstance->_clampedEvalTime,
                "Layer -> renderToLayer/renderViaSharedD3DAdaptor",
                nativeInstance->_d3dDrawMode
                    ? "Layer -> renderViaSharedD3DAdaptor"
                    : "Layer -> renderToLayer",
                true, "drawCompat Layer routing mismatch");
            detail::logoChainTraceLogf(
                motionPath, "drawCompat.matrix", "0x6D5FB8",
                nativeInstance->_clampedEvalTime,
                "route={} drawAffine=[{:.3f},{:.3f},{:.3f},{:.3f},{:.3f},{:.3f}] cameraOffset=({:.3f},{:.3f}) sampleExpectedYuzu=[1,0,0,1,960,540]",
                nativeInstance->_d3dDrawMode ? "layer-via-d3d" : "layer",
                nativeInstance->_runtime->drawAffineMatrix[0],
                nativeInstance->_runtime->drawAffineMatrix[1],
                nativeInstance->_runtime->drawAffineMatrix[2],
                nativeInstance->_runtime->drawAffineMatrix[3],
                nativeInstance->_runtime->drawAffineMatrix[4],
                nativeInstance->_runtime->drawAffineMatrix[5],
                nativeInstance->_cameraOffsetX, nativeInstance->_cameraOffsetY);
            if(nativeInstance->_d3dDrawMode) {
                nativeInstance->renderViaSharedD3DAdaptor(paramObj);
            } else {
                nativeInstance->renderToLayer(paramObj);
            }
            if(result) *result = *arg;
            return TJS_S_OK;
        }

        // Step 4: param resolves to a Layer via property chain
        {
            iTJSDispatch2 *resolved = tryResolveSeparateAdaptorOwner(*arg);
            if(resolved) {
                detail::logoChainTraceCheck(
                    motionPath, "drawCompat.dispatch", "0x6D5FB8",
                    nativeInstance->_clampedEvalTime,
                    "Resolved owner Layer -> renderToLayer/renderViaSharedD3DAdaptor",
                    nativeInstance->_d3dDrawMode
                        ? "Resolved owner Layer -> renderViaSharedD3DAdaptor"
                        : "Resolved owner Layer -> renderToLayer",
                    true, "drawCompat owner-layer routing mismatch");
                if(nativeInstance->_d3dDrawMode) {
                    nativeInstance->renderViaSharedD3DAdaptor(resolved);
                } else {
                    nativeInstance->renderToLayer(resolved);
                }
                if(result) *result = tTJSVariant(resolved, resolved);
                return TJS_S_OK;
            }
        }

        // Fallback: no SLA/Layer match
        detail::logoChainTraceCheck(
            motionPath, "drawCompat.dispatch", "0x6D5FB8",
            nativeInstance->_clampedEvalTime,
            "D3DAdaptor | SeparateLayerAdaptor | Layer",
            "unresolved target", false,
            "drawCompat could not classify the target object");
        if(result) {
            *result = nativeInstance->_runtime->lastCanvas;
        }
        return TJS_S_OK;
    }

    tjs_error Player::playCompat(tTJSVariant *result, tjs_int numparams,
                                 tTJSVariant **param, iTJSDispatch2 *objthis) {
        if(result) {
            result->Clear();
        }

        auto *self = ncbInstanceAdaptor<Player>::GetNativeInstance(objthis, true);
        if(!self) {
            return TJS_E_INVALIDOBJECT;
        }

        ttstr label;
        tjs_int flags = 0;
        if(numparams > 0 && param[0] && param[0]->Type() != tvtVoid) {
            if(param[0]->Type() == tvtInteger || param[0]->Type() == tvtReal) {
                flags = param[0]->AsInteger();
            } else {
                label = *param[0];
            }
        }
        if(numparams > 1 && param[1] && param[1]->Type() != tvtVoid) {
            flags = param[1]->AsInteger();
        }

        if(!self->_runtime->activeMotion && self->_project.Type() == tvtObject) {
            if(const auto snapshot = detail::lookupModuleSnapshot(self->_project)) {
                activateMotion(*self->_runtime, snapshot);
                self->syncVariableKeysFromActiveMotion();
            }
        }

        self->ensureMotionLoaded();
        if(self->_runtime->activeMotion && self->_runtime->timelines.empty()) {
            detail::primeTimelineStates(self->_runtime->timelines,
                                        *self->_runtime->activeMotion);
        }

        if(!label.IsEmpty() && !self->_runtime->activeMotion) {
            self->setMotion(label);
            self->ensureMotionLoaded();
            if(self->_runtime->activeMotion && self->_runtime->timelines.empty()) {
                detail::primeTimelineStates(self->_runtime->timelines,
                                            *self->_runtime->activeMotion);
            }
        }

        if(!self->_runtime->activeMotion) {
            if(result) {
                *result = tTJSVariant(false);
            }
            return TJS_S_OK;
        }

        if((flags & PlayFlagForce) != 0) {
            self->stopTimeline(TJS_W(""));
        }

        const auto playOne = [&](const std::string &timelineLabel) {
            auto &state = self->_runtime->timelines[timelineLabel];
            state.label = timelineLabel;
            state.flags = flags;
            state.blendRatio = 1.0;
            state.playing = true;
            state.currentTime = 0.0;
            state.controlInitialized = false;
            state.controlLastAppliedTime = 0.0;
            state.controlFrameCursor.clear();
            state.controlTrackValues.clear();
            state.controlTrackAnimators.clear();
            if(std::find(self->_runtime->playingTimelineLabels.begin(),
                         self->_runtime->playingTimelineLabels.end(),
                         timelineLabel) ==
               self->_runtime->playingTimelineLabels.end()) {
                self->_runtime->playingTimelineLabels.push_back(timelineLabel);
            }
            // Ensure totalFrames is set (may be 0 if timeline wasn't primed)
            if(state.totalFrames <= 0.0 && self->_runtime->activeMotion) {
                auto it = self->_runtime->activeMotion->timelineTotalFrames.find(timelineLabel);
                if(it != self->_runtime->activeMotion->timelineTotalFrames.end()) {
                    state.totalFrames = it->second;
                }
            }
        };

        bool started = false;
        if(!label.IsEmpty()) {
            const auto key = detail::narrow(label);
            if(self->_runtime->timelines.find(key) != self->_runtime->timelines.end()) {
                playOne(key);
                started = true;
            }
        }

        if(!started) {
            const auto &primary = !self->_runtime->activeMotion->mainTimelineLabels.empty()
                ? self->_runtime->activeMotion->mainTimelineLabels
                : self->_runtime->activeMotion->diffTimelineLabels;
            for(const auto &timelineLabel : primary) {
                playOne(timelineLabel);
                started = true;
            }
        }

        self->_allplaying = !self->_runtime->playingTimelineLabels.empty();

        if(result) {
            *result = tTJSVariant(started);
        }
        return TJS_S_OK;
    }

    tjs_error Player::progressCompatMethod(tTJSVariant *result, tjs_int numparams,
                                           tTJSVariant **param,
                                           iTJSDispatch2 *objthis) {
        auto *self = ncbInstanceAdaptor<Player>::GetNativeInstance(objthis, true);
        if(!self) {
            return TJS_E_INVALIDOBJECT;
        }

        self->ensureMotionLoaded();

        double delta = 0.0;
        if(numparams > 0 && param[0] && param[0]->Type() != tvtVoid) {
            delta = param[0]->AsReal();
        }
        // Clamp delta to sane range: TJS tick differences can overflow
        // when uint32 wraps (e.g. 4294967381 = 2^32 + 85)
        if(delta < 0 || delta > 60000) {
            delta = 0;
        }

        self->_runtime->pendingEvents.clear();
        self->frameProgress(delta * kMotionFramesPerMillisecond);
        const auto motionPath =
            self->_runtime && self->_runtime->activeMotion
                ? self->_runtime->activeMotion->path
                : std::string{};
        detail::logoChainTraceCheck(
            motionPath, "progressCompat.dt", "0x6D2A98",
            self->_clampedEvalTime,
            fmt::format("dt_ms*60/1000={:.6f}", delta * kMotionFramesPerMillisecond),
            fmt::format("dt_frames={:.6f}", self->_frameLastTime),
            std::fabs(self->_frameLastTime - delta * kMotionFramesPerMillisecond) <
                0.000001,
            "progressCompat dt(ms)->frame conversion diverged from 0x6D2A98");

        // Aligned to libkrkr2.so Player_progressCompat (0x6D2A98):
        // progress_inner -> updateLayers -> calcBounds -> dispatchEvents.
        self->ensureNodeTreeBuilt();
        if(!self->_runtime->nodes.empty()) {
            detail::logoChainTraceLogf(
                motionPath, "progressCompat.update", "0x6D2A98",
                self->_clampedEvalTime,
                "timelineCurrentTime={:.3f} pendingEvents={} nodes={}",
                self->_clampedEvalTime, self->_runtime->pendingEvents.size(),
                self->_runtime->nodes.size());
            self->updateLayers();
        }
        self->calcBounds();

        if(detail::logoSnapshotMarkEnabledForPath(motionPath) &&
           motionPath.find("m2logo.mtn") != std::string::npos &&
           self->_clampedEvalTime >= 30.0 && self->_clampedEvalTime <= 40.0) {
            std::fprintf(stderr, "SHOTMARK motion=%s frame=%.3f\n",
                         motionPath.c_str(), self->_clampedEvalTime);
        }

        // Aligned to libkrkr2.so Player_dispatchEvents (0x6C4490):
        // After stepping timelines, dispatch queued onAction/onSync events.
        if(!self->_runtime->pendingEvents.empty()) {
            for(const auto &ev : self->_runtime->pendingEvents) {
                try {
                    if(ev.type == 0) {
                        // onAction(param1, param2)
                        tTJSVariant p1(detail::widen(ev.param1));
                        tTJSVariant p2(detail::widen(ev.param2));
                        tTJSVariant *args[] = { &p1, &p2 };
                        objthis->FuncCall(0, TJS_W("onAction"),
                            nullptr, nullptr, 2, args, objthis);
                    } else if(ev.type == 1) {
                        // onSync()
                        objthis->FuncCall(0, TJS_W("onSync"),
                            nullptr, nullptr, 0, nullptr, objthis);
                    }
                } catch(...) {}
            }
            self->_runtime->pendingEvents.clear();
        }

        if(result) {
            *result = tTJSVariant(self->getProgressCompat());
        }
        return TJS_S_OK;
    }

    tjs_error Player::setVariableCompatMethod(tTJSVariant *, tjs_int numparams,
                                              tTJSVariant **param,
                                              iTJSDispatch2 *objthis) {
        auto *self = ncbInstanceAdaptor<Player>::GetNativeInstance(objthis, true);
        if(!self) {
            return TJS_E_INVALIDOBJECT;
        }
        if(numparams < 2 || !param[0] || !param[1]) {
            return TJS_E_INVALIDPARAM;
        }

        const double transition =
            (numparams >= 3 && param[2]) ? param[2]->AsReal() : 0.0;
        const double ease =
            (numparams >= 4 && param[3]) ? param[3]->AsReal() : 0.0;
        self->setVariable(ttstr(*param[0]), param[1]->AsReal(), transition,
                          ease);
        return TJS_S_OK;
    }

    tjs_error Player::isPlayingCompat(tTJSVariant *result, tjs_int,
                                      tTJSVariant **, iTJSDispatch2 *objthis) {
        auto *self = ncbInstanceAdaptor<Player>::GetNativeInstance(objthis, true);
        if(!self) {
            return TJS_E_INVALIDOBJECT;
        }

        const bool playing = !self->_runtime->playingTimelineLabels.empty();
        self->_allplaying = playing;
        if(result) {
            *result = tTJSVariant(playing);
        }
        return TJS_S_OK;
    }

    tjs_error Player::stopCompat(tTJSVariant *result, tjs_int numparams,
                                 tTJSVariant **param, iTJSDispatch2 *objthis) {
        auto *self = ncbInstanceAdaptor<Player>::GetNativeInstance(objthis, true);
        if(!self) {
            return TJS_E_INVALIDOBJECT;
        }

        // Aligned to libkrkr2.so Player_stop (0x6D9A30):
        // Binary simply clears the Player-level playing flag (player+1099).
        // Timeline state is left intact; TJS polls `playing` for edge-triggered
        // stop detection and may still inspect the final motion pose afterward.
        self->_allplaying = false;

        if(result) {
            *result = tTJSVariant(true);
        }
        return TJS_S_OK;
    }

    tTJSVariant Player::motionList() {
        std::vector<std::string> paths;
        std::unordered_set<std::string> seen;
        for(const auto &[_, snapshot] : _runtime->motionsByKey) {
            if(snapshot && seen.insert(snapshot->path).second) {
                paths.push_back(snapshot->path);
            }
        }
        return detail::makeArray(detail::stringsToVariants(paths));
    }

    void Player::emoteEdit(tTJSVariant args) {
        _directEdit = true;
        _tags = args;
    }

} // namespace motion
