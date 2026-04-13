#pragma once

#include <array>
#include <cstdint>

namespace motion::detail {

    // Aligned to libkrkr2.so Player_hitTest (0x690DF0):
    //   int32 type @ +0
    //   int32 pad  @ +4
    //   doubles    @ +8
    struct HitData {
        std::int32_t type = 0;
        std::int32_t pad = 0;
        std::array<double, 16> values{};
    };

    inline HitData makeCircleHitData(double cx, double cy, double r,
                                     std::int32_t typeOverride = 1) {
        HitData hit{};
        hit.type = typeOverride;
        hit.values[0] = cx;
        hit.values[1] = cy;
        hit.values[2] = r;
        return hit;
    }

    inline HitData makeRectHitData(double left, double top,
                                   double right, double bottom,
                                   std::int32_t typeOverride = 2) {
        HitData hit{};
        hit.type = typeOverride;
        hit.values[3] = left;
        hit.values[4] = top;
        hit.values[5] = right;
        hit.values[6] = bottom;
        return hit;
    }

    inline HitData makeQuadHitData(double x0, double y0,
                                   double x1, double y1,
                                   double x2, double y2,
                                   double x3, double y3,
                                   std::int32_t typeOverride = 3) {
        HitData hit{};
        hit.type = typeOverride;
        hit.values[7] = x0;
        hit.values[8] = y0;
        hit.values[9] = x1;
        hit.values[10] = y1;
        hit.values[11] = x2;
        hit.values[12] = y2;
        hit.values[13] = x3;
        hit.values[14] = y3;
        return hit;
    }

    inline bool hitTestHitData(const HitData &hit, double x, double y) {
        switch(hit.type) {
            case 1: {
                const double cx = hit.values[0];
                const double cy = hit.values[1];
                const double r = hit.values[2];
                const double dx = x - cx;
                const double dy = y - cy;
                return dx * dx + dy * dy <= r * r;
            }
            case 2:
                return hit.values[3] <= x && x < hit.values[5] &&
                    hit.values[4] <= y && y < hit.values[6];
            case 3: {
                const double x0 = hit.values[7];
                const double y0 = hit.values[8];
                const double x1 = hit.values[9];
                const double y1 = hit.values[10];
                const double x2 = hit.values[11];
                const double y2 = hit.values[12];
                const double x3 = hit.values[13];
                const double y3 = hit.values[14];
                const auto cross = [](double ax, double ay, double bx, double by,
                                      double px, double py) {
                    return (bx - ax) * (py - ay) - (by - ay) * (px - ax);
                };
                const double winding = cross(x0, y0, x1, y1, x2, y2) >= 0.0
                    ? 1.0
                    : -1.0;
                return winding * cross(x0, y0, x1, y1, x, y) <= 0.0 &&
                    winding * cross(x1, y1, x2, y2, x, y) <= 0.0 &&
                    winding * cross(x2, y2, x3, y3, x, y) <= 0.0 &&
                    winding * cross(x3, y3, x0, y0, x, y) <= 0.0;
            }
            default:
                return false;
        }
    }

} // namespace motion::detail
