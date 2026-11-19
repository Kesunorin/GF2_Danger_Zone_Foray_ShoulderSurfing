#pragma once
#include <cmath>
#include <algorithm>
namespace shoulder {
struct Vec3 {
    float x{}, y{}, z{};
};
struct Quat {
    float x{}, y{}, z{}, w{1};
};
inline Vec3 operator+(Vec3 a, Vec3 b) {
    return {a.x + b.x, a.y + b.y, a.z + b.z};
}
inline Vec3 operator-(Vec3 a, Vec3 b) {
    return {a.x - b.x, a.y - b.y, a.z - b.z};
}
inline Vec3 operator*(Vec3 a, float s) {
    return {a.x * s, a.y * s, a.z * s};
}
inline float length(Vec3 v) {
    return std::sqrt(v.x * v.x + v.y * v.y + v.z * v.z);
}
inline Vec3 normalized(Vec3 v) {
    float n = length(v);
    return n > .0001f ? v * (1.f / n) : Vec3{};
}
inline Vec3 forward(float yaw, float pitch) {
    constexpr float r = .01745329252f;
    yaw *= r;
    pitch *= r;
    return {std::sin(yaw) * std::cos(pitch), -std::sin(pitch), std::cos(yaw) * std::cos(pitch)};
}
inline Vec3 right(float yaw) {
    yaw *= .01745329252f;
    return {std::cos(yaw), 0, -std::sin(yaw)};
}
inline Quat rotation(float yaw, float pitch) {
    float y = yaw * .00872664626f, p = pitch * .00872664626f;
    return {std::sin(p) * std::cos(y), std::cos(p) * std::sin(y), -std::sin(p) * std::sin(y),
            std::cos(p) * std::cos(y)};
}
inline Vec3 position(Vec3 player, float yaw, float pitch, float side, float height, float distance) {
    return player + Vec3{0, height, 0} + right(yaw) * side - forward(yaw, pitch) * distance;
}
inline bool finite(Vec3 v) {
    return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);
}
} // namespace shoulder
