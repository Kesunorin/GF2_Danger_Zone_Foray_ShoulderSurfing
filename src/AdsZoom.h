#pragma once
#include <algorithm>
#include <cmath>

namespace shoulder {
// Perspective image scale is cot(FOV/2). Interpolate that scale, rather than
// the FOV angle, so equal progress steps produce equal visual magnification.
inline float linearZoomFov(float normalFov, float targetFov, float progress) {
    constexpr float radians = .017453292519943295f;
    if (!std::isfinite(normalFov) || !std::isfinite(targetFov) || normalFov <= 0 || normalFov >= 179 ||
        targetFov <= 0 || targetFov >= 179)
        return normalFov;
    if (!std::isfinite(progress))
        progress = 0;
    progress = std::clamp(progress, 0.f, 1.f);
    if (progress == 0)
        return normalFov;
    if (progress == 1)
        return targetFov;
    float normal = 1.f / std::tan(normalFov * .5f * radians);
    float target = 1.f / std::tan(targetFov * .5f * radians);
    return 2.f * std::atan(1.f / (normal + (target - normal) * progress)) / radians;
}
struct NativeAdsProgress {
    bool valid{};
    float progress{}, duration{};
};
// Captured shoot fields: normalized remaining time and remaining seconds.
// Inactive fields can recover toward 1 after release: do not animate those.
// Use native progress directly, including native partial-aim resumption.
inline NativeAdsProgress nativeAdsProgress(bool engaged, float remaining, float seconds) {
    if (!engaged)
        return {true, 0, 0};
    if (!std::isfinite(remaining) || !std::isfinite(seconds) || remaining < 0 || remaining > 1.0001f ||
        seconds < 0 || seconds > 30)
        return {};
    if (remaining == 0)
        return seconds == 0 ? NativeAdsProgress{true, 1, 0} : NativeAdsProgress{};
    float duration = seconds / remaining;
    if (!std::isfinite(duration) || duration < .01f || duration > 30)
        return {};
    return {true, 1.f - std::clamp(remaining, 0.f, 1.f), duration};
}
} // namespace shoulder
