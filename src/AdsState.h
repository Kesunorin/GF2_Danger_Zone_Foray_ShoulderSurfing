#pragma once
namespace shoulder {
// The sampled Crosshair bool is authoritative when available. A held mouse
// button is only a fallback: the game can reject or delay an ADS request.
struct AdsState {
    bool available{}, active{};
    void reset() {
        available = false;
        active = false;
    }
    void sample(bool valid, unsigned char raw) {
        available = valid && raw <= 1;
        active = available && raw == 1;
    }
    bool engaged(bool useGame, bool captured, bool focused, bool rightHeld) const {
        if (!captured || !focused)
            return false;
        return useGame && available ? active : rightHeld;
    }
};
} // namespace shoulder
