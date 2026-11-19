#pragma once
#include "CameraMath.h"
namespace shoulder {
// A native refresh takes ownership back. Repeated reads of our own output retain
// the original anchor, so a stationary bar cannot accumulate projection offsets.
inline bool sameHudPosition(Vec3 a, Vec3 b) {
    return finite(a) && finite(b) && length(a - b) < .00005f;
}
struct HudPosition {
    Vec3 native{}, written{};
    bool owned{};
    bool update(Vec3 observed, Vec3 desired) {
        if (!finite(observed) || !finite(desired))
            return false;
        if (!owned || !sameHudPosition(observed, written))
            native = observed;
        written = desired;
        owned = true;
        return true;
    }
    bool restore(Vec3 observed, Vec3 &result) const {
        if (!owned || !sameHudPosition(observed, written))
            return false;
        result = native;
        return finite(result);
    }
};
inline bool headBarScreenPoint(Vec3 projected, int width, int height, Vec3 &screen) {
    if (!finite(projected) || width < 1 || height < 1)
        return false;
    screen = projected;
    // Keep behind-camera anchors outside the canvas. This only moves the native
    // anchor; it does not change alpha, active state, damage timers or health.
    if (projected.z <= .08f) {
        screen.x = -float(width) * 2;
        screen.y = -float(height) * 2;
    }
    return finite(screen);
}
} // namespace shoulder
