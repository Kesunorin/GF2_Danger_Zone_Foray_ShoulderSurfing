#include "Runtime.h"
#include "RuntimeEntry.h"
#include "RuntimeLog.h"
#include <tlhelp32.h>
#include "CameraMath.h"
#include "Geometry.h"
#include "CameraCollision.h"
#include "CameraHelperVolume.h"
#include "RoofVisibility.h"
#include "GpuMesh.h"
#include "KeyBindings.h"
#include "SettingsPanel.h"
#include "CursorGate.h"
#include "AdsState.h"
#include "AdsMagnification.h"
#include "AdsZoom.h"
#include "OpticState.h"
#include "NativeRayConvergence.h"
#include "ThrowTarget.h"
#include "VoiceDirection.h"
#include "HudPosition.h"
#include "DodgeDirection.h"
#include "StreamingDistance.h"
#include "../vendor/minhook/include/MinHook.h"
#include <cstdio>
#include <cstdarg>
#include <atomic>
#include <mutex>
#include <string>
#include <vector>
#include <unordered_map>
#include <unordered_set>
#include <memory>
#include <array>
#include <cwctype>
using namespace shoulder;
using namespace runtime;
namespace {
Api api;
HMODULE ownModule{};
std::wstring directory, iniPath, logPath;
RuntimeLog runtimeLog;
std::atomic<bool> initialized{}, faulted{};
thread_local bool inMod{};
void log(const char *format, ...) {
    va_list args;
    va_start(args, format);
    runtimeLog.write(format, args);
    va_end(args);
}
void logDebug(const char *format, ...) {
    if (!runtimeLog.debug())
        return;
    va_list args;
    va_start(args, format);
    runtimeLog.write(format, args, RuntimeLog::Level::Debug);
    va_end(args);
}
void logError(const char *format, ...) {
    va_list args;
    va_start(args, format);
    runtimeLog.write(format, args, RuntimeLog::Level::Error);
    va_end(args);
}
// The launcher already prevents mixed versions; retain the DLL-side guard for
// callers that use LoadLibrary directly, without a growing version-name list.
void rejectOtherModVersions() {
    auto snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE, GetCurrentProcessId());
    if (snapshot == INVALID_HANDLE_VALUE)
        throw std::runtime_error("Cannot inspect loaded mod versions");
    MODULEENTRY32W module{sizeof(module)};
    bool other = false;
    constexpr wchar_t prefix[] = L"GF2ShoulderSurfing";
    if (Module32FirstW(snapshot, &module))
        do {
            if (module.hModule != ownModule && _wcsnicmp(module.szModule, prefix, std::size(prefix) - 1) == 0)
                other = true;
        } while (Module32NextW(snapshot, &module));
    CloseHandle(snapshot);
    if (other)
        throw std::runtime_error("Another mod version is loaded; restart the game before loading this build");
}
struct Config {
    float distance = 3.2f, height = 1.55f, side = .65f, fov = 70, adsFov = 50, sensitivity = .16f,
          adsSensitivity = .16f, renderDistance = 0, minPitch = -45, maxPitch = 65, radius = .18f,
          smoothing = 16, streamingMultiplier = 0;
    int collisionMask = -5;
    bool invert = false, collision = true, aim = true, turn = true;
    bool autoReleaseUI = true, autoResumeUI = true, gameADS = true;
    bool autoOptic = true, renderCollision = true;
    bool gameSensitivity = true, healthFix = true, interactFix = true, playerBarsFix = true, dodgeFix = true,
         linearADSZoom = true, laserConvergence = true;
    float sensitivityMultiplier = 1, adsSensitivityMultiplier = 1;
    int toggleKey = VK_F7, swapKey = VK_F6, captureKey = VK_F8, reloadKey = VK_F9, leftKey = 0, rightKey = 0,
        settingsKey = VK_F10;
} cfg;
void resetInputState();
void resetSettingsCursor();
void resetDodgeDirection();
void resetNativeLaser();
void resetNativeThrowInput();
void resetOpticState();
void resetGameSensitivity();
void resetNativeAdsZoom();
void resetVoiceDirection(bool original = false, bool retry = false);
void resetNativeHealth(bool retry = false);
void resetNativePlayerBars(bool retry = false);
void resetNativeInteraction(bool retry = false);
void resetNativeTracking(bool retry = false);
void resetNativeStreaming(bool retry = false);
void resetNativeRoof(bool retry = false);
void resetNativeAttackProjection(bool retry = false);
void streamingStatus(int state, float nearRadius = 0, float preloadRadius = 0) {
    settings::streamingNear.store(nearRadius);
    settings::streamingPreload.store(preloadRadius);
    settings::streamingState.store(state);
}
Obj playerIK(Obj root);
float numberSetting(const wchar_t *section, const wchar_t *key, float fallback, float lo, float hi) {
    wchar_t text[80], def[80];
    swprintf_s(def, L"%.4f", fallback);
    GetPrivateProfileStringW(section, key, def, text, 80, iniPath.c_str());
    wchar_t *end{};
    float v = wcstof(text, &end);
    return end == text || !std::isfinite(v) ? fallback : std::clamp(v, lo, hi);
}
float setting(const wchar_t *key, float fallback, float lo, float hi) {
    return numberSetting(L"Camera", key, fallback, lo, hi);
}
int keySetting(const wchar_t *key, const wchar_t *fallback) {
    wchar_t text[80];
    GetPrivateProfileStringW(L"Keys", key, fallback, text, 80, iniPath.c_str());
    int value = parseKey(text);
    if (value < 0 || value == VK_ESCAPE) {
        log("Invalid/reserved binding %ls=%ls; using %ls", key, text, fallback);
        return parseKey(fallback);
    }
    return value;
}
void loadConfig() {
    std::lock_guard<std::mutex> configLock(settings::iniMutex);
    runtimeLog.setDebug(GetPrivateProfileIntW(L"Diagnostics", L"DebugLogging", 0, iniPath.c_str()) != 0);
    log("Diagnostic logging: debug=%d; initialization, READY and errors remain enabled", runtimeLog.debug());
    cfg.distance = setting(L"Distance", 3.2f, .5f, 10);
    cfg.height = setting(L"Height", 1.55f, .2f, 4);
    cfg.side = setting(L"ShoulderOffset", .65f, 0, 2);
    cfg.fov = setting(L"FOV", 70, 25, 110);
    cfg.adsFov = setting(L"ADSFOV", 50, 15, 100);
    cfg.sensitivity = setting(L"Sensitivity", .16f, .01f, 2);
    cfg.adsSensitivity = setting(L"ADSSensitivity", cfg.sensitivity, .01f, 2);
    cfg.renderDistance = setting(L"RenderDistance", 0, 0, 5000);
    if (cfg.renderDistance > 0)
        cfg.renderDistance = std::max(25.f, cfg.renderDistance);
    cfg.streamingMultiplier = setting(L"StreamingDistanceMultiplier", 0, 0, 8);
    if (cfg.streamingMultiplier > 0)
        cfg.streamingMultiplier = std::max(1.f, cfg.streamingMultiplier);
    cfg.minPitch = setting(L"MinPitch", -45, -80, 0);
    cfg.maxPitch = setting(L"MaxPitch", 65, 1, 80);
    cfg.radius = setting(L"CollisionRadius", .18f, .05f, .5f);
    cfg.smoothing = setting(L"Smoothing", 16, 0, 50);
    cfg.invert = GetPrivateProfileIntW(L"Camera", L"InvertY", 0, iniPath.c_str()) != 0;
    cfg.collision = GetPrivateProfileIntW(L"Camera", L"Collision", 1, iniPath.c_str()) != 0;
    cfg.collisionMask = GetPrivateProfileIntW(L"Camera", L"CollisionMask", -5, iniPath.c_str());
    cfg.aim = GetPrivateProfileIntW(L"Gameplay", L"CenterAim", 1, iniPath.c_str()) != 0;
    cfg.turn = GetPrivateProfileIntW(L"Gameplay", L"TurnCharacter", 1, iniPath.c_str()) != 0;
    cfg.gameADS = GetPrivateProfileIntW(L"Gameplay", L"UseGameADS", 1, iniPath.c_str()) != 0;
    cfg.linearADSZoom = GetPrivateProfileIntW(L"Camera", L"LinearADSZoom", 1, iniPath.c_str()) != 0;
    cfg.laserConvergence =
        GetPrivateProfileIntW(L"Gameplay", L"NativeLaserConvergence", 1, iniPath.c_str()) != 0;
    cfg.dodgeFix = GetPrivateProfileIntW(L"Gameplay", L"CameraRelativeDodge", 1, iniPath.c_str()) != 0;
    cfg.playerBarsFix =
        GetPrivateProfileIntW(L"Gameplay", L"CameraRelativePlayerBars", 1, iniPath.c_str()) != 0;
    cfg.healthFix = GetPrivateProfileIntW(L"Gameplay", L"CameraRelativeHealthBars", 1, iniPath.c_str()) != 0;
    cfg.interactFix =
        GetPrivateProfileIntW(L"Gameplay", L"CameraRelativeInteractMarkers", 1, iniPath.c_str()) != 0;
    cfg.autoOptic = GetPrivateProfileIntW(L"Camera", L"AutoOpticMagnification", 1, iniPath.c_str()) != 0;
    cfg.renderCollision =
        GetPrivateProfileIntW(L"Camera", L"RenderGeometryCollision", 1, iniPath.c_str()) != 0;
    cfg.gameSensitivity = GetPrivateProfileIntW(L"Camera", L"UseGameSensitivity", 1, iniPath.c_str()) != 0;
    cfg.sensitivityMultiplier = setting(L"SensitivityMultiplier", 1, .1f, 5);
    cfg.adsSensitivityMultiplier = setting(L"ADSSensitivityMultiplier", 1, .1f, 5);
    cfg.autoReleaseUI = GetPrivateProfileIntW(L"Input", L"AutoReleaseUI", 1, iniPath.c_str()) != 0;
    cfg.autoResumeUI = GetPrivateProfileIntW(L"Input", L"AutoResumeUI", 1, iniPath.c_str()) != 0;
    cfg.toggleKey = keySetting(L"ToggleCamera", L"F7");
    cfg.captureKey = keySetting(L"CaptureMouse", L"F8");
    cfg.reloadKey = keySetting(L"ReloadConfig", L"F9");
    cfg.swapKey = keySetting(L"SwapShoulder", L"F6");
    cfg.leftKey = keySetting(L"LeftShoulder", L"NONE");
    cfg.rightKey = keySetting(L"RightShoulder", L"NONE");
    cfg.settingsKey = keySetting(L"SettingsPanel", L"F10");
    bool used[256]{};
    for (auto key : {&cfg.toggleKey, &cfg.captureKey, &cfg.reloadKey, &cfg.settingsKey, &cfg.swapKey,
                     &cfg.leftKey, &cfg.rightKey}) {
        if (*key && used[*key]) {
            log("Duplicate binding VK=%d disabled; priority toggle/capture/reload/settings/swap/left/right",
                *key);
            *key = 0;
        } else if (*key)
            used[*key] = true;
    }
    resetInputState();
    resetDodgeDirection();
    resetNativeLaser();
    resetNativeThrowInput();
    resetOpticState();
    resetGameSensitivity();
    resetNativeAdsZoom();
    resetVoiceDirection(false, true);
    resetNativePlayerBars(true);
    resetNativeHealth(true);
    resetNativeInteraction(true);
    resetNativeTracking(true);
    resetNativeStreaming(true);
    resetNativeRoof(true);
    resetNativeAttackProjection(true);
    log("Config: distance=%.2f height=%.2f side=%.2f fov=%.1f sens=%.3f adsSens=%.3f far=%.1f centerAim=%d turn=%d aimOrigin=reference",
        cfg.distance, cfg.height, cfg.side, cfg.fov, cfg.sensitivity, cfg.adsSensitivity, cfg.renderDistance,
        cfg.aim, cfg.turn);
    log("Bindings VK: toggle=%d capture=%d reload=%d settings=%d swap=%d left=%d right=%d; Esc suspends; F8 manually releases",
        cfg.toggleKey, cfg.captureKey, cfg.reloadKey, cfg.settingsKey, cfg.swapKey, cfg.leftKey,
        cfg.rightKey);
    log("Input gate: autoReleaseUI=%d autoResumeUI=%d; resume covers menus/Escape/focus; manual capture release cancels",
        cfg.autoReleaseUI, cfg.autoResumeUI);
    log("ADS: useGameState=%d; autoOptic=%d; seven verified optic IDs, red dots use fixed ADS, magnified optics use 2/8; unknown/absent use fallback",
        cfg.gameADS, cfg.autoOptic);
    log("Linear ADS zoom=%d; native normalized remaining time drives projection scale; no weapon duration constant",
        cfg.linearADSZoom);
    log("Camera render geometry collision=%d; swept sphere radius=%.3f; shared mesh cache and readback budget",
        cfg.renderCollision, cfg.radius);
    log("Native building loading radius factor=%.2f; 0/1 original, >1 scales linked radii; independent of camera far clip",
        cfg.streamingMultiplier);
    log("Native PC crosshair sensitivity link=%d normalMultiplier=%.2f adsMultiplier=%.2f",
        cfg.gameSensitivity, cfg.sensitivityMultiplier, cfg.adsSensitivityMultiplier);
}
Class rootClass{}, cameraClass{}, transformClass{};
Field rootInstance{}, rootCamera{}, rootPlayer{};
Field cursorUI{};
bool uiBlocked{}, lastUiBlocked{};
Field ikEntity{}, entityAim{}, entityWeapon{};
Method aimUpdate{}, aimOrigin{}, aimDirection{}, entityTurn{}, turnRotation{};
int crosshairSource{}, clientSide{};
Method findObjects{}, getCanvasParent{}, getCanvasCamera{}, worldToScreen{}, screenToWorld{};
Obj crosshairType{}, canvasType{};
unsigned crosshairTypeHandle{}, canvasTypeHandle{}, crosshairHandle{};
Vec3 crosshairSaved{};
bool crosshairMoved{};
int lastCrosshairSearch = -600, lastAimLog = -600;
Class crosshairClass{};
Field crosshairADS{};
AdsState adsState;
Class opticDataClass{}, opticLoadoutClass{}, opticDictionaryClass{};
Field opticEntityData{}, opticDataLoadout{}, opticAttachments{}, opticCount{}, opticFreeCount{},
    opticVersion{}, opticEntries{};
bool opticBound{};
int opticScopeSlot = 2;
OpticSample opticSample;
ULONGLONG lastOpticPoll{};
void resetOpticState() {
    opticSample = {};
    lastOpticPoll = 0;
}
float effectiveMagnification() {
    return effectiveADSMagnification(cfg.autoOptic, opticSample);
}
Method rendererBounds{}, rendererEnabled{}, objectActive{}, getGameObject{}, getLayer{}, getWorldMatrix{},
    getMeshComponent{}, sharedMesh{}, meshVertices{}, vertexBuffer{}, indexBuffer{}, meshIndexFormat{},
    meshAttributes{};
Obj rendererType{}, meshFilterType{};
unsigned rendererTypeHandle{}, meshFilterTypeHandle{}, rendererScanHandle{};
size_t rendererScanIndex{}, rendererScanCount{};
bool renderScanStarted{};
ULONGLONG nextGeometryScan{};
std::unordered_set<Obj> indexedRenderers;
int lastMeshLoadFrame = -1;
std::wstring meshHitName;
float meshHitDistance{};
struct MeshData {
    unsigned handle{};
    bool valid{};
    std::vector<Vec3> vertices;
    std::vector<unsigned> indices;
};
struct RenderData {
    unsigned handle{};
    Bounds bounds{};
    Matrix world{};
    std::wstring name;
    int layer{};
    bool attempted{};
    std::shared_ptr<MeshData> data;
};
std::vector<RenderData> renderGeometry;
std::unordered_map<Obj, std::shared_ptr<MeshData>> meshCache;
int lastCollisionLog = -600, lastCollisionBudgetLog = -600;
ULONGLONG lastCameraHelperLog{}, lastPhysicsCollisionLog{};
ULONGLONG lastAimHelperLog{};
unsigned postEffectVolumeTypeHandle{}, postEffectRendererTypeHandle{};
bool postEffectFilterFailed{};
ULONGLONG lastPostEffectCameraLog{}, lastPostEffectAimLog{};
size_t collisionTrianglesThisFrame{};
int collisionBudgetFrame = -1;
bool renderSupported{}, graphicsChecked{};
Method graphicsDeviceType{};
size_t meshCacheBytes{};
Class aimConfigClass{};
Field nativeAimRuntime{}, nativePcSensitivity{};
bool nativeSensitivityBound{}, nativeSensitivityValid{};
float nativeSensitivity = 1;
ULONGLONG lastNativeSensitivityPoll{};
void resetGameSensitivity() {
    nativeSensitivity = 1;
    nativeSensitivityValid = false;
    lastNativeSensitivityPoll = 0;
}
Method lateUpdate{}, rootUpdate{}, rootDestroy{};
Method getTransform{}, componentTransform{}, getParent{}, getChildComponent{}, getMain{}, getPosition{},
    setPosition{}, getRotation{}, setRotation{}, getFov{}, setFov{}, getOrtho{}, setOrtho{}, getNear{},
    setNear{}, getFar{}, setFar{}, isAlive{}, getFrame{}, getDelta{}, getWidth{}, getHeight{},
    cursorVisible{}, cursorLock{}, getCursorVisible{}, getCursorLock{}, sphereCast{}, hitCollider{},
    isChild{}, getName{};
Obj cameraType{}, entityType{};
unsigned cameraTypeHandle{}, entityTypeHandle{}, hitArrayHandle{};
int hitStride{};
using Invoker = Obj (*)(void *, Method, Obj, void **);
Invoker originalInvoker{};
CursorGate captureGate;
const bool &captured = captureGate.active;
bool enabled = false, leftShoulder = false;
float yaw{}, pitch = 12;
POINT lastMouse{};
bool mouseValid{};
int lastInputFrame = -1, lastCameraFrame = -1, lastStatusFrame = -1;
bool keyWas[256]{};
std::atomic<int> pendingCommand{};
unsigned rootHandle{};
std::wstring nameOf(Obj obj) {
    auto text = api.call(getName, obj);
    if (!text)
        return {};
    return std::wstring(api.stringChars(text), api.stringLength(text));
}
struct Saved {
    unsigned cameraHandle{};
    Vec3 position;
    Quat rotation;
    float fov{}, nearClip{}, farClip{};
    bool ortho{};
    bool cursorShown = true;
    int lockMode{};
    Vec3 smoothed;
    bool first = true;
} saved;
bool alive(Obj obj) {
    if (!obj)
        return false;
    void *args[] = {obj};
    return api.value<bool>(isAlive, nullptr, args);
}
template <typename T> void set(Method m, Obj obj, T value) {
    void *args[] = {&value};
    api.call(m, obj, args);
}
void setCursor(bool show, int mode) {
    set(cursorVisible, nullptr, show);
    set(cursorLock, nullptr, mode);
}
void clearGeometry() {
    for (auto &r : renderGeometry)
        api.handleFree(r.handle);
    renderGeometry.clear();
    indexedRenderers.clear();
    for (auto &item : meshCache)
        api.handleFree(item.second->handle);
    meshCache.clear();
    meshCacheBytes = 0;
    if (rendererScanHandle)
        api.handleFree(rendererScanHandle);
    rendererScanHandle = 0;
    rendererScanCount = rendererScanIndex = 0;
    renderScanStarted = false;
    nextGeometryScan = 0;
    lastMeshLoadFrame = -1;
}
void restoreCrosshair() {
    if (crosshairHandle) {
        auto cross = api.handleTarget(crosshairHandle);
        if (crosshairMoved && alive(cross))
            set(setPosition, api.call(componentTransform, cross), crosshairSaved);
        api.handleFree(crosshairHandle);
        crosshairHandle = 0;
    }
    crosshairMoved = false;
    lastCrosshairSearch = -600;
    adsState.reset();
    resetNativeLaser();
    resetNativeAdsZoom();
    resetNativeThrowInput();
}
void restore() {
    resetSettingsCursor();
    resetDodgeDirection();
    resetNativeInteraction(false);
    resetNativeTracking(false);
    resetNativeStreaming(false);
    resetNativeRoof(false);
    resetNativePlayerBars(false);
    resetNativeHealth(false);
    resetVoiceDirection(true, false);
    captureGate.reset();
    mouseValid = false;
    resetOpticState();
    restoreCrosshair();
    if (saved.cameraHandle) {
        auto cam = api.handleTarget(saved.cameraHandle);
        if (alive(cam)) {
            auto tr = api.call(componentTransform, cam);
            set(setPosition, tr, saved.position);
            set(setRotation, tr, saved.rotation);
            set(setFov, cam, saved.fov);
            set(setNear, cam, saved.nearClip);
            set(setFar, cam, saved.farClip);
            set(setOrtho, cam, saved.ortho);
        }
        api.handleFree(saved.cameraHandle);
        saved.cameraHandle = 0;
        setCursor(saved.cursorShown, saved.lockMode);
        log("Original camera and cursor restored");
    }
}
bool foreground() {
    auto window = GetForegroundWindow();
    DWORD pid{};
    GetWindowThreadProcessId(window, &pid);
    return pid == GetCurrentProcessId() && !settings::ownsWindow(window);
}
#include "NativeRadialCursorRuntime.inc"
bool nativeUIRequestsCursor() {
    if (nativeRadialRequestsCursor())
        return true;
    if (!cursorUI)
        return false;
    auto raw = api.global<std::array<unsigned char, 16>>(cursorUI);
    return sampledUiRequestsCursor(raw[0], raw[1]);
}
bool gameUI() {
    return cfg.autoReleaseUI && nativeUIRequestsCursor();
}
#include "NativeCursorRuntime.inc"
void resetInputState() {
    for (int k : {cfg.toggleKey, cfg.swapKey, cfg.captureKey, cfg.reloadKey, cfg.leftKey, cfg.rightKey,
                  cfg.settingsKey, VK_ESCAPE})
        if (k)
            keyWas[k] = (GetAsyncKeyState(k) & 0x8000) != 0;
    mouseValid = false;
}
bool pressed(int key) {
    if (!key)
        return false;
    bool now = (GetAsyncKeyState(key) & 0x8000) != 0;
    bool edge = now && !keyWas[key];
    keyWas[key] = now;
    return edge;
}
void updateAdsState();
void updateOpticState(Obj root);
void updateGameSensitivity();
bool zoomHeld() {
    return adsState.engaged(cfg.gameADS, captured, foreground(),
                            (GetAsyncKeyState(VK_RBUTTON) & 0x8000) != 0);
}
Obj currentRoot() {
    auto root = api.global<Obj>(rootInstance);
    return alive(root) ? root : nullptr;
}
void toggleCamera() {
    enabled = !enabled;
    if (!enabled)
        restore();
    else
        changeCapture(true);
    log("Camera %s", enabled ? "enabled" : "disabled");
}
void commandTick() {
    switch (pendingCommand.exchange(0)) {
    case 1:
        if (!enabled)
            toggleCamera();
        break;
    case 2:
        if (enabled)
            toggleCamera();
        break;
    case 3:
        leftShoulder = !leftShoulder;
        log("Shoulder: %s", leftShoulder ? "left" : "right");
        break;
    case 4:
        loadConfig();
        break;
    case 5:
        changeCapture(false);
        break;
    case 6:
        if (enabled)
            changeCapture(true);
        break;
    }
}
#include "NativeVoiceRuntime.inc"
#include "NativeHealthRuntime.inc"
#include "NativePlayerBarsRuntime.inc"
#include "NativeInteractionRuntime.inc"
#include "NativeTrackingRuntime.inc"
#include "NativeAttackProjectionRuntime.inc"
#include "NativeStreamingRuntime.inc"
#include "NativeRoofRuntime.inc"
void inputTick() {
    int frame = api.value<int>(getFrame);
    if (frame == lastInputFrame)
        return;
    lastInputFrame = frame;
    auto root = currentRoot();
    auto previousRoot = rootHandle ? api.handleTarget(rootHandle) : nullptr;
    if (previousRoot != root) {
        restore();
        clearGeometry();
        enabled = false;
        if (rootHandle)
            api.handleFree(rootHandle);
        rootHandle = root ? api.handleNew(root, false) : 0;
        log("DarKov scene changed; camera reset");
    }
    if (!root)
        return;
    syncCapture();
    commandTick();
    bool focus = foreground();
    if (!focus) {
        mouseValid = false;
        resetInputState();
        return;
    }
    bool escape = pressed(VK_ESCAPE);
    if (pressed(cfg.reloadKey)) {
        loadConfig();
        syncCapture();
        return;
    }
    if (pressed(cfg.toggleKey))
        toggleCamera();
    if (pressed(cfg.settingsKey)) {
        showSettings();
        return;
    }
    if (pressed(cfg.swapKey)) {
        leftShoulder = !leftShoulder;
        log("Shoulder: %s", leftShoulder ? "left" : "right");
    }
    if (pressed(cfg.leftKey)) {
        leftShoulder = true;
        log("Shoulder: left");
    }
    if (pressed(cfg.rightKey)) {
        leftShoulder = false;
        log("Shoulder: right");
    }
    if (pressed(cfg.captureKey) && enabled) {
        changeCapture(!captured && !captureGate.resumePending);
        log("Mouse capture %s (UI request=%d)", captured ? "enabled" : "released", uiBlocked);
    }
    if (escape) {
        syncCapture();
        log("Escape: suspended cursor; autoResume=%d", cfg.autoReleaseUI && cfg.autoResumeUI);
    }
    if (!enabled || !captured)
        return;
    updateOpticState(root);
    updateGameSensitivity();
    updateAdsState();
    // Mouse is confined (not locked); measure movement before recentering.
    HWND window = GetForegroundWindow();
    RECT client{};
    POINT center{};
    if (!GetClientRect(window, &client))
        return;
    center = {(client.right - client.left) / 2, (client.bottom - client.top) / 2};
    ClientToScreen(window, &center);
    POINT p;
    if (!GetCursorPos(&p))
        return;
    float nativeFactor = cfg.gameSensitivity && nativeSensitivityValid ? nativeSensitivity : 1.f;
    float sensitivity = zoomHeld()
                            ? magnifiedADS(cfg.fov, cfg.adsFov,
                                           cfg.adsSensitivity * cfg.adsSensitivityMultiplier * nativeFactor,
                                           effectiveMagnification())
                                  .sensitivity
                            : cfg.sensitivity * cfg.sensitivityMultiplier * nativeFactor;
    if (mouseValid) {
        yaw = std::remainder(yaw + (p.x - lastMouse.x) * sensitivity, 360.f);
        pitch = std::clamp(pitch + (p.y - lastMouse.y) * sensitivity * (cfg.invert ? -1.f : 1.f),
                           cfg.minPitch, cfg.maxPitch);
    }
    SetCursorPos(center.x, center.y);
    lastMouse = center;
    mouseValid = true;
}
Obj playerIK(Obj root) {
    auto playerRoot = api.read<Obj>(root, rootPlayer);
    if (!alive(playerRoot))
        return nullptr;
    bool includeInactive = false;
    void *args[] = {entityType, &includeInactive};
    auto ik = api.call(getChildComponent, playerRoot, args);
    return alive(ik) ? ik : nullptr;
}
Class opticFieldType(Field f) {
    return f ? api.classFromType(*reinterpret_cast<const void **>(static_cast<char *>(f) + 8)) : nullptr;
}
Field checkedOpticField(Class c, const char *name, bool reference, const char *primitive = nullptr) {
    auto f = c ? api.field(c, name) : nullptr;
    if (!f || api.fieldFlags(f) & 16)
        throw std::runtime_error("Optic instance field unavailable");
    int offset{};
    memcpy(&offset, static_cast<char *>(f) + 24, 4);
    auto type = opticFieldType(f);
    if (offset < 16 || !type || api.isValueType(type) == reference ||
        (primitive && strcmp(api.className(type), primitive)))
        throw std::runtime_error("Optic field type/layout changed");
    return f;
}
void bindGameSensitivity() {
    try {
        aimConfigClass = api.find("Assembly-CSharp.dll", "GF.DarKov", "DarKovAimConfig");
        nativeAimRuntime = api.field(aimConfigClass, "_runtime");
        if (!nativeAimRuntime || !(api.fieldFlags(nativeAimRuntime) & 16) ||
            opticFieldType(nativeAimRuntime) != aimConfigClass)
            throw std::runtime_error("Native aim runtime field changed");
        nativePcSensitivity = checkedOpticField(aimConfigClass, "PcCrosshairSensitivity", false, "Single");
        nativeSensitivityBound = true;
    } catch (const std::exception &e) {
        nativeSensitivityBound = false;
        logError("Optional native sensitivity binding unavailable: %s", e.what());
    }
    log("Native sensitivity bound=%d", nativeSensitivityBound);
}
void updateGameSensitivity() {
    if (!cfg.gameSensitivity || !nativeSensitivityBound) {
        resetGameSensitivity();
        return;
    }
    auto now = GetTickCount64();
    if (lastNativeSensitivityPoll && now - lastNativeSensitivityPoll < 250)
        return;
    lastNativeSensitivityPoll = now;
    bool was = nativeSensitivityValid;
    float previous = nativeSensitivity;
    // DarKovAimConfig is a managed configuration object, not UnityEngine.Object.
    // Its static field roots it; Unity's object-liveness predicate is inapplicable.
    try {
        auto runtime = api.global<Obj>(nativeAimRuntime);
        nativeSensitivityValid = runtime && *static_cast<Class *>(runtime) == aimConfigClass;
        float value = nativeSensitivityValid ? api.read<float>(runtime, nativePcSensitivity) : 1.f;
        nativeSensitivityValid = nativeSensitivityValid && std::isfinite(value) && value > 0 && value <= 10;
        nativeSensitivity = nativeSensitivityValid ? value : 1.f;
    } catch (const std::exception &e) {
        nativeSensitivityValid = false;
        nativeSensitivity = 1;
        logError("Native sensitivity sample unavailable: %s; factor=1 fallback", e.what());
    }
    if (was != nativeSensitivityValid || previous != nativeSensitivity)
        logDebug("Native sensitivity sample valid=%d factor=%.3f; normal/ADS independent multipliers retained",
            nativeSensitivityValid, nativeSensitivity);
}
void bindOpticMetadata(Class entity) {
    try {
        opticEntityData = checkedOpticField(entity, "JCNHFBABNIJ", true);
        opticDataClass = opticFieldType(opticEntityData);
        opticDataLoadout = checkedOpticField(opticDataClass, "<JKEBJPPPFHE>k__BackingField", true);
        opticLoadoutClass = api.find("Assembly-CSharp.dll", "", "KJFMMGMOHFM");
        opticAttachments = checkedOpticField(opticLoadoutClass, "<PPMHKJOJACP>k__BackingField", true);
        opticDictionaryClass = opticFieldType(opticAttachments);
        opticCount = checkedOpticField(opticDictionaryClass, "count", false, "Int32");
        opticFreeCount = checkedOpticField(opticDictionaryClass, "freeCount", false, "Int32");
        opticVersion = checkedOpticField(opticDictionaryClass, "version", false, "Int32");
        opticEntries = checkedOpticField(opticDictionaryClass, "entries", true);
        auto slots = api.find("Assembly-CSharp.dll", "", "DBDMKLJHAAA");
        auto scope = api.field(slots, "Scope");
        if (!scope || !(api.fieldFlags(scope) & 16) || api.global<int>(scope) != 2)
            throw std::runtime_error("Optic Scope enum changed");
        auto arrayClass = opticFieldType(opticEntries), entryClass = api.elementClass(arrayClass);
        if (!entryClass || api.elementSize(arrayClass) != sizeof(OpticEntry))
            throw std::runtime_error("Optic entry stride changed");
        for (const auto &spec : std::array<std::pair<const char *, int>, 4>{
                 {{"hashCode", 0}, {"next", 4}, {"key", 8}, {"value", 12}}}) {
            auto f = checkedOpticField(entryClass, spec.first, false);
            int offset{};
            memcpy(&offset, static_cast<char *>(f) + 24, 4);
            auto type = opticFieldType(f);
            bool correctType =
                !strcmp(spec.first, "key")
                    ? type == slots
                    : !strcmp(api.className(type), !strcmp(spec.first, "value") ? "UInt32" : "Int32");
            if (offset != 16 + spec.second || !correctType)
                throw std::runtime_error("Optic entry metadata changed");
        }
        opticBound = true;
        log("Optic metadata bound: Scope=2, seven verified IDs; red dots=fixed ADS, magnified=2/8; fields only, 250ms polling on Unity callback");
    } catch (const std::exception &e) {
        opticBound = false;
        logError("Optic automatic detection unavailable: %s; fixed ADS FOV fallback remains", e.what());
    }
}
OpticSample sampleOptic(Obj root) {
    auto ik = playerIK(root);
    if (!ik)
        return {};
    auto entity = api.read<Obj>(ik, ikEntity);
    if (!entity)
        return {};
    auto data = api.read<Obj>(entity, opticEntityData);
    if (!data || *static_cast<Class *>(data) != opticDataClass)
        return {};
    auto loadout = api.read<Obj>(data, opticDataLoadout);
    if (!loadout || *static_cast<Class *>(loadout) != opticLoadoutClass)
        return {};
    auto dictionary = api.read<Obj>(loadout, opticAttachments);
    if (!dictionary || *static_cast<Class *>(dictionary) != opticDictionaryClass)
        return {};
    int version = api.read<int>(dictionary, opticVersion), count = api.read<int>(dictionary, opticCount),
        freeCount = api.read<int>(dictionary, opticFreeCount);
    if (count < 0 || count > 32 || freeCount < 0 || freeCount > count)
        return {};
    auto entries = api.read<Obj>(dictionary, opticEntries);
    if (!entries)
        return count == 0 && freeCount == 0 ? OpticSample{true, 0} : OpticSample{};
    if (*static_cast<Class *>(entries) != opticFieldType(opticEntries))
        return {};
    size_t capacity{};
    memcpy(&capacity, static_cast<char *>(entries) + 24, 8);
    if (capacity < static_cast<size_t>(count) || capacity > 256)
        return {};
    std::array<OpticEntry, 32> rows{};
    memcpy(rows.data(), static_cast<char *>(entries) + 32, static_cast<size_t>(count) * sizeof(OpticEntry));
    auto sample = decodeOpticEntries(rows.data(), count, freeCount, capacity, opticScopeSlot);
    if (version != api.read<int>(dictionary, opticVersion) ||
        loadout != api.read<Obj>(data, opticDataLoadout) ||
        dictionary != api.read<Obj>(loadout, opticAttachments))
        return {};
    return sample;
}
void updateOpticState(Obj root) {
    if (!cfg.autoOptic || !opticBound) {
        opticSample = {};
        return;
    }
    if (!foreground() || uiBlocked)
        return;
    auto now = GetTickCount64();
    if (lastOpticPoll && now - lastOpticPoll < 250)
        return;
    lastOpticPoll = now;
    auto previous = opticSample;
    opticSample = sampleOptic(root);
    if (previous.valid != opticSample.valid || previous.scopeId != opticSample.scopeId) {
        auto profile = opticSample.valid ? verifiedOpticProfile(opticSample.scopeId) : nullptr;
        auto view = magnifiedADS(cfg.fov, cfg.adsFov, cfg.adsSensitivity, effectiveMagnification());
        logDebug("Optic sample valid=%d scopeId=%u verifiedFactor=%.0f effective=%.0f fov=%.2f sens=%.4f recognized=%d optic=%s",
            opticSample.valid, opticSample.scopeId, verifiedOpticMagnification(opticSample.scopeId),
            effectiveMagnification(), view.fov, view.sensitivity, profile != nullptr,
            profile ? profile->name : "unknown/none");
    }
}
Obj sceneCamera(Obj root) {
    auto cameraRoot = api.read<Obj>(root, rootCamera);
    if (alive(cameraRoot)) {
        bool includeInactive = false;
        void *args[] = {cameraType, &includeInactive};
        auto cam = api.call(getChildComponent, cameraRoot, args);
        if (alive(cam))
            return cam;
    }
    auto cam = api.call(getMain);
    return alive(cam) ? cam : nullptr;
}
bool geometryExcluded(std::wstring name) {
    for (auto &c : name)
        c = static_cast<wchar_t>(std::towlower(c));
    for (auto exclude : {L"leaf", L"bush", L"grass", L"terrain", L"tile_", L"weapon", L"muzzle", L"fx_",
                         L"particle", L"fps_"})
        if (name.find(exclude) != std::wstring::npos)
            return true;
    return false;
}
bool geometryName(std::wstring name) {
    if (geometryExcluded(name))
        return false;
    for (auto &c : name)
        c = static_cast<wchar_t>(std::towlower(c));
    return name.find(L"s_es_") != std::wstring::npos || name.find(L"s_slg_") != std::wstring::npos ||
           name.find(L"wall") != std::wstring::npos ||
           name.find(L"building") != std::wstring::npos || name.find(L"container") != std::wstring::npos;
}
void geometryScan() {
    if (!renderSupported)
        return;
    if (!renderScanStarted) {
        if (GetTickCount64() < nextGeometryScan)
            return;
        // Buildings may stream in after the first scene scan. Drop destroyed
        // renderers and periodically discover new ones; retain existing data.
        for (auto it = renderGeometry.begin(); it != renderGeometry.end();) {
            auto renderer = api.handleTarget(it->handle);
            if (!alive(renderer)) {
                indexedRenderers.erase(renderer);
                api.handleFree(it->handle);
                it = renderGeometry.erase(it);
            } else
                ++it;
        }
        renderScanStarted = true;
        void *args[] = {rendererType};
        auto objects = api.call(findObjects, nullptr, args);
        if (!objects) {
            renderScanStarted = false;
            nextGeometryScan = GetTickCount64() + 10000;
            return;
        }
        memcpy(&rendererScanCount, static_cast<char *>(objects) + 24, 8);
        if (rendererScanCount > 20000)
            throw std::runtime_error("Renderer scan exceeds limit");
        rendererScanHandle = api.handleNew(objects, false);
    }
    if (!rendererScanHandle)
        return;
    auto objects = api.handleTarget(rendererScanHandle);
    auto end = std::min(rendererScanCount, rendererScanIndex + 40);
    for (; rendererScanIndex < end; rendererScanIndex++) {
        Obj renderer{};
        memcpy(&renderer, static_cast<char *>(objects) + 32 + rendererScanIndex * 8, 8);
        if (!alive(renderer) || indexedRenderers.count(renderer))
            continue;
        auto name = nameOf(renderer);
        if (geometryExcluded(name))
            continue;
        bool structural = geometryName(name);
        auto node = api.call(componentTransform, renderer);
        for (int depth = 0; !structural && depth < 4 && alive(node); depth++) {
            structural = geometryName(nameOf(node));
            node = api.call(getParent, node);
        }
        if (!structural)
            continue;
        RenderData item;
        item.handle = api.handleNew(renderer, false);
        item.bounds = api.value<Bounds>(rendererBounds, renderer);
        item.world = api.value<Matrix>(getWorldMatrix, api.call(componentTransform, renderer));
        item.layer = api.value<int>(getLayer, api.call(getGameObject, renderer));
        item.name = std::move(name);
        renderGeometry.push_back(std::move(item));
        indexedRenderers.insert(renderer);
    }
    if (rendererScanIndex >= rendererScanCount) {
        api.handleFree(rendererScanHandle);
        rendererScanHandle = 0;
        rendererScanIndex = 0;
        renderScanStarted = false;
        nextGeometryScan = GetTickCount64() + 10000;
        logDebug("Static render geometry indexed: %zu candidates from %zu renderers", renderGeometry.size(),
            rendererScanCount);
    }
}
std::shared_ptr<MeshData> loadGeometry(Obj mesh) {
    auto found = meshCache.find(mesh);
    if (found != meshCache.end())
        return found->second;
    if (meshCache.size() >= 256)
        return {};
    auto data = std::make_shared<MeshData>();
    data->handle = api.handleNew(mesh, false);
    meshCache.emplace(mesh, data);
    try {
        int count = api.value<int>(meshVertices, mesh);
        if (count <= 0 || count > 50000)
            return data;
        auto attributes = api.call(meshAttributes, mesh);
        if (!attributes)
            return data;
        size_t n{};
        memcpy(&n, static_cast<char *>(attributes) + 24, 8);
        if (n > 32)
            return data;
        bool positionSupported = false;
        for (size_t i = 0; i < n; i++) {
            int a[4];
            memcpy(a, static_cast<char *>(attributes) + 32 + i * 16, 16);
            if (a[0] == 0)
                positionSupported = a[1] == 0 && a[2] == 3 && a[3] == 0;
        }
        if (!positionSupported)
            return data;
        int stream = 0;
        void *args[] = {&stream};
        auto vertices = readGpuBuffer(api.value<ID3D11Buffer *>(vertexBuffer, mesh, args));
        auto indices = readGpuBuffer(api.value<ID3D11Buffer *>(indexBuffer, mesh));
        size_t stride = vertices.size() / static_cast<size_t>(count);
        if (stride < 12 || stride > 256 || vertices.size() % count)
            return data;
        int format = api.value<int>(meshIndexFormat, mesh);
        if (format != 0 && format != 1)
            return data;
        size_t element = format == 1 ? 4 : 2;
        if (indices.size() % element)
            return data;
        size_t bytes =
            static_cast<size_t>(count) * sizeof(Vec3) + (indices.size() / element) * sizeof(unsigned);
        if (bytes > 64 * 1024 * 1024 - meshCacheBytes) {
            logDebug("Mesh cache byte limit reached");
            return data;
        }
        data->vertices.resize(count);
        for (int i = 0; i < count; i++)
            memcpy(&data->vertices[i], vertices.data() + i * stride, 12);
        data->indices.resize(indices.size() / element);
        for (size_t i = 0; i < data->indices.size(); i++) {
            unsigned value{};
            memcpy(&value, indices.data() + i * element, element);
            data->indices[i] = value;
        }
        data->valid = true;
        meshCacheBytes += bytes;
        logDebug("Mesh cached: vertices=%d triangles=%zu name=%ls", count, data->indices.size() / 3,
            nameOf(mesh).c_str());
    } catch (const std::exception &e) {
        logError("Mesh unavailable: %s", e.what());
    }
    return data;
}
bool prepareGeometry(RenderData &item, Obj renderer, int frame) {
    if (!item.attempted) {
        if (lastMeshLoadFrame == frame)
            return false;
        lastMeshLoadFrame = frame;
        item.attempted = true;
        void *args[] = {meshFilterType};
        auto filter = api.call(getMeshComponent, renderer, args);
        if (!alive(filter))
            return false;
        auto mesh = api.call(sharedMesh, filter);
        if (!alive(mesh))
            return false;
        item.data = loadGeometry(mesh);
        // A full bounded cache is temporary; do not permanently exclude this
        // renderer after a failed allocation attempt.
        if (!item.data)
            item.attempted = false;
    }
    return item.data && item.data->valid;
}
float renderTarget(Vec3 origin, Vec3 direction, float closest) {
    meshHitName.clear();
    meshHitDistance = 0;
    int frame = api.value<int>(getFrame);
    for (auto &item : renderGeometry) {
        if (!boundsRay(origin, direction, item.bounds, closest))
            continue;
        auto renderer = api.handleTarget(item.handle);
        if (!alive(renderer) || !api.value<bool>(rendererEnabled, renderer))
            continue;
        auto object = api.call(getGameObject, renderer);
        if (!api.value<bool>(objectActive, object))
            continue;
        if (!prepareGeometry(item, renderer, frame))
            continue;
        const auto &data = *item.data;
        for (size_t i = 0; i + 2 < data.indices.size(); i += 3) {
            auto a = data.indices[i], b = data.indices[i + 1], c = data.indices[i + 2];
            if (a >= data.vertices.size() || b >= data.vertices.size() || c >= data.vertices.size())
                continue;
            float distance = triangleRay(origin, direction, transformPoint(item.world, data.vertices[a]),
                                         transformPoint(item.world, data.vertices[b]),
                                         transformPoint(item.world, data.vertices[c]));
            if (distance < closest) {
                closest = distance;
                meshHitName = item.name;
                meshHitDistance = distance;
            }
        }
    }
    return closest;
}
float renderCameraCollision(Vec3 pivot, Vec3 direction, float limit, int frame) {
    if (!renderSupported || !cfg.renderCollision)
        return limit;
    if (collisionBudgetFrame != frame) {
        collisionBudgetFrame = frame;
        collisionTrianglesThisFrame = 0;
    }
    constexpr size_t budget = 24000, queryBudget = 12000;
    size_t trianglesThisQuery = 0;
    float closest = limit;
    const RenderData *contact = nullptr;
    bool limited = false;
    struct Candidate {
        size_t index;
        float entry;
    };
    std::vector<Candidate> candidates;
    for (size_t i = 0; i < renderGeometry.size(); i++) {
        const auto &item = renderGeometry[i];
        if (item.layer < 0 || item.layer > 31 ||
            !(static_cast<unsigned>(cfg.collisionMask) & (1u << item.layer)))
            continue;
        float entry = sphereBoundsEntry(pivot, direction, cfg.radius, item.bounds, limit);
        if (std::isfinite(entry))
            candidates.push_back({i, entry});
    }
    std::sort(candidates.begin(), candidates.end(), [](const Candidate &a, const Candidate &b) {
        return a.entry != b.entry ? a.entry < b.entry : a.index < b.index;
    });
    for (const auto &candidate : candidates) {
        // After contraction, farther boxes cannot shorten the current camera path.
        if (candidate.entry > closest)
            break;
        auto &item = renderGeometry[candidate.index];
        auto renderer = api.handleTarget(item.handle);
        if (!alive(renderer) || !api.value<bool>(rendererEnabled, renderer))
            continue;
        auto object = api.call(getGameObject, renderer);
        if (!alive(object) || !api.value<bool>(objectActive, object))
            continue;
        // The cache describes static geometry. A moved door/container must not
        // block the camera at its old location; dynamic geometry uses Physics.
        auto currentWorld = api.value<Matrix>(getWorldMatrix, api.call(componentTransform, renderer));
        if (!unchangedGeometryTransform(item.world, currentWorld))
            continue;
        if (!prepareGeometry(item, renderer, frame))
            continue;
        const auto &data = *item.data;
        for (size_t i = 0; i + 2 < data.indices.size(); i += 3) {
            if (collisionTrianglesThisFrame >= budget || trianglesThisQuery >= queryBudget) {
                limited = true;
                break;
            }
            ++collisionTrianglesThisFrame;
            ++trianglesThisQuery;
            auto a = data.indices[i], b = data.indices[i + 1], c = data.indices[i + 2];
            if (a >= data.vertices.size() || b >= data.vertices.size() || c >= data.vertices.size())
                continue;
            float hit = sphereTriangleSweep(pivot, direction, cfg.radius,
                                            transformPoint(item.world, data.vertices[a]),
                                            transformPoint(item.world, data.vertices[b]),
                                            transformPoint(item.world, data.vertices[c]), closest);
            float distance = cameraClearance(closest, hit);
            if (distance < closest) {
                closest = distance;
                contact = &item;
            }
        }
        if (limited || closest == 0)
            break;
    }
    if (contact && frame - lastCollisionLog >= 120) {
        logDebug("Camera mesh collision: distance=%.3f radius=%.3f triangles=%zu name=%ls", closest, cfg.radius,
            collisionTrianglesThisFrame, contact->name.c_str());
        lastCollisionLog = frame;
    }
    if (limited && frame - lastCollisionBudgetLog >= 600) {
        logDebug("Camera mesh collision budget reached: %zu triangles/query, %zu/frame; remaining geometry skipped",
            queryBudget, budget);
        lastCollisionBudgetLog = frame;
    }
    return closest;
}
bool postEffectCollider(Obj collider, std::wstring_view colliderName) {
    if (!postEffectVolumeName(colliderName) || !postEffectVolumeTypeHandle || !postEffectRendererTypeHandle ||
        postEffectFilterFailed)
        return false;
    try {
        auto type = api.handleTarget(postEffectVolumeTypeHandle);
        if (!type)
            return false;
        void *args[] = {type};
        bool hasVolume = alive(api.call(getMeshComponent, collider, args));
        if (!hasVolume)
            return false;
        args[0] = api.handleTarget(postEffectRendererTypeHandle);
        if (!args[0])
            return false;
        return nativePostEffectVolume(colliderName, hasVolume,
                                      alive(api.call(getMeshComponent, collider, args)));
    } catch (const std::exception &e) {
        postEffectFilterFailed = true;
        logError("Post-effect collision filter unavailable: %s; retaining contacts", e.what());
        return false;
    }
}
Vec3 collide(Vec3 pivot, Vec3 desired, Obj player) {
    if (!cfg.collision || !sphereCast)
        return desired;
    auto delta = desired - pivot;
    float distance = length(delta);
    if (distance < .05f)
        return desired;
    auto direction = normalized(delta);
    auto hits = api.handleTarget(hitArrayHandle);
    float radius = cfg.radius;
    int mask = cfg.collisionMask, query = 1;
    void *args[] = {&pivot, &radius, &direction, hits, &distance, &mask, &query};
    int count = api.value<int>(sphereCast, nullptr, args);
    count = std::clamp(count, 0, 32);
    float closest = distance;
    std::wstring contactName;
    float contactDistance{};
    for (int i = 0; i < count; i++) {
        auto hit = static_cast<char *>(hits) + 32 + i * hitStride;
        auto collider = api.call(hitCollider, hit);
        if (!alive(collider))
            continue;
        // DarKov's PlaneForUse is a gameplay helper volume, not a physical wall.
        // Its initial overlap previously collapsed the camera to 12 centimetres.
        auto colliderName = nameOf(collider);
        if (colliderName == L"PlaneForUse")
            continue;
        if (postEffectCollider(collider, colliderName)) {
            auto now = GetTickCount64();
            if (now - lastPostEffectCameraLog >= 5000) {
                logDebug("Camera post-effect volume ignored: name=%ls sameObjectVolume=1 renderer=0 radius=%.3f queryDistance=%.3f",
                    colliderName.c_str(), radius, distance);
                lastPostEffectCameraLog = now;
            }
            continue;
        }
        auto tr = api.call(componentTransform, collider);
        if (nativeCameraControlVolume(
                tr, [](Obj node) { return nameOf(node); },
                [](Obj node) { return alive(node) ? api.call(getParent, node) : nullptr; })) {
            auto now = GetTickCount64();
            if (now - lastCameraHelperLog >= 5000) {
                logDebug("Camera helper ignored: name=%ls group=CameraTriggerGroup_LD radius=%.3f queryDistance=%.3f",
                    colliderName.c_str(), radius, distance);
                lastCameraHelperLog = now;
            }
            continue;
        }
        auto playerRoot = api.call(getParent, player);
        void *childArgs[] = {playerRoot ? playerRoot : player};
        if (tr == player || tr == playerRoot || api.value<bool>(isChild, tr, childArgs))
            continue;
        float d;
        memcpy(&d, hit + 28, 4);
        if (std::isfinite(d) && d >= 0) {
            float limited = std::max(.12f, d - .05f);
            if (limited < closest) {
                closest = limited;
                contactName = colliderName;
                contactDistance = d;
            }
        }
    }
    auto now = GetTickCount64();
    if (!contactName.empty() && now - lastPhysicsCollisionLog >= 5000) {
        logDebug("Camera physics collision: name=%ls hitDistance=%.3f cameraDistance=%.3f radius=%.3f",
            contactName.c_str(), contactDistance, closest, radius);
        lastPhysicsCollisionLog = now;
    }
    closest = renderCameraCollision(pivot, direction, closest, api.value<int>(getFrame));
    return pivot + direction * closest;
}
Vec3 aimTarget(Vec3 origin, Vec3 direction, Obj player) {
    constexpr float range = 200.f;
    float distance = range, radius = .01f;
    int mask = cfg.collisionMask, query = 1;
    auto hits = api.handleTarget(hitArrayHandle);
    void *args[] = {&origin, &radius, &direction, hits, &distance, &mask, &query};
    int count = std::clamp(api.value<int>(sphereCast, nullptr, args), 0, 32);
    float closest = range;
    auto playerRoot = api.call(getParent, player);
    for (int i = 0; i < count; i++) {
        auto hit = static_cast<char *>(hits) + 32 + i * hitStride;
        auto collider = api.call(hitCollider, hit);
        if (!alive(collider))
            continue;
        auto colliderName = nameOf(collider);
        if (colliderName == L"PlaneForUse")
            continue;
        // Local post-processing boxes also cannot provide a bullet aim surface.
        if (postEffectCollider(collider, colliderName)) {
            auto now = GetTickCount64();
            if (now - lastPostEffectAimLog >= 5000) {
                logDebug("Aim post-effect volume ignored: name=%ls sameObjectVolume=1 renderer=0",
                    colliderName.c_str());
                lastPostEffectAimLog = now;
            }
            continue;
        }
        auto tr = api.call(componentTransform, collider);
        // These native LD control regions are not aimable surfaces either.
        // A positive-distance hit can lie between the shoulder camera and
        // the player, reversing aim and raising the gun toward empty space.
        if (nativeCameraControlVolume(
                tr, [](Obj node) { return nameOf(node); },
                [](Obj node) { return alive(node) ? api.call(getParent, node) : nullptr; })) {
            auto now = GetTickCount64();
            if (now - lastAimHelperLog >= 5000) {
                float d;
                memcpy(&d, hit + 28, 4);
                logDebug("Aim helper ignored: name=%ls group=CameraTriggerGroup_LD hitDistance=%.3f",
                    nameOf(collider).c_str(), d);
                lastAimHelperLog = now;
            }
            continue;
        }
        void *childArgs[] = {playerRoot ? playerRoot : player};
        if (tr == player || tr == playerRoot || api.value<bool>(isChild, tr, childArgs))
            continue;
        float d;
        memcpy(&d, hit + 28, 4);
        if (std::isfinite(d) && d > .01f)
            closest = std::min(closest, d);
    }
    closest = renderTarget(origin, direction, closest);
    return origin + direction * closest;
}
bool activeCrosshair(Obj cross) {
    if (!alive(cross) || *static_cast<Class *>(cross) != crosshairClass)
        return false;
    auto object = api.call(getGameObject, cross);
    return alive(object) && api.value<bool>(objectActive, object);
}
Obj acquireCrosshair() {
    auto cross = crosshairHandle ? api.handleTarget(crosshairHandle) : nullptr;
    if (activeCrosshair(cross))
        return cross;
    if (crosshairHandle) {
        restoreCrosshair();
    }
    int frame = api.value<int>(getFrame);
    if (frame - lastCrosshairSearch < 60)
        return nullptr;
    lastCrosshairSearch = frame;
    void *args[] = {crosshairType};
    auto objects = api.call(findObjects, nullptr, args);
    if (!objects)
        return nullptr;
    // Root the result while checking active candidates through Unity getters.
    auto handle = api.handleNew(objects, false);
    Obj selected{};
    try {
        objects = api.handleTarget(handle);
        size_t count{};
        memcpy(&count, static_cast<char *>(objects) + 24, 8);
        if (count <= 64)
            for (size_t i = 0; i < count; i++) {
                objects = api.handleTarget(handle);
                memcpy(&cross, static_cast<char *>(objects) + 32 + i * 8, 8);
                if (activeCrosshair(cross)) {
                    crosshairHandle = api.handleNew(cross, false);
                    selected = api.handleTarget(crosshairHandle);
                    log("Acquired DarKov crosshair=%p", selected);
                    break;
                }
            }
    } catch (...) {
        api.handleFree(handle);
        throw;
    }
    api.handleFree(handle);
    return selected;
}
void updateAdsState() {
    bool available = adsState.available, active = adsState.active;
    if (!enabled || !captured || !foreground() || uiBlocked || !cfg.gameADS || !crosshairADS)
        adsState.reset();
    else {
        auto cross = acquireCrosshair();
        adsState.sample(cross != nullptr, cross ? api.read<unsigned char>(cross, crosshairADS) : 0);
    }
    if (available != adsState.available || active != adsState.active)
        logDebug("ADS source=%s active=%d rightButton=%d",
            adsState.available ? "game-crosshair" : "right-button-fallback", adsState.active,
            (GetAsyncKeyState(VK_RBUTTON) & 0x8000) != 0);
}
void centerCrosshair() {
    if (!captured) {
        resetDodgeDirection();
        restoreCrosshair();
        return;
    }
    auto cross = acquireCrosshair();
    if (!cross)
        return;
    auto tr = api.call(componentTransform, cross);
    void *parentArgs[] = {canvasType};
    auto canvas = api.call(getCanvasParent, cross, parentArgs);
    if (!alive(canvas))
        return;
    auto uiCam = api.call(getCanvasCamera, canvas);
    if (!alive(uiCam))
        return;
    auto old = api.value<Vec3>(getPosition, tr);
    if (!crosshairMoved) {
        crosshairSaved = old;
        crosshairMoved = true;
    }
    void *screenArgs[] = {&old};
    auto screen = api.value<Vec3>(worldToScreen, uiCam, screenArgs);
    screen.x = api.value<int>(getWidth) * .5f;
    screen.y = api.value<int>(getHeight) * .5f;
    void *worldArgs[] = {&screen};
    auto world = api.value<Vec3>(screenToWorld, uiCam, worldArgs);
    if (finite(world))
        set(setPosition, tr, world);
}
#include "NativeDodgeRuntime.inc"
#include "NativeAdsZoomRuntime.inc"
#include "NativeLaserRuntime.inc"
#include "NativeThrowRuntime.inc"
#include "NativeThrowInputRuntime.inc"
void aimTick(int phase = 1) {
    if (!enabled || !captured || !cfg.aim || !saved.cameraHandle || !foreground())
        return;
    auto root = currentRoot();
    if (!root)
        return;
    auto ik = playerIK(root);
    if (!ik)
        return;
    auto entity = api.read<Obj>(ik, ikEntity);
    if (!entity)
        return;
    auto aim = api.read<Obj>(entity, entityAim);
    if (!aim)
        return;
    auto dodgeMode = nativeDodgeMode(entity, phase);
    if (dodgeMode != DodgeAimMode::Normal) {
        resetNativeLaser();
        if (dodgeMode == DodgeAimMode::Prepare) {
            auto direction = dodgeDirection.requested;
            alignas(8) unsigned char empty[48]{};
            void *args[] = {&direction, &crosshairSource, empty, empty, empty};
            api.call(aimUpdate, aim, args);
            if (cfg.turn) {
                auto turn = api.call(entityTurn, entity);
                if (turn) {
                    auto q = rotation(std::atan2(direction.x, direction.z) * 57.29577951f, 0);
                    void *turnArgs[] = {&q, &clientSide};
                    api.call(turnRotation, turn, turnArgs);
                }
            }
        }
        // Let the native action own direction/animation for its actual RollSpeed span.
        return;
    }
    auto cam = api.handleTarget(saved.cameraHandle);
    if (!alive(cam))
        return;
    auto player = api.call(componentTransform, ik);
    auto camTr = api.call(componentTransform, cam);
    auto cameraPosition = api.value<Vec3>(getPosition, camTr);
    auto cameraRay = forward(yaw, pitch);
    Vec3 target{};
    auto playerPosition = api.value<Vec3>(getPosition, player);
    // saved.smoothed is the applied shoulder position, while native input can
    // temporarily put this same Camera back into its original top-down pose.
    if (!saved.first)
        cacheNativeThrowInput(entity, saved.smoothed, cameraRay, playerPosition);
    bool throwing = nativeThrowTarget(aim, cameraPosition, cameraRay, playerPosition, target);
    if (!throwing)
        target = aimTarget(cameraPosition, cameraRay, player);
    auto reference = api.value<Vec3>(aimOrigin, aim), origin = reference;
    auto direction = normalized(target - origin);
    if (!finite(direction) || length(direction) < .9f)
        return;
    if (!throwing)
        convergeNativeLaser(entity, aim, reference, target, direction, phase);
    else
        resetNativeLaser();
    // A verified active laser uses its native source displacement. Otherwise
    // keep the centered target direction from the game's reference unchanged.
    // The obsolete animated-muzzle compensation is removed; model transforms
    // remain presentation data and never replace the game's aiming reference.
    // Verified runtime entry: PlayerCrosshair accepts full 3D direction.
    // Ordinary gun aim clears the legacy ground target; native ground selection
    // instead supplies only parameter 5, verified to populate GOKHJNGGJKC while
    // leaving IIDDJIBHKNM empty. The game owns trajectory/range and item usage.
    alignas(8) unsigned char empty[48]{};
    NativeOptionalPoint groundTarget{target, 1, {}};
    void *args[] = {&direction, &crosshairSource, empty, empty,
                    throwing ? static_cast<void *>(&groundTarget) : static_cast<void *>(empty)};
    api.call(aimUpdate, aim, args);
    if (cfg.turn) {
        auto turn = api.call(entityTurn, entity);
        if (turn) {
            auto q = rotation(std::atan2(direction.x, direction.z) * 57.29577951f, 0);
            void *turnArgs[] = {&q, &clientSide};
            api.call(turnRotation, turn, turnArgs);
        }
    }
    int frame = api.value<int>(getFrame);
    if (frame - lastAimLog >= 600) {
        auto actual = api.value<Vec3>(aimDirection, aim);
        logDebug("Aim mode=reference origin=(%.3f %.3f %.3f) target=(%.3f %.3f %.3f) requested=(%.4f %.4f %.4f) actual=(%.4f %.4f %.4f) mesh=%ls meshDistance=%.3f laser=%d laserReady=%d groundSelection=%d",
            origin.x, origin.y, origin.z, target.x, target.y, target.z, direction.x, direction.y, direction.z,
            actual.x, actual.y, actual.z, meshHitName.c_str(), meshHitDistance, laserPresent,
            laserCalibration.ready, throwing);
        lastAimLog = frame;
    }
}
void cameraTick() {
    int frame = api.value<int>(getFrame);
    if (frame == lastCameraFrame)
        return;
    lastCameraFrame = frame;
    auto root = currentRoot();
    if (!root) {
        restore();
        return;
    }
    syncCapture();
    // Unity's graphics getter dereferences thread-local graphics state. Resolve
    // metadata in the loader thread, but invoke it only from Unity LateUpdate.
    if (!graphicsChecked) {
        log("Graphics probe on Unity LateUpdate thread=%lu", GetCurrentThreadId());
        int graphicsType = api.value<int>(graphicsDeviceType);
        renderSupported = graphicsType == 2;
        graphicsChecked = true;
        log("Graphics device type=%d; D3D11 mesh targeting=%d", graphicsType, renderSupported);
    }
    if (lastStatusFrame < 0 || frame - lastStatusFrame > 600) {
        logDebug("DarKov frame=%d enabled=%d capture=%d", frame, enabled, captured);
        lastStatusFrame = frame;
    }
    if (!enabled)
        return;
    updateOpticState(root);
    updateAdsState();
    geometryScan();
    auto ik = playerIK(root);
    auto player = ik ? api.call(componentTransform, ik) : nullptr;
    auto cam = sceneCamera(root);
    if (!player || !cam) {
        restore();
        enabled = false;
        log("No active DarKov player/camera; camera disabled");
        return;
    }
    Vec3 playerPos = api.value<Vec3>(getPosition, player);
    if (!finite(playerPos))
        throw std::runtime_error("Invalid player position");
    if (saved.cameraHandle && api.handleTarget(saved.cameraHandle) != cam)
        restore();
    auto tr = api.call(componentTransform, cam);
    if (!saved.cameraHandle) {
        saved.cameraHandle = api.handleNew(cam, false);
        saved.position = api.value<Vec3>(getPosition, tr);
        saved.rotation = api.value<Quat>(getRotation, tr);
        saved.fov = api.value<float>(getFov, cam);
        saved.nearClip = api.value<float>(getNear, cam);
        saved.farClip = api.value<float>(getFar, cam);
        saved.ortho = api.value<bool>(getOrtho, cam);
        saved.cursorShown = api.value<bool>(getCursorVisible);
        saved.lockMode = api.value<int>(getCursorLock);
        saved.first = true;
        Vec3 toPlayer = playerPos - saved.position;
        yaw = std::atan2(toPlayer.x, toPlayer.z) * 57.29577951f;
        pitch = 12.f;
        log("Acquired DarKov camera=%p displayModel=%p originalFOV=%.1f", cam, player, saved.fov);
    }
    if (foreground() && !uiBlocked)
        setCursor(!captured, captured ? 2 : 0);
    auto pivot = playerPos + Vec3{0, cfg.height, 0};
    auto desired =
        position(playerPos, yaw, pitch, leftShoulder ? -cfg.side : cfg.side, cfg.height, cfg.distance);
    auto constrained = collide(pivot, desired, player);
    float dt = std::clamp(api.value<float>(getDelta), .001f, .1f);
    float blend = cfg.smoothing <= 0 ? 1.f : 1.f - std::exp(-cfg.smoothing * dt);
    if (saved.first) {
        saved.smoothed = constrained;
        saved.first = false;
    } else
        saved.smoothed = saved.smoothed + (constrained - saved.smoothed) * blend;
    // Recheck the interpolated point so smoothing does not move through a wall.
    saved.smoothed = collide(pivot, saved.smoothed, player);
    set(setPosition, tr, saved.smoothed);
    set(setRotation, tr, rotation(yaw, pitch));
    set(setOrtho, cam, false);
    set(setNear, cam, .08f);
    bool ads = zoomHeld();
    float targetFov =
        ads ? magnifiedADS(cfg.fov, cfg.adsFov, cfg.adsSensitivity, effectiveMagnification()).fov : cfg.fov;
    float fov = nativeAdsZoomFov(ik ? api.read<Obj>(ik, ikEntity) : nullptr, targetFov, ads);
    set(setFov, cam, fov);
    nativeRoofTick(root);
    set(setFar, cam, cfg.renderDistance > 0 ? cfg.renderDistance : saved.farClip);
    aimTick();
    centerCrosshair();
    nativeVoiceTick();
    nativeHealthTick(root, cam);
    nativePlayerBarsTick(root, cam);
    nativeInteractionTick(cam);
    nativeTrackingTick(root, cam, frame);
}
void guardedTick(int phase) {
    if (inMod || faulted)
        return;
    inMod = true;
    try {
        if (phase == 1)
            cameraTick();
        else {
            if (phase == 0) {
                inputTick();
                nativeStreamingTick();
            } else {
                syncCapture();
                updateAdsState();
            }
            aimTick(phase);
        }
    } catch (const std::exception &e) {
        logError("ERROR: %s; disabled", e.what());
        enabled = false;
        captureGate.reset();
        faulted = true;
        try {
            restore();
        } catch (...) {
        };
    }
    inMod = false;
}
Obj invokerHook(void *entry, Method method, Obj self, void **args) {
    if (initialized && !inMod && method == rootUpdate)
        guardedTick(0);
    if (initialized && !inMod && method == rootDestroy) {
        inMod = true;
        try {
            restore();
        } catch (...) {
        }
        enabled = false;
        inMod = false;
    }
    auto result = originalInvoker(entry, method, self, args);
    if (initialized && !inMod && method == rootUpdate)
        guardedTick(2);
    if (initialized && !inMod && method == lateUpdate)
        guardedTick(1);
    return result;
}
void bindUnity() {
    auto systemInfo = api.find("UnityEngine.CoreModule.dll", "UnityEngine", "SystemInfo");
    graphicsDeviceType = api.require(systemInfo, "get_graphicsDeviceType");
    cameraClass = api.find("UnityEngine.CoreModule.dll", "UnityEngine", "Camera");
    transformClass = api.find("UnityEngine.CoreModule.dll", "UnityEngine", "Transform");
    auto go = api.find("UnityEngine.CoreModule.dll", "UnityEngine", "GameObject"),
         component = api.find("UnityEngine.CoreModule.dll", "UnityEngine", "Component"),
         object = api.find("UnityEngine.CoreModule.dll", "UnityEngine", "Object"),
         time = api.find("UnityEngine.CoreModule.dll", "UnityEngine", "Time"),
         screen = api.find("UnityEngine.CoreModule.dll", "UnityEngine", "Screen"),
         cursor = api.find("UnityEngine.CoreModule.dll", "UnityEngine", "Cursor");
    getTransform = api.require(go, "get_transform");
    componentTransform = api.require(component, "get_transform");
    getChildComponent = api.requireTyped(go, "GetComponentInChildren", {"System.Type", "System.Boolean"}, "UnityEngine.Component");
    isAlive = api.require(object, "op_Implicit", 1);
    getName = api.require(object, "get_name");
    getMain = api.require(cameraClass, "get_main");
    getPosition = api.require(transformClass, "get_position");
    setPosition = api.require(transformClass, "set_position", 1);
    getRotation = api.require(transformClass, "get_rotation");
    setRotation = api.require(transformClass, "set_rotation", 1);
    isChild = api.require(transformClass, "IsChildOf", 1);
    getParent = api.require(transformClass, "get_parent");
    getFov = api.require(cameraClass, "get_fieldOfView");
    setFov = api.require(cameraClass, "set_fieldOfView", 1);
    getOrtho = api.require(cameraClass, "get_orthographic");
    setOrtho = api.require(cameraClass, "set_orthographic", 1);
    getNear = api.require(cameraClass, "get_nearClipPlane");
    setNear = api.require(cameraClass, "set_nearClipPlane", 1);
    getFar = api.require(cameraClass, "get_farClipPlane");
    setFar = api.require(cameraClass, "set_farClipPlane", 1);
    getFrame = api.require(time, "get_frameCount");
    getDelta = api.require(time, "get_unscaledDeltaTime");
    getWidth = api.require(screen, "get_width");
    getHeight = api.require(screen, "get_height");
    cursorVisible = api.require(cursor, "set_visible", 1);
    cursorLock = api.require(cursor, "set_lockState", 1);
    getCursorVisible = api.require(cursor, "get_visible");
    getCursorLock = api.require(cursor, "get_lockState");
    cameraType = api.typeObject(api.classType(cameraClass));
    cameraTypeHandle = api.handleNew(cameraType, false);
    auto entityClass = api.find("Assembly-CSharp.dll", "GF.DarKov", "DarkovAimIK");
    entityType = api.typeObject(api.classType(entityClass));
    entityTypeHandle = api.handleNew(entityType, false);
    auto physics = api.find("UnityEngine.PhysicsModule.dll", "UnityEngine", "Physics"),
         hit = api.find("UnityEngine.PhysicsModule.dll", "UnityEngine", "RaycastHit");
    hitStride = api.instanceSize(hit) - 16;
    if (hitStride != 44)
        throw std::runtime_error("RaycastHit layout changed");
    sphereCast = api.require(physics, "SphereCastNonAlloc", 7);
    hitCollider = api.require(hit, "get_collider");
    hitArrayHandle = api.handleNew(api.arrayNew(hit, 32), false);
    ikEntity = api.field(entityClass, "CBKENCHIIBK");
    auto entity = api.find("Assembly-CSharp.dll", "", "JDHIOAAODNO"),
         aim = api.find("Assembly-CSharp.dll", "", "MHCGGDLDEPF"),
         turn = api.find("Assembly-CSharp.dll", "", "AFFAOKCCHDM");
    entityAim = api.field(entity, "ICHBHDHMBCJ");
    if (!ikEntity || !entityAim)
        throw std::runtime_error("DarKov aim fields changed");
    aimUpdate = api.require(aim, "PJDLHOKKEKL", 5);
    aimOrigin = api.require(aim, "CAFMBLFHEOK");
    aimDirection = api.require(aim, "PKDNAKNEIOM");
    entityTurn = api.require(entity, "DLMNGHKNNNF");
    turnRotation = api.require(turn, "BCLIOJJJDDF", 2);
    for (auto instance : {aimUpdate, aimOrigin, aimDirection, entityTurn, turnRotation}) {
        unsigned impl{};
        if (api.methodFlags(instance, &impl) & 16)
            throw std::runtime_error("Core aim method instance ABI changed");
    }
    auto source = api.find("Assembly-CSharp.dll", "", "DFNLMNKJKIC"),
         side = api.find("Assembly-CSharp.dll", "", "KJMJBJOIBHF");
    auto sourceField = api.field(source, "PlayerCrosshair"), sideField = api.field(side, "Client");
    if (!sourceField || !sideField)
        throw std::runtime_error("Aim enum values missing");
    crosshairSource = api.global<int>(sourceField);
    clientSide = api.global<int>(sideField);
    if (crosshairSource != 2 || clientSide != 0)
        throw std::runtime_error("Aim enum values changed");
    entityWeapon = checkedOpticField(entity, "KJMEEDLGOKA", true);
    bindOpticMetadata(entity);
    bindNativeDodge(entity);
    bindNativeAdsZoom(entity);
    bindNativeLaser(entity);
    bindNativeThrow(aim);
    bindNativeThrowInput(entity);
    bindGameSensitivity();
    auto cross = api.find("Assembly-CSharp.dll", "DarKov.UI", "Crosshair"),
         canvas = api.find("UnityEngine.UIModule.dll", "UnityEngine", "Canvas");
    crosshairType = api.typeObject(api.classType(cross));
    crosshairTypeHandle = api.handleNew(crosshairType, false);
    canvasType = api.typeObject(api.classType(canvas));
    canvasTypeHandle = api.handleNew(canvasType, false);
    findObjects = api.requireTyped(object, "FindObjectsOfType", {"System.Type"}, "UnityEngine.Object[]", true);
    getCanvasParent = api.requireTyped(component, "GetComponentInParent", {"System.Type"}, "UnityEngine.Component");
    getCanvasCamera = api.require(canvas, "get_worldCamera");
    worldToScreen = api.require(cameraClass, "WorldToScreenPoint", 1);
    screenToWorld = api.require(cameraClass, "ScreenToWorldPoint", 1);
    crosshairClass = cross;
    crosshairADS = api.field(cross, "KMFIKHKKIJE");
    log("Sampled game ADS field bound=%d; unavailable uses right-button fallback", crosshairADS != nullptr);
    auto renderer = api.find("UnityEngine.CoreModule.dll", "UnityEngine", "Renderer"),
         meshRenderer = api.find("UnityEngine.CoreModule.dll", "UnityEngine", "MeshRenderer"),
         filter = api.find("UnityEngine.CoreModule.dll", "UnityEngine", "MeshFilter"),
         mesh = api.find("UnityEngine.CoreModule.dll", "UnityEngine", "Mesh");
    rendererBounds = api.require(renderer, "get_bounds");
    rendererEnabled = api.require(renderer, "get_enabled");
    getGameObject = api.require(component, "get_gameObject");
    getLayer = api.require(go, "get_layer");
    objectActive = api.require(go, "get_activeInHierarchy");
    getWorldMatrix = api.require(transformClass, "get_localToWorldMatrix");
    getMeshComponent = api.requireTyped(component, "GetComponent", {"System.Type"}, "UnityEngine.Component");
    sharedMesh = api.require(filter, "get_sharedMesh");
    meshVertices = api.require(mesh, "get_vertexCount");
    vertexBuffer = api.require(mesh, "GetNativeVertexBufferPtr", 1);
    indexBuffer = api.require(mesh, "GetNativeIndexBufferPtr");
    meshIndexFormat = api.require(mesh, "get_indexFormat");
    meshAttributes = api.require(mesh, "GetVertexAttributesAlloc");
    rendererType = api.typeObject(api.classType(meshRenderer));
    rendererTypeHandle = api.handleNew(rendererType, false);
    meshFilterType = api.typeObject(api.classType(filter));
    meshFilterTypeHandle = api.handleNew(meshFilterType, false);
    try {
        auto volume = api.find("Unity.RenderPipelines.Core.Runtime.dll", "UnityEngine.Rendering", "Volume");
        postEffectVolumeTypeHandle = api.handleNew(api.typeObject(api.classType(volume)), false);
        postEffectRendererTypeHandle = api.handleNew(api.typeObject(api.classType(renderer)), false);
        if (!postEffectVolumeTypeHandle || !postEffectRendererTypeHandle)
            throw std::runtime_error("Post-effect type GC handle allocation failed");
    } catch (const std::exception &e) {
        if (postEffectVolumeTypeHandle)
            api.handleFree(postEffectVolumeTypeHandle);
        if (postEffectRendererTypeHandle)
            api.handleFree(postEffectRendererTypeHandle);
        postEffectVolumeTypeHandle = postEffectRendererTypeHandle = 0;
        logError("Post-effect collision filter binding unavailable: %s; retaining contacts", e.what());
    }
    log("Post-effect collision filter bound=%d; dedicated name and same-object Volume without Renderer",
        postEffectVolumeTypeHandle != 0 && postEffectRendererTypeHandle != 0);
    bindNativeVoice();
    bindNativeHealth();
    bindNativePlayerBars();
    bindNativeInteraction();
    bindNativeTracking();
    bindNativeAttackProjection();
    bindNativeStreaming();
    bindNativeRoof();
}
DWORD WINAPI initialize(void *) {
    try {
        wchar_t path[32768];
        GetModuleFileNameW(ownModule, path, 32768);
        directory = path;
        directory = directory.substr(0, directory.find_last_of(L"\\/"));
        iniPath = directory + L"\\GF2ShoulderSurfing.ini";
        logPath = directory + L"\\GF2ShoulderSurfingV52.log";
        runtimeLog.configure(logPath);
        log("GF2 ShoulderSurfing 0.52.0 initialization; dynamic runtime APIs, metadata bindings and frame entry; up to two hooks");
        rejectOtherModVersions();
        loadConfig();
        api.initialize();
        auto deadline = GetTickCount64() + 120000;
        auto domain = api.domainGet();
        if (!domain)
            log("Waiting for IL2CPP domain during game startup");
        while (!domain && GetTickCount64() < deadline) {
            Sleep(250);
            domain = api.domainGet();
        }
        if (!domain)
            throw std::runtime_error("IL2CPP domain startup timed out");
        auto thread = api.threadAttach(domain);
        if (!thread)
            throw std::runtime_error("Cannot attach initialization thread to IL2CPP");
        struct ScopedRuntimeThread {
            Obj value;
            ~ScopedRuntimeThread() {
                api.threadDetach(value);
            }
        } detachThread{thread};
        // Automatic startup can reach GameAssembly before managed game types exist.
        while (true) {
            try {
                api.find("Assembly-CSharp.dll", "GF.DarKov", "DarKovGameRoot");
                break;
            } catch (const std::exception &e) {
                if (GetTickCount64() >= deadline) {
                    throw std::runtime_error(std::string("Managed game metadata startup timed out: ") +
                                             e.what());
                }
                Sleep(250);
            }
        }
        api.validateAbi();
        log("Runtime ABI validated; APIs standard=%u fingerprint=%u; structural catalog=%zu classes",
            api.exportResolver->standardCount, api.exportResolver->fingerprintCount, api.catalogCount);
        rootClass = api.find("Assembly-CSharp.dll", "GF.DarKov", "DarKovGameRoot");
        auto follow = api.find("Assembly-CSharp.dll", "GF.DarKov", "DarKovCameraFollow");
        if (strcmp(api.className(rootClass), "DarKovGameRoot"))
            throw std::runtime_error("Runtime name validation failed");
        rootInstance = api.field(rootClass, "Ins");
        rootCamera = api.field(rootClass, "CameraRoot");
        rootPlayer = api.field(rootClass, "PlayerRoot");
        if (!rootInstance || !rootCamera || !rootPlayer)
            throw std::runtime_error("DarKov fields changed");
        try {
            auto gameCursor = api.find("Assembly-CSharp.dll", "DarKov.UI", "DarKovCursor");
            cursorUI = api.field(gameCursor, "PONPCFMLPMM");
        } catch (const std::exception &e) {
            logError("Game UI signal unavailable: %s; use manual capture", e.what());
        }
        log("Game UI signal bound=%d; autoRelease=%d autoResume=%d; raw [1,1] requests cursor",
            cursorUI != nullptr, cfg.autoReleaseUI, cfg.autoResumeUI);
        bindNativeRadialCursor();
        lateUpdate = api.require(follow, "LateUpdate");
        rootUpdate = api.require(rootClass, "Update");
        rootDestroy = api.method(rootClass, "OnDestroy", 0);
        // Unity messages use HybridCLR's Invoker, bypassing methodPointer.
        // This 2019.4 Invoker returns a boxed object and takes four arguments.
        // Filter by MethodInfo identity, preserving all unrelated interpreted calls.
        if (Api::invoker(lateUpdate) != Api::invoker(rootUpdate))
            throw std::runtime_error("HybridCLR Invoker layout changed");
        if (!supportedInvoker(api.exportResolver->image(), Api::invoker(lateUpdate)))
            throw std::runtime_error("Unsupported HybridCLR Invoker ABI");
        log("Metadata validation complete; binding Unity interfaces without graphics calls");
        bindUnity();
        log("Dynamic metadata bindings complete; renamed classes=%u fields=%u methods=%u; Invoker RVA=%zx",
            api.resolvedClasses, api.resolvedFields, api.resolvedMethods,
            static_cast<unsigned char *>(Api::invoker(lateUpdate)) - api.exportResolver->image().base());
        if (MH_Initialize() != MH_OK)
            throw std::runtime_error("MinHook initialization failed");
        if (MH_CreateHook(Api::invoker(lateUpdate), reinterpret_cast<void *>(&invokerHook),
                          reinterpret_cast<void **>(&originalInvoker)) != MH_OK)
            throw std::runtime_error("Camera hook creation failed");
        queueNativeThrowInput();
        if (MH_QueueEnableHook(Api::invoker(lateUpdate)) != MH_OK)
            throw std::runtime_error("Camera hook activation queue failed");
        initialized = true;
        if (MH_ApplyQueued() != MH_OK) {
            initialized = false;
            throw std::runtime_error("Camera hook activation failed");
        }
        log("READY: controls from [Keys], Esc releases. Aim uses game reference; graphics probe deferred to Unity LateUpdate.");
    } catch (const std::exception &e) {
        faulted = true;
        logError("INITIALIZATION FAILED: %s", e.what());
    }
    return 0;
}
} // namespace
extern "C" __declspec(dllexport) DWORD WINAPI GF2ShoulderCommand(void *command) {
    pendingCommand.store(static_cast<int>(reinterpret_cast<uintptr_t>(command)));
    return 1;
}
BOOL APIENTRY DllMain(HMODULE module, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        ownModule = module;
        DisableThreadLibraryCalls(module);
        auto thread = CreateThread(nullptr, 0, initialize, nullptr, 0, nullptr);
        if (thread)
            CloseHandle(thread);
    }
    return TRUE;
}
