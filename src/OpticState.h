#pragma once
#include <cstddef>
#include <cstdint>
#include "AdsMagnification.h"
namespace shoulder {
// Dictionary entry layout is separately checked against live IL2CPP metadata.
struct OpticEntry {
    std::int32_t hash, next, slot;
    std::uint32_t id;
};
static_assert(sizeof(OpticEntry) == 16);
struct OpticSample {
    bool valid{};
    std::uint32_t scopeId{};
};
inline OpticSample decodeOpticEntries(const OpticEntry *entries, int count, int freeCount,
                                      std::size_t capacity, int scopeSlot = 2) {
    if (count < 0 || count > 32 || freeCount < 0 || freeCount > count ||
        capacity < static_cast<std::size_t>(count) || capacity > 256 || scopeSlot != 2 || (!entries && count))
        return {};
    int live = 0;
    bool found = false;
    std::uint32_t id = 0;
    for (int i = 0; i < count; i++) {
        const auto &entry = entries[i];
        if (entry.hash < 0)
            continue;
        if (entry.slot < 0 || entry.slot > 5)
            return {};
        ++live;
        if (entry.slot != scopeSlot)
            continue;
        if (found || !entry.id)
            return {};
        found = true;
        id = entry.id;
    }
    if (live != count - freeCount)
        return {};
    return {true, id};
}
struct OpticProfile {
    std::uint32_t id;
    float factor;
    const char *name;
};
// IDs are correlated with user-labelled paired current-equipment snapshots.
// Zero preserves the fixed ADS settings for red dots with no stated factor;
// it does not claim those optics have a measured physical magnification of 0.
inline constexpr OpticProfile opticProfiles[] = {
    {306101011u, 0.f, "红点瞄具"},         {306101013u, 0.f, "优质红点瞄具"},
    {306101015u, 0.f, "特种红点瞄具"},     {306101023u, 2.f, "优质倍率红点瞄具"},
    {306101025u, 2.f, "特种倍率红点瞄具"}, {306103014u, 8.f, "优质8倍精准瞄具"},
    {306103015u, 8.f, "特种8倍精准瞄具"}};
inline const OpticProfile *verifiedOpticProfile(std::uint32_t id) {
    for (const auto &profile : opticProfiles)
        if (profile.id == id)
            return &profile;
    return nullptr;
}
inline float verifiedOpticMagnification(std::uint32_t id) {
    auto profile = verifiedOpticProfile(id);
    return profile ? profile->factor : 0.f;
}
inline float effectiveADSMagnification(bool automatic, OpticSample sample) {
    return automatic && sample.valid ? verifiedOpticMagnification(sample.scopeId) : 0.f;
}
} // namespace shoulder
