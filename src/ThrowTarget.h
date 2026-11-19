#pragma once
#include "Geometry.h"
#include <array>
namespace shoulder {
// Verified current IL2CPP layout: Vector3 at 0, has_value at 12, size 16.
struct NativeOptionalPoint {
    Vec3 value{};
    unsigned char present{};
    std::array<unsigned char, 3> padding{};
};
static_assert(sizeof(NativeOptionalPoint) == 16);
inline bool groundTargetSelection(NativeOptionalPoint direct, NativeOptionalPoint ground, int source,
                                  Vec3 player) {
    return direct.present == 0 && ground.present == 1 && source == 2 && finite(ground.value) &&
           finite(player) && std::abs(ground.value.y - player.y) < .15f &&
           length(ground.value - player) < 150.f;
}
inline bool cameraGroundTarget(Vec3 camera, Vec3 forward, Vec3 player, float groundY, Vec3 &target) {
    if (!finite(camera) || !finite(forward) || !finite(player) || !std::isfinite(groundY) ||
        length(forward) < .9f || length(forward) > 1.1f || length(camera - player) > 20.f ||
        std::abs(groundY - player.y) > .15f)
        return false;
    forward = normalized(forward);
    float horizontal = std::hypot(forward.x, forward.z);
    if (horizontal < .15f)
        return false;
    float t = forward.y < -.0001f ? (groundY - camera.y) / forward.y : -1.f;
    // A forward downward ray selects its ground-plane intersection. Looking at
    // the horizon/sky selects a finite far candidate. The quickbar integration
    // must feed it before native limiting, never into the already limited cache.
    if (t > 0 && std::isfinite(t))
        target = camera + forward * std::min(t, 100.f / horizontal);
    else
        target = camera + Vec3{forward.x / horizontal, 0, forward.z / horizontal} * 100.f;
    target.y = groundY;
    auto delta = target - player;
    delta.y = 0;
    float range = length(delta);
    if (range > 100.f)
        target = player + delta * (100.f / range);
    target.y = groundY;
    return finite(target);
}
} // namespace shoulder
