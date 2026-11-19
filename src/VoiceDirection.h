#pragma once
#include "CameraMath.h"
namespace shoulder {
inline float wrappedDegrees(float angle) {
    return std::remainder(angle, 360.f);
}
inline bool planarAngle(Quat q, float &angle) {
    float norm = q.x * q.x + q.y * q.y + q.z * q.z + q.w * q.w;
    if (!std::isfinite(norm) || norm < .98f || norm > 1.02f || std::abs(q.x) > .001f || std::abs(q.y) > .001f)
        return false;
    angle = std::atan2(2 * (q.w * q.z + q.x * q.y), 1 - 2 * (q.y * q.y + q.z * q.z)) * 57.29577951f;
    return std::isfinite(angle);
}
inline Quat planarRotation(float angle) {
    float half = wrappedDegrees(angle) * .00872664626f;
    return {0, 0, std::sin(half), std::cos(half)};
}
inline float quaternionYaw(Quat q) {
    return std::atan2(2 * (q.x * q.z + q.w * q.y), 1 - 2 * (q.x * q.x + q.y * q.y)) * 57.29577951f;
}
// Native UI angle changes by -cameraYaw. Preserve its original-view bearing,
// then make local UI angle change by +cameraYaw on the verified normal canvas.
struct VoiceDirection {
    bool valid{}, owned{};
    float nativeKey{}, lastApplied{};
    bool update(float observed, float cameraYaw, float originalYaw, float &corrected) {
        if (!std::isfinite(observed) || !std::isfinite(cameraYaw) || !std::isfinite(originalYaw))
            return false;
        if (!valid || !owned || std::abs(wrappedDegrees(observed - lastApplied)) > .001f) {
            nativeKey = wrappedDegrees(observed + cameraYaw);
            valid = true;
        }
        corrected = wrappedDegrees(nativeKey + cameraYaw - 2 * originalYaw);
        lastApplied = corrected;
        owned = true;
        return true;
    }
    bool owns(float observed) const {
        return valid && owned && std::isfinite(observed) &&
               std::abs(wrappedDegrees(observed - lastApplied)) <= .001f;
    }
    float nativeAt(float cameraYaw) const {
        return wrappedDegrees(nativeKey - cameraYaw);
    }
};
} // namespace shoulder
