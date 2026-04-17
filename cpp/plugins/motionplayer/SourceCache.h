//
// Reverse-engineered from libkrkr2.so motionplayer.dll
// Stub classes for TJS API compatibility
//
// Aligned to libkrkr2.so Motion_namespace_ncb_register (0x6D9B08):
// Includes Point, Circle, Rect, Quad, LayerGetter stubs + SourceCache/ObjSource.
//
#pragma once

#include "tjs.h"

namespace motion {

    class SourceCache {
    public:
        SourceCache() = default;
    };

    class ObjSource {
    public:
        ObjSource() = default;
    };

    // Aligned to libkrkr2.so Motion.Point (0x690FBC)
    struct Point {
        int type = 0;
        double x = 0, y = 0;

        int getType() const { return type; }
        double getX() const { return x; }
        double getY() const { return y; }
        bool contains(double, double) { return false; }
    };

    // Aligned to libkrkr2.so Motion.Circle (0x691300)
    struct Circle {
        int type = 1;
        double x = 0, y = 0, r = 0;

        int getType() const { return type; }
        double getX() const { return x; }
        double getY() const { return y; }
        double getR() const { return r; }
        bool contains(double px, double py) {
            double dx = px - x, dy = py - y;
            return dx * dx + dy * dy <= r * r;
        }
    };

    // Aligned to libkrkr2.so Motion.Rect (0x6916A4)
    struct Rect {
        int type = 2;
        double l = 0, t = 0, w = 0, h = 0;

        int getType() const { return type; }
        double getL() const { return l; }
        double getT() const { return t; }
        double getW() const { return w; }
        double getH() const { return h; }
        bool contains(double px, double py) {
            return px >= l && px < l + w && py >= t && py < t + h;
        }
    };

    // Aligned to libkrkr2.so Motion.Quad (0x691AD0)
    struct Quad {
        int type = 3;
        // 4 corners × 2 floats = 8 values
        double verts[8] = {};

        int getType() const { return type; }
        tTJSVariant getP() const { return tTJSVariant(); } // stub
        bool contains(double, double) { return false; } // stub
    };

    // Aligned to libkrkr2.so Motion.LayerGetter (0x69B350)
    // 28 read-only properties from node state
    class LayerGetter {
    public:
        LayerGetter() = default;

        int getType() const { return _type; }
        ttstr getLabel() const { return _label; }
        bool getVisible() const { return _visible; }
        bool getBranchVisible() const { return _branchVisible; }
        bool getLayerVisible() const { return _layerVisible; }
        double getX() const { return _x; }
        double getY() const { return _y; }
        double getLeft() const { return _x; }
        double getTop() const { return _y; }
        bool getFlipX() const { return _flipX; }
        bool getFlipY() const { return _flipY; }
        double getZoomX() const { return _zoomX; }
        double getZoomY() const { return _zoomY; }
        double getAngleDeg() const { return _angleDeg; }
        double getAngleRad() const { return _angleRad; }
        double getSlantX() const { return _slantX; }
        double getSlantY() const { return _slantY; }
        double getOriginX() const { return _originX; }
        double getOriginY() const { return _originY; }
        int getOpacity() const { return _opacity; }
        tTJSVariant getMtx() const { return {}; }
        tTJSVariant getVtx() const { return {}; }
        tTJSVariant getColor() const { return {}; }
        tTJSVariant getBezierPatch() const { return {}; }
        tTJSVariant getShape() const { return {}; }
        tTJSVariant getMotion() const { return {}; }
        tTJSVariant getParticle() const { return {}; }

    private:
        int _type = 0;
        ttstr _label;
        bool _visible = true, _branchVisible = true, _layerVisible = true;
        double _x = 0, _y = 0;
        bool _flipX = false, _flipY = false;
        double _zoomX = 1.0, _zoomY = 1.0;
        double _angleDeg = 0, _angleRad = 0;
        double _slantX = 0, _slantY = 0;
        double _originX = 0, _originY = 0;
        int _opacity = 255;
    };

} // namespace motion
