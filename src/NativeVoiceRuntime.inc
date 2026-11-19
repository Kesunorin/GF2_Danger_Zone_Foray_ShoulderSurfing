// Included inside the existing runtime namespace. No new hooks or UI lifecycle observer.
Method voiceChildren{}, voiceChild{}, voiceLocalRotation{}, voiceSetLocalRotation{}, voiceFindAll{},
    voiceTransformPoint{}, voiceCanvasMode{};
bool voiceBound{}, voiceFailed{};
unsigned voiceContainer{};
ULONGLONG voiceLastScan{};
int voiceLastLog = -600;
struct VoiceEntry {
    unsigned handle{};
    VoiceDirection direction;
};
std::vector<VoiceEntry> voiceEntries;
void restoreVoiceEntry(VoiceEntry &entry, float targetYaw) {
    if (!entry.handle)
        return;
    auto object = api.handleTarget(entry.handle);
    if (entry.direction.owned && alive(object)) {
        float observed{};
        auto q = api.value<Quat>(voiceLocalRotation, object);
        // Never overwrite a later native game update or another owner's transform write.
        if (planarAngle(q, observed) && entry.direction.owns(observed))
            set(voiceSetLocalRotation, object, planarRotation(entry.direction.nativeAt(targetYaw)));
    }
    entry.direction = {};
}
void resetVoiceDirection(bool original, bool retry) {
    float targetYaw = original && saved.cameraHandle ? quaternionYaw(saved.rotation) : yaw;
    for (auto &entry : voiceEntries) {
        try {
            restoreVoiceEntry(entry, targetYaw);
        } catch (const std::exception &e) {
            logError("Native voice cleanup skipped: %s", e.what());
        }
        if (entry.handle)
            api.handleFree(entry.handle);
    }
    voiceEntries.clear();
    if (voiceContainer)
        api.handleFree(voiceContainer);
    voiceContainer = 0;
    voiceLastScan = 0;
    if (retry)
        voiceFailed = false;
}
int voiceChildCount(Obj parent) {
    int count = api.value<int>(voiceChildren, parent);
    if (count < 0 || count > 64)
        throw std::runtime_error("Voice child bound");
    return count;
}
Obj voiceChildAt(Obj parent, int i) {
    void *args[] = {&i};
    return api.call(voiceChild, parent, args);
}
Obj voiceChildNamed(Obj parent, const wchar_t *name) {
    for (int i = 0, n = voiceChildCount(parent); i < n; i++) {
        auto child = voiceChildAt(parent, i);
        if (nameOf(child) == name)
            return child;
    }
    return nullptr;
}
bool voiceActive(Obj object) {
    return alive(object) && api.value<bool>(objectActive, api.call(getGameObject, object));
}
Obj findVoiceContainer() {
    void *args[] = {crosshairType};
    auto array = api.call(voiceFindAll, nullptr, args);
    if (!array)
        return nullptr;
    size_t count{};
    memcpy(&count, static_cast<char *>(array) + 24, 8);
    if (count > 20)
        throw std::runtime_error("Voice crosshair bound");
    auto handle = api.handleNew(array, false);
    Obj found{};
    try {
        for (size_t i = 0; i < count && !found; i++) {
            auto current = api.handleTarget(handle);
            Obj cross{};
            memcpy(&cross, static_cast<char *>(current) + 32 + i * 8, 8);
            if (!alive(cross))
                continue;
            auto t = api.call(componentTransform, cross);
            for (int depth = 0; t && depth < 16; depth++, t = api.call(getParent, t)) {
                if (nameOf(t) != L"ESMainPCPanel(Clone)")
                    continue;
                auto panelRoot = voiceChildNamed(t, L"Root");
                if (panelRoot && voiceActive(panelRoot))
                    found = voiceChildNamed(panelRoot, L"ESVoiceprint");
                break;
            }
        }
    } catch (...) {
        api.handleFree(handle);
        throw;
    }
    api.handleFree(handle);
    return found;
}
bool voiceProjectionPositive(Obj root) {
    // Verify the actual parent's screen basis, including any canvas/parent mirroring.
    void *canvasArgs[] = {canvasType};
    auto current = root;
    Obj camera{};
    int mode = -1;
    for (int depth = 0; current && depth < 16; depth++) {
        auto canvas = api.call(getCanvasParent, current, canvasArgs);
        if (!alive(canvas))
            break;
        mode = api.value<int>(voiceCanvasMode, canvas);
        camera = api.call(getCanvasCamera, canvas);
        if (mode == 0 || alive(camera))
            break;
        current = api.call(getParent, api.call(componentTransform, canvas));
        mode = -1;
    }
    if (mode != 0 && !alive(camera))
        return false;
    auto parent = api.call(getParent, root);
    if (!alive(parent))
        return false;
    Vec3 projected[3];
    Vec3 local[3] = {{0, 0, 0}, {1, 0, 0}, {0, 1, 0}};
    for (int i = 0; i < 3; i++) {
        void *args[] = {&local[i]};
        auto world = api.value<Vec3>(voiceTransformPoint, parent, args);
        if (mode == 0)
            projected[i] = world;
        else {
            void *projectArgs[] = {&world};
            projected[i] = api.value<Vec3>(worldToScreen, camera, projectArgs);
        }
        if (!finite(projected[i]))
            return false;
    }
    auto x = projected[1] - projected[0], y = projected[2] - projected[0];
    return x.x * y.y - x.y * y.x > 1e-6f;
}
void scanVoiceEntries() {
    if (voiceContainer && !alive(api.handleTarget(voiceContainer))) {
        resetVoiceDirection(false, false);
    }
    if (!voiceContainer) {
        auto container = findVoiceContainer();
        if (!container)
            return;
        voiceContainer = api.handleNew(container, false);
    }
    auto container = api.handleTarget(voiceContainer);
    std::vector<Obj> active;
    for (int i = 0, n = voiceChildCount(container); i < n; i++) {
        auto clone = voiceChildAt(container, i);
        if (nameOf(clone) != L"ESVoiceprint(Clone)" || !voiceActive(clone))
            continue;
        auto root = voiceChildNamed(clone, L"Root");
        if (root && voiceActive(root))
            active.push_back(root);
        if (active.size() > 16)
            throw std::runtime_error("Active voice entry bound");
    }
    for (size_t i = 0; i < voiceEntries.size();) {
        auto &entry = voiceEntries[i];
        auto root = api.handleTarget(entry.handle);
        if (std::find(active.begin(), active.end(), root) == active.end()) {
            restoreVoiceEntry(entry, yaw);
            api.handleFree(entry.handle);
            voiceEntries.erase(voiceEntries.begin() + i);
        } else
            i++;
    }
    for (auto root : active) {
        bool known = false;
        for (auto &entry : voiceEntries)
            if (api.handleTarget(entry.handle) == root) {
                known = true;
                break;
            }
        if (!known && voiceProjectionPositive(root)) {
            voiceEntries.push_back({api.handleNew(root, false), {}});
            log("Native voice entry acquired=%p; normal UI projection; angular correction only", root);
        }
    }
}
void nativeVoiceTick() {
    if (!enabled || !captured || uiBlocked || !foreground() || !saved.cameraHandle) {
        if (voiceContainer || !voiceEntries.empty())
            resetVoiceDirection(false, false);
        return;
    }
    if (!voiceBound || voiceFailed)
        return;
    try {
        auto now = GetTickCount64();
        if (!voiceLastScan || now - voiceLastScan >= 250) {
            voiceLastScan = now;
            scanVoiceEntries();
        }
        auto baseYaw = quaternionYaw(saved.rotation);
        if (!std::isfinite(baseYaw) || !std::isfinite(yaw))
            return;
        for (auto &entry : voiceEntries) {
            auto root = api.handleTarget(entry.handle);
            if (!voiceActive(root)) {
                restoreVoiceEntry(entry, yaw);
                continue;
            }
            float observed{}, corrected{};
            auto q = api.value<Quat>(voiceLocalRotation, root);
            if (!planarAngle(q, observed)) {
                restoreVoiceEntry(entry, yaw);
                continue;
            }
            if (!entry.direction.update(observed, yaw, baseYaw, corrected))
                continue;
            if (std::abs(wrappedDegrees(observed - corrected)) > .0001f)
                set(voiceSetLocalRotation, root, planarRotation(corrected));
            int frame = api.value<int>(getFrame);
            if (frame - voiceLastLog >= 600) {
                logDebug("Native voice angle observed=%.2f corrected=%.2f cameraYaw=%.2f originalYaw=%.2f activeEntries=%zu; visibility/position/alpha unchanged",
                    observed, corrected, yaw, baseYaw, voiceEntries.size());
                voiceLastLog = frame;
            }
        }
    } catch (const std::exception &e) {
        voiceFailed = true;
        logError("Optional native voice correction disabled: %s; native display retained until reload", e.what());
        resetVoiceDirection(false, false);
    }
}
void bindNativeVoice() {
    try {
        voiceChildren = api.require(transformClass, "get_childCount");
        voiceChild = api.require(transformClass, "GetChild", 1);
        voiceLocalRotation = api.require(transformClass, "get_localRotation");
        voiceSetLocalRotation = api.require(transformClass, "set_localRotation", 1);
        auto resources = api.find("UnityEngine.CoreModule.dll", "UnityEngine", "Resources");
        voiceFindAll = api.require(resources, "FindObjectsOfTypeAll", 1);
        voiceTransformPoint = api.require(transformClass, "TransformPoint", 1);
        auto canvas = api.find("UnityEngine.UIModule.dll", "UnityEngine", "Canvas");
        voiceCanvasMode = api.require(canvas, "get_renderMode");
        voiceBound = true;
    } catch (const std::exception &e) {
        voiceBound = false;
        logError("Native voice metadata unavailable: %s", e.what());
    }
    log("Native voice correction bound=%d; always enabled in shoulder view; only existing camera LateUpdate; no visibility writes",
        voiceBound);
}
