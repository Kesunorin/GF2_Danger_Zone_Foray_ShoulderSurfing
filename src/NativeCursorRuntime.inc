// Separate ownership for F10 in original view; the shoulder camera's Saved
// state does not exist there. Only hold it for this panel session.
struct SettingsCursorRestore {
    bool pending{}, shown{};
    int lockMode{};
} settingsCursorRestore;
void resetSettingsCursor() {
    settingsCursorRestore = {};
}
void changeCapture(bool request) {
    captureGate.request(request && enabled && foreground() && !uiBlocked);
    mouseValid = false;
    if (!captured) {
        resetDodgeDirection();
        restoreCrosshair();
        resetVoiceDirection(false, false);
        resetNativePlayerBars(false);
        resetNativeHealth(false);
        resetNativeInteraction(false);
    }
    if (saved.cameraHandle && foreground())
        setCursor(!captured, captured ? 2 : 0);
}
void syncCapture() {
    uiBlocked = gameUI();
    bool was = captured;
    bool resume = cfg.autoReleaseUI && cfg.autoResumeUI;
    bool escapeHeld = (GetAsyncKeyState(VK_ESCAPE) & 0x8000) != 0;
    bool held = (GetAsyncKeyState(VK_LBUTTON) & 0x8000) || (GetAsyncKeyState(VK_RBUTTON) & 0x8000) ||
                escapeHeld || (GetAsyncKeyState(VK_MENU) & 0x8000) || (GetAsyncKeyState(VK_TAB) & 0x8000) ||
                (cfg.captureKey && (GetAsyncKeyState(cfg.captureKey) & 0x8000)) ||
                (cfg.settingsKey && (GetAsyncKeyState(cfg.settingsKey) & 0x8000));
    if (escapeHeld && enabled)
        captureGate.suspend(resume);
    auto panel = settings::panel.load();
    bool settingsOpen = (panel && IsWindowVisible(panel)) || settings::opening.load();
    if (settingsOpen)
        setCursor(true, 0);
    bool focused = foreground();
    captureGate.update(enabled, focused, uiBlocked || settingsOpen, resume, held, GetTickCount64() * .001);
    // Native radial input may update the cursor again after the first open frame.
    // Keep it unlocked for the complete UI session; inputTick then skips recentering.
    if (uiBlocked && saved.cameraHandle && focused)
        setCursor(true, 0);
    if (settingsCursorRestore.pending && !settingsOpen) {
        if (enabled || saved.cameraHandle)
            resetSettingsCursor();
        else if (focused) {
            // A native menu owns the cursor now. Do not restore an older hidden state
            // over that request, even if automatic UI release was disabled in settings.
            if (nativeUIRequestsCursor())
                setCursor(true, 0);
            else
                setCursor(settingsCursorRestore.shown, settingsCursorRestore.lockMode);
            resetSettingsCursor();
        }
    }
    if (uiBlocked != lastUiBlocked) {
        logDebug("Game UI cursor request=%d; capture=%d pendingResume=%d", uiBlocked, captured,
            captureGate.resumePending);
        lastUiBlocked = uiBlocked;
        if (uiBlocked && saved.cameraHandle && foreground())
            setCursor(true, 0);
    }
    if (was != captured) {
        mouseValid = false;
        if (!captured) {
            resetDodgeDirection();
            restoreCrosshair();
            resetVoiceDirection(false, false);
            resetNativePlayerBars(false);
            resetNativeHealth(false);
            resetNativeInteraction(false);
        }
        if (saved.cameraHandle && (!captured || foreground()))
            setCursor(!captured, captured ? 2 : 0);
        logDebug("Capture gate: capture=%d ui=%d focus=%d pendingResume=%d", captured, uiBlocked, focused,
            captureGate.resumePending);
    }
}
void showSettings() {
    if (!enabled && !saved.cameraHandle && !settingsCursorRestore.pending) {
        bool shown = api.value<bool>(getCursorVisible);
        int mode = api.value<int>(getCursorLock);
        settingsCursorRestore = {true, shown, mode};
    }
    auto owner = GetForegroundWindow();
    captureGate.suspend(cfg.autoReleaseUI && cfg.autoResumeUI && enabled);
    mouseValid = false;
    resetDodgeDirection();
    restoreCrosshair();
    resetVoiceDirection(false, false);
    resetNativePlayerBars(false);
    resetNativeHealth(false);
    resetNativeInteraction(false);
    // Settings needs a visible cursor even when original view has no saved camera.
    setCursor(true, 0);
    settings::toggleKey.store(cfg.settingsKey);
    settings::toggle(ownModule, iniPath, owner, [] { pendingCommand.store(4); });
    log("Settings panel requested; cursor visible, pendingResume=%d", captureGate.resumePending);
}
