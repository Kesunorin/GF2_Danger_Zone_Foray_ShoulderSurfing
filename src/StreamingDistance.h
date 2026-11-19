#pragma once
#include <algorithm>
#include <array>
#include <cmath>
namespace shoulder {
// Required, inner-near, near, preload and unload, in native world units.
// The inner-near field's use is verified in the classifier and normalizer;
// it is deliberately not exposed as a separately named user setting.
using StreamingRadii = std::array<float, 5>;
inline bool validStreamingRadii(const StreamingRadii &r, float requiredMultiplier) {
    if (!std::isfinite(requiredMultiplier) || requiredMultiplier < 1 || requiredMultiplier > 16)
        return false;
    for (float value : r)
        if (!std::isfinite(value) || value < 1 || value > 10000)
            return false;
    return r[0] <= r[1] && r[1] <= r[2] && r[0] * requiredMultiplier <= r[2] && r[2] <= r[3] &&
           r[3] + 1 <= r[4];
}
inline bool scaledStreamingRadii(const StreamingRadii &native, float requiredMultiplier, float factor,
                                 StreamingRadii &out) {
    if (!std::isfinite(factor) || factor < 1 || factor > 8 ||
        !validStreamingRadii(native, requiredMultiplier))
        return false;
    for (size_t i = 0; i < native.size(); i++)
        out[i] = native[i] * factor;
    return validStreamingRadii(out, requiredMultiplier);
}
struct StreamingDistanceOwnership {
    StreamingRadii native{}, written{};
    bool owned{};
    bool unchanged(const StreamingRadii &current) const {
        // These are primitive fields written with memcpy, not accumulated math.
        return owned && current == written;
    }
    bool restore(const StreamingRadii &current, float multiplier, StreamingRadii &out) const {
        if (!owned)
            return false;
        out = current;
        for (size_t i = 0; i < current.size(); i++)
            if (current[i] == written[i])
                out[i] = native[i];
        // Preserve fresh native writes. Reject a mixed tuple that breaks the
        // original radius ordering rather than overwriting the new values.
        return validStreamingRadii(out, multiplier) && out != current;
    }
};
} // namespace shoulder
