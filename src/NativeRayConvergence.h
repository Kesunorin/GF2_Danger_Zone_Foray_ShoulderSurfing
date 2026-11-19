#pragma once
#include "Geometry.h"
namespace shoulder {
struct RaySourceOffset {
    float right{}, up{}, axial{};
};
inline bool nativeRayOffset(Vec3 reference, Vec3 start, Vec3 end, Vec3 nativeDirection,
                            RaySourceOffset &offset) {
    if (!finite(reference) || !finite(start) || !finite(end) || !finite(nativeDirection))
        return false;
    float size = length(nativeDirection), span = length(end - start);
    if (size < .9f || size > 1.1f || span < .1f || span > 210.f || length(start - reference) > 2.f)
        return false;
    auto direction = normalized(nativeDirection), beam = normalized(end - start);
    if (dot(direction, beam) < .9993908f)
        return false; // 2 degrees; reject stale/scattered rays.
    float horizontal = std::hypot(direction.x, direction.z);
    if (horizontal < .2f)
        return false;
    Vec3 right{direction.z / horizontal, 0, -direction.x / horizontal}, up = cross(direction, right);
    auto delta = start - reference;
    offset = {dot(delta, right), dot(delta, up), dot(delta, direction)};
    return std::abs(offset.right) <= .25f && std::abs(offset.up) <= .25f && offset.axial >= -.25f &&
           offset.axial <= 2.f;
}
struct NativeRayCalibration {
    RaySourceOffset offset{};
    bool ready{};
    void reset() {
        offset = {};
        ready = false;
    }
    void sample(bool valid, RaySourceOffset value, float dt, bool frozen) {
        if (!valid || !std::isfinite(value.right) || !std::isfinite(value.up) ||
            !std::isfinite(value.axial) || std::abs(value.right) > .25f || std::abs(value.up) > .25f ||
            value.axial < -.25f || value.axial > 2.f || !std::isfinite(dt) || dt <= 0) {
            reset();
            return;
        }
        // The current native ray is already validated. Waiting for a stable pose
        // delayed ADS alignment and smoothing lagged behind its moving source.
        // Acquire the first valid source even if firing began on the same frame;
        // then keep that established geometry through firing animation.
        if (frozen && ready)
            return;
        offset = value;
        ready = true;
    }
};
inline Vec3 nativeRayConvergence(Vec3 reference, Vec3 target, Vec3 base, RaySourceOffset offset) {
    if (!finite(reference) || !finite(target) || !finite(base) || !std::isfinite(offset.right) ||
        !std::isfinite(offset.up) || !std::isfinite(offset.axial) || std::abs(offset.right) > .25f ||
        std::abs(offset.up) > .25f || offset.axial < -.25f || offset.axial > 2.f)
        return base;
    auto delta = target - reference;
    float range = length(delta), horizontal = std::hypot(delta.x, delta.z);
    if (range < 1.f || horizontal < 1.f || length(base) < .9f || length(base) > 1.1f ||
        horizontal <= std::abs(offset.right))
        return base;
    float alongSquared = range * range - offset.right * offset.right - offset.up * offset.up;
    if (alongSquared <= 0)
        return base;
    float along = std::sqrt(alongSquared);
    if (along <= offset.axial + .1f)
        return base; // The target must be ahead of the source.
    float yaw = std::atan2(delta.x, delta.z) - std::asin(offset.right / horizontal);
    float elevation = std::atan2(delta.y, std::sqrt(horizontal * horizontal - offset.right * offset.right)) -
                      std::atan2(offset.up, along);
    Vec3 result{std::sin(yaw) * std::cos(elevation), std::sin(elevation),
                std::cos(yaw) * std::cos(elevation)};
    if (!finite(result) || dot(normalized(base), result) < .9975641f)
        return base; // 4-degree bound.
    return result;
}
} // namespace shoulder
