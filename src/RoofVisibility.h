#pragma once
#include <cmath>
#include <string_view>

namespace shoulder {
template <class Node, class ReadName, class ReadParent>
bool nativeRoofHierarchy(Node node, ReadName readName, ReadParent readParent) {
    // The captured native roof lives under _Roof. Do not classify arbitrary
    // roofs by mesh substrings or apply this to walls and other fade owners.
    for (unsigned depth = 0; node && depth < 8; ++depth) {
        if (readName(node) == L"_Roof")
            return true;
        node = readParent(node);
    }
    return false;
}
inline bool roofFadeValue(float value) {
    return std::isfinite(value) && value >= 0 && value <= 1;
}
struct RoofFadeEndpoint {
    float original{}, last{};
    bool owned{}, nativeChanged{};
    bool apply(float hidden, float visible, float &replacement) {
        if (nativeChanged || !roofFadeValue(hidden) || !roofFadeValue(visible))
            return false;
        if (owned && hidden != last) {
            owned = false;
            nativeChanged = true;
            return false;
        }
        if (!owned) {
            if (hidden == visible)
                return false;
            original = hidden;
        }
        owned = true;
        last = replacement = visible;
        return true;
    }
    bool restore(float current, float &replacement) {
        bool allowed = owned && roofFadeValue(current) && current == last;
        if (allowed)
            replacement = original;
        owned = false;
        return allowed;
    }
};
} // namespace shoulder
