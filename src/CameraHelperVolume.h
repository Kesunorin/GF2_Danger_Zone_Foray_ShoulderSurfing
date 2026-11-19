#pragma once
#include <string_view>

namespace shoulder {
// A local post-processing region uses its BoxCollider to blend Volume settings.
// Require both its dedicated name and a Volume on the same object; a rendered
// wall (or a namesake object without the component) remains collidable.
inline bool postEffectVolumeName(std::wstring_view name) {
    constexpr std::wstring_view prefix = L"_PostEffect";
    return name == prefix || (name.size() > prefix.size() && name.substr(0, prefix.size()) == prefix &&
                              name[prefix.size()] == L'_');
}
inline bool nativePostEffectVolume(std::wstring_view name, bool hasVolume, bool hasRenderer) {
    return postEffectVolumeName(name) && hasVolume && !hasRenderer;
}
// These LD camera-control volumes can be non-trigger BoxColliders. A name
// alone is insufficient: require the native control-group ancestor as well.
inline bool cameraControlVolumeName(std::wstring_view name) {
    constexpr std::wstring_view prefix = L"CameraTrigger";
    if (name.substr(0, prefix.size()) != prefix)
        return false;
    name.remove_prefix(prefix.size());
    while (!name.empty() && name.front() >= L'0' && name.front() <= L'9')
        name.remove_prefix(1);
    if (name.empty())
        return true;
    if (name.size() < 4 || name.substr(0, 2) != L" (" || name.back() != L')')
        return false;
    name.remove_prefix(2);
    name.remove_suffix(1);
    if (name.empty())
        return false;
    for (auto c : name)
        if (c < L'0' || c > L'9')
            return false;
    return true;
}

template <class Node, class ReadName, class ReadParent>
bool nativeCameraControlVolume(Node node, ReadName readName, ReadParent readParent) {
    if (!node || !cameraControlVolumeName(readName(node)))
        return false;
    // Bound both malformed/cyclic hierarchies and per-contact lookup cost.
    for (unsigned depth = 0; node && depth < 8; ++depth) {
        if (readName(node) == L"CameraTriggerGroup_LD")
            return true;
        node = readParent(node);
    }
    return false;
}
} // namespace shoulder
