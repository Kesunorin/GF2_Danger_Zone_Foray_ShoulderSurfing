#pragma once
#include <cmath>
namespace shoulder {
// UI, settings, Escape and focus loss temporarily suspend an existing capture.
// Manual release and scene reset revoke the resume permission.
struct CursorGate {
    bool active{}, resumePending{};
    double clearSince = -1;
    void release() {
        active = false;
        resumePending = false;
        clearSince = -1;
    }
    void reset() {
        release();
    }
    void request(bool allowed) {
        release();
        active = allowed;
    }
    void suspend(bool autoResume) {
        resumePending = autoResume && (active || resumePending);
        active = false;
        clearSince = -1;
    }
    void update(bool enabled, bool focused, bool ui, bool autoResume, bool buttonsHeld, double now) {
        if (!enabled || !std::isfinite(now)) {
            reset();
            return;
        }
        if (!autoResume) {
            resumePending = false;
            clearSince = -1;
        }
        if (!focused || ui) {
            suspend(autoResume);
            return;
        }
        if (!resumePending)
            return;
        // A UI click held while closing must not immediately become a fire input.
        if (buttonsHeld) {
            clearSince = -1;
            return;
        }
        if (clearSince < 0 || now < clearSince) {
            clearSince = now;
            return;
        }
        if (now - clearSince >= .15) {
            active = true;
            resumePending = false;
            clearSince = -1;
        }
    }
};
// These raw bytes were correlated with the user's backpack/pause sequence.
// No inference is made from the unused primary cursor field or mask count.
inline bool sampledUiRequestsCursor(unsigned char first, unsigned char second) {
    return first == 1 && second == 1;
}
} // namespace shoulder
