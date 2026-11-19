#pragma once
#include <algorithm>
#include <cmath>
namespace shoulder {
inline bool validADSMagnification(float value) {
    return value == 0 || value == 1 || value == 2 || value == 8;
}
struct AdsView {
    float fov, sensitivity;
};
inline AdsView magnifiedADS(float normalFov, float fixedADSFov, float baseSensitivity, float magnification) {
    if (!validADSMagnification(magnification) || magnification == 0)
        return {fixedADSFov, baseSensitivity};
    constexpr float toRadians = .01745329252f, toDegrees = 57.29577951f;
    float fov = 2.f * std::atan(std::tan(normalFov * .5f * toRadians) / magnification) * toDegrees;
    return {fov, baseSensitivity / magnification};
}
} // namespace shoulder
