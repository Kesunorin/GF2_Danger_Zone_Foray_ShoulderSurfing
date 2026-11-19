#pragma once
#include "CameraMath.h"
#include <limits>
namespace shoulder {
struct Bounds {
    Vec3 center, extents;
};
struct Matrix {
    float m[16];
};
inline float dot(Vec3 a, Vec3 b) {
    return a.x * b.x + a.y * b.y + a.z * b.z;
}
inline Vec3 cross(Vec3 a, Vec3 b) {
    return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}
inline Vec3 transformPoint(const Matrix &m, Vec3 p) {
    return {m.m[0] * p.x + m.m[4] * p.y + m.m[8] * p.z + m.m[12],
            m.m[1] * p.x + m.m[5] * p.y + m.m[9] * p.z + m.m[13],
            m.m[2] * p.x + m.m[6] * p.y + m.m[10] * p.z + m.m[14]};
}
inline bool boundsRay(Vec3 origin, Vec3 direction, Bounds b, float limit) {
    float nearest = 0, farthest = limit;
    float o[] = {origin.x, origin.y, origin.z}, d[] = {direction.x, direction.y, direction.z},
          c[] = {b.center.x, b.center.y, b.center.z}, e[] = {b.extents.x, b.extents.y, b.extents.z};
    for (int i = 0; i < 3; i++) {
        if (std::abs(d[i]) < 1e-7f) {
            if (std::abs(o[i] - c[i]) > e[i])
                return false;
            continue;
        }
        float lo = (c[i] - e[i] - o[i]) / d[i], hi = (c[i] + e[i] - o[i]) / d[i];
        if (lo > hi)
            std::swap(lo, hi);
        nearest = std::max(nearest, lo);
        farthest = std::min(farthest, hi);
        if (nearest > farthest)
            return false;
    }
    return true;
}
inline float triangleRay(Vec3 origin, Vec3 direction, Vec3 a, Vec3 b, Vec3 c) {
    auto e1 = b - a, e2 = c - a, p = cross(direction, e2);
    float det = dot(e1, p);
    if (std::abs(det) < 1e-8f)
        return std::numeric_limits<float>::infinity();
    auto t = origin - a;
    float u = dot(t, p) / det;
    if (u < 0 || u > 1)
        return std::numeric_limits<float>::infinity();
    auto q = cross(t, e1);
    float v = dot(direction, q) / det;
    if (v < 0 || u + v > 1)
        return std::numeric_limits<float>::infinity();
    float distance = dot(e2, q) / det;
    return distance > .01f ? distance : std::numeric_limits<float>::infinity();
}
} // namespace shoulder
