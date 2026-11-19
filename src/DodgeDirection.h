#pragma once
#include "CameraMath.h"
namespace shoulder {
inline Vec3 cameraRelativeDodge(float yaw, bool w, bool a, bool s, bool d) {
    if (!std::isfinite(yaw))
        return {};
    return normalized(forward(yaw, 0) * float(int(w) - int(s)) + right(yaw) * float(int(d) - int(a)));
}
enum class DodgeAimMode { Normal, Prepare, Native };
// Update-edge intent followed by observed native RollSpeed. No invented timer
// for the roll itself; its duration/cost/cooldown remain native.
struct DodgeDirection {
    bool spaceWas{}, pending{}, nativeRoll{};
    double started{};
    Vec3 requested{};
    void reset(bool spaceDown = false) {
        *this = {};
        spaceWas = spaceDown;
    }
    DodgeAimMode sample(bool valid, int speed, bool preUpdate, bool space, Vec3 intent, double now) {
        if (!valid || !std::isfinite(now)) {
            reset(space);
            return DodgeAimMode::Normal;
        }
        if (speed == 5009) {
            nativeRoll = true;
            pending = false;
            spaceWas = space;
            return DodgeAimMode::Native;
        }
        if (nativeRoll) {
            nativeRoll = false;
            pending = false;
        }
        if (preUpdate) {
            bool edge = space && !spaceWas;
            spaceWas = space;
            if (edge && finite(intent) && length(intent) > .9f) {
                pending = true;
                requested = normalized(intent);
                started = now;
            }
        }
        // An unaccepted/blocked input must not suspend normal aim indefinitely.
        if (pending && (now < started || now - started > .2))
            pending = false;
        return pending ? (preUpdate ? DodgeAimMode::Prepare : DodgeAimMode::Native) : DodgeAimMode::Normal;
    }
};
} // namespace shoulder
