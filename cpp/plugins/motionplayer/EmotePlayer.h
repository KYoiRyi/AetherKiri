//
// Created by LiDon on 2025/9/15.
// Aligned to libkrkr2.so D3DEmotePlayer architecture:
// D3DEmotePlayerNativeInstance(24b) → EmoteObject(40b) → Player(1496b)
// EmotePlayer is a thin shell that delegates all animation logic to an owned Player.
//
#pragma once

#include <memory>
#include <string>
#include <spdlog/spdlog.h>
#include "tjs.h"
#include "Player.h"

namespace motion {

    enum MaskMode { MaskModeStencil = 0, MaskModeAlpha = 1 };

    enum TimelinePlayFlag {
        TimelinePlayFlagParallel = 1,
        TimelinePlayFlagSequential = 2
    };

    class EmotePlayer {
    public:
        explicit EmotePlayer(ResourceManager rm);
        ~EmotePlayer();

        // --- Properties ---
        void setUseD3D(bool v) { _useD3D = v; }
        [[nodiscard]] bool getUseD3D() const { return _useD3D; }

        void setSmoothing(bool v) { _smoothing = v; }
        [[nodiscard]] bool getSmoothing() const { return _smoothing; }

        void setMeshDivisionRatio(double v);
        [[nodiscard]] double getMeshDivisionRatio() const { return _meshDivisionRatio; }

        void setQueuing(bool v) { _queuing = v; }
        [[nodiscard]] bool getQueuing() const { return _queuing; }

        void setHairScale(double v) { _hairScale = v; }
        [[nodiscard]] double getHairScale() const { return _hairScale; }

        void setPartsScale(double v) { _partsScale = v; }
        [[nodiscard]] double getPartsScale() const { return _partsScale; }

        void setBustScale(double v) { _bustScale = v; }
        [[nodiscard]] double getBustScale() const { return _bustScale; }

        void setBodyScale(double v) { _bodyScale = v; }
        [[nodiscard]] double getBodyScale() const { return _bodyScale; }

        void setVisible(bool v);
        [[nodiscard]] bool getVisible() const { return _visible; }

        [[nodiscard]] bool getAnimating() const;

        void setProgress(double v) { _progress = v; }
        [[nodiscard]] double getProgress() const { return _progress; }

        void setModified(bool v) { _modified = v; }
        [[nodiscard]] bool getModified() const { return _modified; }

        void setDrawVisible(bool v) { _drawVisible = v; }
        [[nodiscard]] bool getDrawVisible() const { return _drawVisible; }

        void setDrawOpacity(double v) { _drawOpacity = v; }
        [[nodiscard]] double getDrawOpacity() const { return _drawOpacity; }

        void setOpengl(bool v) { _opengl = v; }
        [[nodiscard]] bool getOpengl() const { return _opengl; }

        void setModule(tTJSVariant v);
        [[nodiscard]] tTJSVariant getModule() const;

        [[nodiscard]] bool getPlayCallback() const { return _playCallback; }

        // --- Methods ---
        void create();
        void load(tTJSVariant data);
        tTJSVariant clone();
        void show();
        void hide();
        void assignState();
        void initPhysics();

        void setRot(double rot, double transition = 0.0,
                    double ease = 0.0);
        static tjs_error setRotCompat(tTJSVariant *result, tjs_int numparams,
                                      tTJSVariant **param,
                                      iTJSDispatch2 *objthis);
        double getRot();

        void setCoord(double x, double y, double transition = 0.0,
                      double ease = 0.0);
        static tjs_error setCoordCompat(tTJSVariant *result, tjs_int numparams,
                                        tTJSVariant **param,
                                        iTJSDispatch2 *objthis);
        void setScale(double s, double transition = 0.0,
                      double ease = 0.0);
        static tjs_error setScaleCompat(tTJSVariant *result, tjs_int numparams,
                                        tTJSVariant **param,
                                        iTJSDispatch2 *objthis);
        double getScale();
        void setMirror(bool mirror);
        void setColor(tjs_int color, double transition = 0.0,
                      double ease = 0.0);
        static tjs_error setColorCompat(tTJSVariant *result, tjs_int numparams,
                                        tTJSVariant **param,
                                        iTJSDispatch2 *objthis);
        tjs_int getColor();

        tjs_int countVariables();
        ttstr getVariableLabelAt(tjs_int idx);
        tjs_int countVariableFrameAt(tjs_int idx);
        ttstr getVariableFrameLabelAt(tjs_int idx, tjs_int frameIdx);
        double getVariableFrameValueAt(tjs_int idx, tjs_int frameIdx);

        void setVariable(ttstr label, double value, double transition = 0.0,
                         double ease = 0.0);
        static tjs_error setVariableCompat(tTJSVariant *result,
                                           tjs_int numparams,
                                           tTJSVariant **param,
                                           iTJSDispatch2 *objthis);
        double getVariable(ttstr label);

        void startWind(double minAngle, double maxAngle, double amplitude,
                       double freqX = 0.0, double freqY = 0.0);
        static tjs_error startWindCompat(tTJSVariant *result, tjs_int numparams,
                                         tTJSVariant **param,
                                         iTJSDispatch2 *objthis);
        void stopWind();
        static tjs_error stopWindCompat(tTJSVariant *result, tjs_int numparams,
                                        tTJSVariant **param,
                                        iTJSDispatch2 *objthis);

        tjs_int countMainTimelines();
        ttstr getMainTimelineLabelAt(tjs_int idx);
        tjs_int countDiffTimelines();
        ttstr getDiffTimelineLabelAt(tjs_int idx);
        tjs_int countPlayingTimelines();
        ttstr getPlayingTimelineLabelAt(tjs_int idx);
        tjs_int getPlayingTimelineFlagsAt(tjs_int idx);

        bool isLoopTimeline(ttstr label);
        tjs_int getTimelineTotalFrameCount(ttstr label);
        void playTimeline(ttstr label, tjs_int flags);
        bool isTimelinePlaying(ttstr label);
        void stopTimeline(ttstr label);

        void setTimelineBlendRatio(ttstr label, double ratio);
        double getTimelineBlendRatio(ttstr label);
        void fadeInTimeline(ttstr label, double duration, tjs_int flags);
        void fadeOutTimeline(ttstr label, double duration, tjs_int flags);

        void setTimeline(ttstr label, bool loop);

        void skip();
        void addPlayCallback();
        void pass(double dt);
        void progress(double dt);

        void setOuterForce(double x, double y);
        void setOuterForce(ttstr label, double x, double y,
                           double transition = 0.0, double ease = 0.0);
        static tjs_error setOuterForceCompat(tTJSVariant *result,
                                             tjs_int numparams,
                                             tTJSVariant **param,
                                             iTJSDispatch2 *objthis);
        tTJSVariant getOuterForce();
        bool contains(double x, double y);
        bool contains(ttstr label, double x, double y);
        static tjs_error containsCompat(tTJSVariant *result, tjs_int numparams,
                                        tTJSVariant **param,
                                        iTJSDispatch2 *objthis);

        // Access to internal Player for delegation from NCB methods
        Player &getPlayer() { return _player; }
        const Player &getPlayer() const { return _player; }

    private:
        // Aligned to libkrkr2.so: EmoteObject(40b) owns ResourceManager + Player(1496b).
        // All animation logic delegates to this Player instance.
        Player _player;

        // EmotePlayer-specific state (not on Player)
        tTJSVariant _module;
        bool _useD3D = false;
        bool _smoothing = true;
        double _meshDivisionRatio = 1.0;
        bool _queuing = false;
        double _hairScale = 1.0;
        double _partsScale = 1.0;
        double _bustScale = 1.0;
        double _bodyScale = 1.0;
        double _progress = 0.0;
        bool _modified = false;
        bool _drawVisible = true;
        double _drawOpacity = 1.0;
        bool _opengl = false;
        bool _visible = true;
        bool _playCallback = false;

        // Aligned to libkrkr2.so sub_530260: finalScale = baseScale * userScale
        float _baseScale = 1.0f;   // +40 in binary D3DEmotePlayer wrapper
        float _userScale = 1.0f;   // +44 in binary

        // Cached values for getScale/getRot/getColor
        // Binary getters return hardcoded 1.0/0.0/0 but we track for local use
        double _rot = 0.0;
        double _coordX = 0.0;
        double _coordY = 0.0;
        bool _mirrorBase = false;
        bool _mirrorRequested = false;
        bool _mirrorChanged = false;
        tjs_int _color = 0xFFFFFF;
    };

    // Thin wrapper for top-level NCB registration (avoids ncbind conflict)
    class D3DEmotePlayer : public EmotePlayer {
    public:
        explicit D3DEmotePlayer(ResourceManager rm) : EmotePlayer(rm) {}
    };

} // namespace motion
