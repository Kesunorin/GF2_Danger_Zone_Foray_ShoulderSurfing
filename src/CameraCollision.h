#pragma once
#include "Geometry.h"
#include <initializer_list>

namespace shoulder {
// The direction is unit length and the result is travel distance, not a fraction.
inline float spherePointSweep(Vec3 origin, Vec3 direction, float radius, Vec3 point, float limit) {
    auto offset = origin - point;
    float b = dot(offset, direction), c = dot(offset, offset) - radius * radius;
    if (c <= 0)
        return 0;
    float discriminant = b * b - c;
    if (discriminant < 0)
        return std::numeric_limits<float>::infinity();
    float distance = -b - std::sqrt(discriminant);
    return distance >= 0 && distance <= limit ? distance : std::numeric_limits<float>::infinity();
}
inline bool pointInTriangle(Vec3 p, Vec3 a, Vec3 b, Vec3 c, Vec3 normal) {
    constexpr float tolerance = -1e-6f;
    return dot(cross(b - a, p - a), normal) >= tolerance && dot(cross(c - b, p - b), normal) >= tolerance &&
           dot(cross(a - c, p - c), normal) >= tolerance;
}
inline float sphereEdgeSweep(Vec3 origin, Vec3 direction, float radius, Vec3 a, Vec3 b, float limit) {
    auto edge = b - a;
    float squared = dot(edge, edge);
    if (squared < 1e-10f)
        return spherePointSweep(origin, direction, radius, a, limit);
    auto relative = origin - a;
    float projection = dot(relative, edge) / squared, velocity = dot(direction, edge) / squared;
    auto radial = relative - edge * projection, radialVelocity = direction - edge * velocity;
    float aa = dot(radialVelocity, radialVelocity), bb = dot(radial, radialVelocity),
          cc = dot(radial, radial) - radius * radius;
    // Initial overlap only applies to the finite segment, never its extension.
    if (cc <= 0 && projection >= 0 && projection <= 1)
        return 0;
    if (aa < 1e-10f)
        return std::numeric_limits<float>::infinity();
    float discriminant = bb * bb - aa * cc;
    if (discriminant < 0)
        return std::numeric_limits<float>::infinity();
    float distance = (-bb - std::sqrt(discriminant)) / aa, along = projection + velocity * distance;
    return distance >= 0 && distance <= limit && along >= 0 && along <= 1
               ? distance
               : std::numeric_limits<float>::infinity();
}
inline float sphereTriangleSweep(Vec3 origin, Vec3 direction, float radius, Vec3 a, Vec3 b, Vec3 c,
                                 float limit) {
    const float miss = std::numeric_limits<float>::infinity();
    if (!finite(origin) || !finite(direction) || !finite(a) || !finite(b) || !finite(c) ||
        !std::isfinite(radius) || radius <= 0 || !std::isfinite(limit) || limit < 0 ||
        std::abs(dot(direction, direction) - 1) > 1e-3f)
        return miss;
    float closest = miss;
    for (auto point : {a, b, c})
        closest = std::min(closest, spherePointSweep(origin, direction, radius, point, limit));
    closest = std::min(closest, sphereEdgeSweep(origin, direction, radius, a, b, limit));
    closest = std::min(closest, sphereEdgeSweep(origin, direction, radius, b, c, limit));
    closest = std::min(closest, sphereEdgeSweep(origin, direction, radius, c, a, limit));
    auto rawNormal = cross(b - a, c - a);
    float magnitude = length(rawNormal);
    if (magnitude < 1e-8f)
        return closest;
    auto normal = rawNormal * (1.f / magnitude);
    float signedDistance = dot(origin - a, normal), speed = dot(direction, normal);
    auto projected = origin - normal * signedDistance;
    if (std::abs(signedDistance) <= radius && pointInTriangle(projected, a, b, c, normal))
        return 0;
    if (std::abs(speed) > 1e-8f)
        for (float side : {-1.f, 1.f}) {
            float distance = (side * radius - signedDistance) / speed;
            if (distance < 0 || distance > limit || distance >= closest)
                continue;
            auto contact = origin + direction * distance - normal * (side * radius);
            if (pointInTriangle(contact, a, b, c, normal))
                closest = distance;
        }
    return closest;
}
inline float sphereBoundsEntry(Vec3 origin, Vec3 direction, float radius, Bounds bounds, float limit) {
    const float miss = std::numeric_limits<float>::infinity();
    if (!finite(origin) || !finite(direction) || !finite(bounds.center) || !finite(bounds.extents) ||
        !std::isfinite(radius) || radius <= 0 || !std::isfinite(limit) || limit < 0 || bounds.extents.x < 0 ||
        bounds.extents.y < 0 || bounds.extents.z < 0 || std::abs(dot(direction, direction) - 1) > 1e-3f)
        return miss;
    // Expanded AABB entry is a conservative lower bound for sphere/mesh contact.
    // It orders meshes without claiming that a box intersection is a real hit.
    float nearest = 0, farthest = limit;
    float o[] = {origin.x, origin.y, origin.z}, d[] = {direction.x, direction.y, direction.z};
    float c[] = {bounds.center.x, bounds.center.y, bounds.center.z};
    float e[] = {bounds.extents.x + radius, bounds.extents.y + radius, bounds.extents.z + radius};
    for (int i = 0; i < 3; i++) {
        if (std::abs(d[i]) < 1e-7f) {
            if (std::abs(o[i] - c[i]) > e[i])
                return miss;
            continue;
        }
        float lo = (c[i] - e[i] - o[i]) / d[i], hi = (c[i] + e[i] - o[i]) / d[i];
        if (lo > hi)
            std::swap(lo, hi);
        nearest = std::max(nearest, lo);
        farthest = std::min(farthest, hi);
        if (nearest > farthest)
            return miss;
    }
    return nearest;
}
inline bool sphereBoundsSweep(Vec3 origin, Vec3 direction, float radius, Bounds bounds, float limit) {
    return std::isfinite(sphereBoundsEntry(origin, direction, radius, bounds, limit));
}
inline float cameraClearance(float requested, float hit, float skin = .03f) {
    if (!std::isfinite(requested) || requested < 0)
        return 0;
    if (!std::isfinite(hit) || hit < 0 || hit > requested)
        return requested;
    return std::min(requested, std::max(0.f, hit - std::max(0.f, skin)));
}
inline bool unchangedGeometryTransform(const Matrix &scanned, const Matrix &current) {
    for (int i = 0; i < 16; i++)
        if (!std::isfinite(scanned.m[i]) || !std::isfinite(current.m[i]) ||
            std::abs(scanned.m[i] - current.m[i]) > 1e-4f)
            return false;
    return true;
}
} // namespace shoulder
