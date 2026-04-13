//
// Created by LiDon on 2025/9/15.
// Aligned to libkrkr2.so D3DEmotePlayer architecture:
// EmotePlayer is a thin shell delegating all animation logic to an owned Player.
// Binary: D3DEmotePlayerNativeInstance(24b) → EmoteObject(40b) → Player(1496b)
//

#include <algorithm>

#include "EmotePlayer.h"
#include "RuntimeSupport.h"
#include "ncbind.hpp"

#define LOGGER spdlog::get("plugin")
#define STUB_WARN(name) LOGGER->warn("EmotePlayer::" #name "() stub called")

namespace motion {

    EmotePlayer::EmotePlayer(ResourceManager rm) :
        _player(std::move(rm)) {}

    EmotePlayer::~EmotePlayer() = default;

    // --- Properties ---

    void EmotePlayer::setVisible(bool v) {
        _visible = v;
        _player.setVisible(v);
    }

    void EmotePlayer::setMeshDivisionRatio(double v) {
        _meshDivisionRatio = v;
        _player.setEmoteMeshDivisionRatio(v);
    }

    bool EmotePlayer::getAnimating() const {
        return _player.getAllplaying();
    }

    void EmotePlayer::setModule(tTJSVariant v) {
        _module = v;
        // Bridge loaded PSB snapshot into Player's animation pipeline.
        // Aligned to libkrkr2.so EmoteObject_init (sub_67DBAC):
        // After loading PSBs, the EmoteObject initializes its internal Player
        // with the loaded motion data.
        auto snapshot = detail::lookupModuleSnapshot(_module);
        if(snapshot) {
            _player.loadFromSnapshot(snapshot);
        }
    }

    tTJSVariant EmotePlayer::getModule() const { return _module; }

    // --- Methods ---

    // Aligned to libkrkr2.so sub_52FD84: create() is actually "destroy/reset"
    void EmotePlayer::create() {
        _module.Clear();
        _player.loadFromSnapshot(nullptr);
        _modified = true;
    }

    void EmotePlayer::load(tTJSVariant data) {
        _module = data;
        auto snapshot = detail::lookupModuleSnapshot(_module);
        if(snapshot) {
            _player.loadFromSnapshot(snapshot);
        }
        _modified = true;
    }

    tTJSVariant EmotePlayer::clone() {
        typedef ncbInstanceAdaptor<EmotePlayer> AdaptorT;

        auto *copy = new EmotePlayer(ResourceManager{});
        // Copy EmotePlayer-specific state
        copy->_module = _module;
        copy->_useD3D = _useD3D;
        copy->_smoothing = _smoothing;
        copy->_meshDivisionRatio = _meshDivisionRatio;
        copy->_queuing = _queuing;
        copy->_hairScale = _hairScale;
        copy->_partsScale = _partsScale;
        copy->_bustScale = _bustScale;
        copy->_bodyScale = _bodyScale;
        copy->_progress = _progress;
        copy->_modified = _modified;
        copy->_drawVisible = _drawVisible;
        copy->_drawOpacity = _drawOpacity;
        copy->_opengl = _opengl;
        copy->_visible = _visible;
        copy->_playCallback = _playCallback;
        copy->_baseScale = _baseScale;
        copy->_userScale = _userScale;
        copy->_rot = _rot;
        copy->_coordX = _coordX;
        copy->_coordY = _coordY;
        copy->_mirrorBase = _mirrorBase;
        copy->_mirrorRequested = _mirrorRequested;
        copy->_mirrorChanged = _mirrorChanged;
        copy->_color = _color;

        // Load the same snapshot into the cloned Player
        auto snapshot = detail::lookupModuleSnapshot(_module);
        if(snapshot) {
            copy->_player.loadFromSnapshot(snapshot);
        }

        tTJSVariant result;
        if(iTJSDispatch2 *adaptor = AdaptorT::CreateAdaptor(copy)) {
            result = tTJSVariant(adaptor, adaptor);
            adaptor->Release();
        } else {
            delete copy;
        }
        return result;
    }

    void EmotePlayer::show() {
        _visible = true;
        _player.setVisible(true);
    }

    void EmotePlayer::hide() {
        _visible = false;
        _player.setVisible(false);
    }

    void EmotePlayer::assignState() { STUB_WARN(assignState); }
    void EmotePlayer::initPhysics() { STUB_WARN(initPhysics); }

    // Aligned to libkrkr2.so sub_5302E4: delegates to Player's rotAnimator
    void EmotePlayer::setRot(double rot, double transition, double ease) {
        _rot = rot;
        _player.setRotate(rot, transition, ease);
        _modified = true;
    }

    tjs_error EmotePlayer::setRotCompat(tTJSVariant *, tjs_int numparams,
                                        tTJSVariant **param,
                                        iTJSDispatch2 *objthis) {
        auto *self =
            ncbInstanceAdaptor<EmotePlayer>::GetNativeInstance(objthis, true);
        if(!self) {
            return TJS_E_INVALIDOBJECT;
        }
        if(numparams < 1 || !param[0]) {
            return TJS_E_INVALIDPARAM;
        }

        const double transition =
            (numparams >= 2 && param[1]) ? param[1]->AsReal() : 0.0;
        const double ease =
            (numparams >= 3 && param[2]) ? param[2]->AsReal() : 0.0;
        self->setRot(param[0]->AsReal(), transition, ease);
        return TJS_S_OK;
    }

    // Aligned to libkrkr2.so sub_53030C: binary returns hardcoded 0.0
    double EmotePlayer::getRot() { return 0.0; }

    // Aligned to libkrkr2.so sub_5301EC: delegates to Player's coordAnimator
    void EmotePlayer::setCoord(double x, double y, double transition,
                               double ease) {
        _coordX = x;
        _coordY = y;
        _player.setEmoteCoord(x, y, transition, ease);
        _modified = true;
    }

    tjs_error EmotePlayer::setCoordCompat(tTJSVariant *, tjs_int numparams,
                                          tTJSVariant **param,
                                          iTJSDispatch2 *objthis) {
        auto *self =
            ncbInstanceAdaptor<EmotePlayer>::GetNativeInstance(objthis, true);
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
        self->setCoord(param[0]->AsReal(), param[1]->AsReal(),
                       transition, ease);
        return TJS_S_OK;
    }

    // Aligned to libkrkr2.so sub_530260: finalScale = baseScale * userScale
    void EmotePlayer::setScale(double s, double transition, double ease) {
        _userScale = static_cast<float>(s);
        const double finalScale =
            static_cast<double>(_baseScale) * static_cast<double>(_userScale);
        _player.setEmoteScale(finalScale, transition, ease);
        _modified = true;
    }

    tjs_error EmotePlayer::setScaleCompat(tTJSVariant *, tjs_int numparams,
                                          tTJSVariant **param,
                                          iTJSDispatch2 *objthis) {
        auto *self =
            ncbInstanceAdaptor<EmotePlayer>::GetNativeInstance(objthis, true);
        if(!self) {
            return TJS_E_INVALIDOBJECT;
        }
        if(numparams < 1 || !param[0]) {
            return TJS_E_INVALIDPARAM;
        }

        const double transition =
            (numparams >= 2 && param[1]) ? param[1]->AsReal() : 0.0;
        const double ease =
            (numparams >= 3 && param[2]) ? param[2]->AsReal() : 0.0;
        self->setScale(param[0]->AsReal(), transition, ease);
        return TJS_S_OK;
    }

    // Aligned to libkrkr2.so sub_5302DC: binary returns hardcoded 1.0
    double EmotePlayer::getScale() { return 1.0; }

    void EmotePlayer::setMirror(bool mirror) {
        // Aligned to libkrkr2.so sub_671DB0:
        // wrapper stores requested mirror, derives a root-flip delta against a
        // baseline bit, forwards that effective flip to Player_setRootFlipX,
        // then triggers the large controller reset path.
        _mirrorRequested = mirror;
        _mirrorChanged = (_mirrorRequested != _mirrorBase);
        _player.setMirror(_mirrorChanged);
        _modified = true;
    }

    void EmotePlayer::setColor(tjs_int color, double transition, double ease) {
        _color = color;
        _player.setEmoteColor(static_cast<tjs_uint32>(color), transition, ease);
        _modified = true;
    }

    tjs_error EmotePlayer::setColorCompat(tTJSVariant *, tjs_int numparams,
                                          tTJSVariant **param,
                                          iTJSDispatch2 *objthis) {
        auto *self =
            ncbInstanceAdaptor<EmotePlayer>::GetNativeInstance(objthis, true);
        if(!self) {
            return TJS_E_INVALIDOBJECT;
        }
        if(numparams < 1 || !param[0]) {
            return TJS_E_INVALIDPARAM;
        }

        const double transition =
            (numparams >= 2 && param[1]) ? param[1]->AsReal() : 0.0;
        const double ease =
            (numparams >= 3 && param[2]) ? param[2]->AsReal() : 0.0;
        self->setColor(param[0]->AsInteger(), transition, ease);
        return TJS_S_OK;
    }
    // Aligned to libkrkr2.so sub_530320: binary returns hardcoded 0
    tjs_int EmotePlayer::getColor() { return 0; }

    // --- Variable system: delegates to Player ---
    // Aligned to libkrkr2.so sub_5305C8 → sub_671228:
    // wrapper forwards label/value/transition/ease into Player_setVariable.
    void EmotePlayer::setVariable(ttstr label, double value, double transition,
                                  double ease) {
        _player.setVariable(label, value, transition, ease);
        _modified = true;
    }

    tjs_error EmotePlayer::setVariableCompat(tTJSVariant *, tjs_int numparams,
                                             tTJSVariant **param,
                                             iTJSDispatch2 *objthis) {
        auto *self =
            ncbInstanceAdaptor<EmotePlayer>::GetNativeInstance(objthis, true);
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

    double EmotePlayer::getVariable(ttstr label) {
        return _player.getVariable(label);
    }

    tjs_int EmotePlayer::countVariables() {
        return _player.countVariables();
    }

    ttstr EmotePlayer::getVariableLabelAt(tjs_int idx) {
        return _player.getVariableLabelAt(idx);
    }

    tjs_int EmotePlayer::countVariableFrameAt(tjs_int idx) {
        return _player.countVariableFrameAt(idx);
    }

    ttstr EmotePlayer::getVariableFrameLabelAt(tjs_int idx, tjs_int frameIdx) {
        return _player.getVariableFrameLabelAt(idx, frameIdx);
    }

    double EmotePlayer::getVariableFrameValueAt(tjs_int idx, tjs_int frameIdx) {
        return _player.getVariableFrameValueAt(idx, frameIdx);
    }

    // --- Wind/Force ---
    void EmotePlayer::startWind(double minAngle, double maxAngle,
                                double amplitude, double freqX,
                                double freqY) {
        _player.startWind(minAngle, maxAngle, amplitude, freqX, freqY);
        _modified = true;
    }

    tjs_error EmotePlayer::startWindCompat(tTJSVariant *, tjs_int numparams,
                                           tTJSVariant **param,
                                           iTJSDispatch2 *objthis) {
        auto *self =
            ncbInstanceAdaptor<EmotePlayer>::GetNativeInstance(objthis, true);
        if(!self) {
            return TJS_E_INVALIDOBJECT;
        }
        if(numparams < 5 || !param[0] || !param[1] || !param[2] ||
           !param[3] || !param[4]) {
            return TJS_E_INVALIDPARAM;
        }

        self->startWind(param[0]->AsReal(), param[1]->AsReal(),
                        param[2]->AsReal(), param[3]->AsReal(),
                        param[4]->AsReal());
        return TJS_S_OK;
    }

    void EmotePlayer::stopWind() {
        _player.stopWind();
        _modified = true;
    }

    tjs_error EmotePlayer::stopWindCompat(tTJSVariant *, tjs_int,
                                          tTJSVariant **,
                                          iTJSDispatch2 *objthis) {
        auto *self =
            ncbInstanceAdaptor<EmotePlayer>::GetNativeInstance(objthis, true);
        if(!self) {
            return TJS_E_INVALIDOBJECT;
        }
        self->stopWind();
        return TJS_S_OK;
    }

    // --- Timeline methods: delegate to Player ---

    tjs_int EmotePlayer::countMainTimelines() {
        return _player.countMainTimelines();
    }

    ttstr EmotePlayer::getMainTimelineLabelAt(tjs_int idx) {
        return _player.getMainTimelineLabelAt(idx);
    }

    tjs_int EmotePlayer::countDiffTimelines() {
        return _player.countDiffTimelines();
    }

    ttstr EmotePlayer::getDiffTimelineLabelAt(tjs_int idx) {
        return _player.getDiffTimelineLabelAt(idx);
    }

    tjs_int EmotePlayer::countPlayingTimelines() {
        return _player.countPlayingTimelines();
    }

    ttstr EmotePlayer::getPlayingTimelineLabelAt(tjs_int idx) {
        return _player.getPlayingTimelineLabelAt(idx);
    }

    tjs_int EmotePlayer::getPlayingTimelineFlagsAt(tjs_int idx) {
        return _player.getPlayingTimelineFlagsAt(idx);
    }

    bool EmotePlayer::isLoopTimeline(ttstr label) {
        return _player.getLoopTimeline(label);
    }

    tjs_int EmotePlayer::getTimelineTotalFrameCount(ttstr label) {
        return _player.getTimelineTotalFrameCount(label);
    }

    void EmotePlayer::playTimeline(ttstr label, tjs_int flags) {
        _player.playTimeline(label, flags);
        _modified = true;
    }

    bool EmotePlayer::isTimelinePlaying(ttstr label) {
        return _player.getTimelinePlaying(label);
    }

    void EmotePlayer::stopTimeline(ttstr label) {
        _player.stopTimeline(label);
    }

    void EmotePlayer::setTimelineBlendRatio(ttstr label, double ratio) {
        _player.setTimelineBlendRatio(label, ratio);
    }

    double EmotePlayer::getTimelineBlendRatio(ttstr label) {
        return _player.getTimelineBlendRatio(label);
    }

    void EmotePlayer::fadeInTimeline(ttstr label, double duration,
                                     tjs_int flags) {
        _player.fadeInTimeline(label, duration, flags);
    }

    void EmotePlayer::fadeOutTimeline(ttstr label, double duration,
                                      tjs_int flags) {
        _player.fadeOutTimeline(label, duration, flags);
    }

    void EmotePlayer::setTimeline(ttstr label, bool loop) {
        // Player doesn't have an exact equivalent; use playTimeline + loop flag
        _player.playTimeline(label, 0);
    }

    void EmotePlayer::addPlayCallback() {
        _playCallback = true;
    }

    void EmotePlayer::skip() {
        // Aligned to libkrkr2.so sub_66EB8C: skip to end of all timelines
        // Player doesn't expose skip() directly, stop all timelines
        _player.stopTimeline(TJS_W(""));
    }

    // Aligned to libkrkr2.so sub_530A5C → sub_67D01C:
    // Binary progress() delegates to Player's full physics/animation engine.
    void EmotePlayer::pass(double dt) {
        _progress += dt;
        double remaining = dt;
        while(remaining > 0.0) {
            const double step = std::min(remaining, 1.1);
            _player.frameProgress(step);
            remaining -= step;
        }
        _modified = true;
    }

    void EmotePlayer::progress(double dt) {
        pass(dt);
    }

    // Aligned to libkrkr2.so sub_672D58: routes by label to bust/h/parts
    void EmotePlayer::setOuterForce(double x, double y) {
        setOuterForce(TJS_W("bust"), x, y, 0.0, 0.0);
    }

    void EmotePlayer::setOuterForce(ttstr label, double x, double y,
                                    double transition, double ease) {
        _player.setOuterForce(label, x, y, transition, ease);
        _modified = true;
    }

    tjs_error EmotePlayer::setOuterForceCompat(tTJSVariant *, tjs_int numparams,
                                               tTJSVariant **param,
                                               iTJSDispatch2 *objthis) {
        auto *self =
            ncbInstanceAdaptor<EmotePlayer>::GetNativeInstance(objthis, true);
        if(!self) {
            return TJS_E_INVALIDOBJECT;
        }
        if(numparams < 3 || !param[0] || !param[1] || !param[2]) {
            return TJS_E_INVALIDPARAM;
        }

        const ttstr label(*param[0]);
        const double transition =
            (numparams >= 4 && param[3]) ? param[3]->AsReal() : 0.0;
        const double ease =
            (numparams >= 5 && param[4]) ? param[4]->AsReal() : 0.0;
        self->setOuterForce(label, param[1]->AsReal(), param[2]->AsReal(),
                            transition, ease);
        return TJS_S_OK;
    }

    tTJSVariant EmotePlayer::getOuterForce() {
        STUB_WARN(getOuterForce);
        return tTJSVariant();
    }

    bool EmotePlayer::contains(double x, double y) {
        if(!_visible) {
            return false;
        }

        // Use local coordinate state for AABB test.
        // Aligned to libkrkr2.so sub_690DF0: supports circle/rect/quad;
        // we use AABB approximation for now.
        const double scale = static_cast<double>(_baseScale * _userScale);
        const double width = _player.getActiveMotionWidth();
        const double height = _player.getActiveMotionHeight();
        if(width <= 0.0 || height <= 0.0) {
            return false;
        }

        const auto scaledWidth = width * scale;
        const auto scaledHeight = height * scale;
        return x >= _coordX && x <= (_coordX + scaledWidth) &&
               y >= _coordY && y <= (_coordY + scaledHeight);
    }

    bool EmotePlayer::contains(ttstr label, double x, double y) {
        if(!_visible || label.IsEmpty()) {
            return false;
        }
        return _player.hitTestLayer(label, x, y);
    }

    tjs_error EmotePlayer::containsCompat(tTJSVariant *result, tjs_int numparams,
                                          tTJSVariant **param,
                                          iTJSDispatch2 *objthis) {
        auto *self =
            ncbInstanceAdaptor<EmotePlayer>::GetNativeInstance(objthis, true);
        if(!self) {
            return TJS_E_INVALIDOBJECT;
        }
        if(!result) {
            return TJS_E_INVALIDPARAM;
        }

        if(numparams >= 3 && param[0] && param[1] && param[2]) {
            *result = tTJSVariant(
                self->contains(ttstr(*param[0]),
                               param[1]->AsReal(),
                               param[2]->AsReal()));
            return TJS_S_OK;
        }
        if(numparams >= 2 && param[0] && param[1]) {
            *result = tTJSVariant(
                self->contains(param[0]->AsReal(), param[1]->AsReal()));
            return TJS_S_OK;
        }
        return TJS_E_INVALIDPARAM;
    }

} // namespace motion
