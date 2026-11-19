// Give the original tracking call a stable shoulder projection Camera. Its
// GameObject is inactive and Camera disabled: it never renders or becomes main.
// Native visibility, arrow and animation updates execute only once per call.
Class trackingOwnerClass{}, trackingWidgetClass{}, trackingCameraClass{}, trackingGameObjectClass{};
Method trackingProjection{}, trackingConstruct{}, trackingSetActive{}, trackingAddCamera{},
    trackingCopyCamera{}, trackingSetEnabled{}, trackingHideFlags{}, trackingDestroy{}, trackingResetView{};
Field trackingMap{}, trackingTask{}, trackingRoot{};
Obj (*trackingNewObject)(Class){};
bool trackingBound{}, trackingFailed{};
DWORD trackingThread{};
unsigned trackingSceneHandle{}, trackingSourceHandle{}, trackingObjectHandle{}, trackingCameraHandle{};
ULONGLONG trackingUpdated{}, trackingLastLog{};
int trackingSnapshotFrame = -1;
bool trackingReady{};
Obj trackingTarget(unsigned handle) {
    return handle ? api.handleTarget(handle) : nullptr;
}
void trackingHold(unsigned &handle, Obj obj) {
    if (trackingTarget(handle) == obj)
        return;
    unsigned replacement = obj ? api.handleNew(obj, false) : 0;
    if (obj && !replacement)
        throw std::runtime_error("Tracking GC handle allocation failed");
    if (handle)
        api.handleFree(handle);
    handle = replacement;
}
void resetNativeTracking(bool retry) {
    trackingReady = false;
    trackingUpdated = 0;
    trackingSnapshotFrame = -1;
    trackingThread = 0;
    auto object = trackingTarget(trackingObjectHandle);
    if (object) {
        try {
            if (alive(object)) {
                // The private object was inactive before adding the Camera.
                // Destroy is deferred by Unity; it remains inactive meanwhile.
                void *args[] = {object};
                api.call(trackingDestroy, nullptr, args);
            }
        } catch (const std::exception &ex) {
            logError("Native tracking projection camera cleanup skipped: %s", ex.what());
        }
    }
    for (auto h : {trackingSceneHandle, trackingSourceHandle, trackingObjectHandle, trackingCameraHandle})
        if (h)
            api.handleFree(h);
    trackingSceneHandle = trackingSourceHandle = trackingObjectHandle = trackingCameraHandle = 0;
    if (retry)
        trackingFailed = false;
}
bool trackingAllowed() {
    return trackingBound && !trackingFailed && enabled && !uiBlocked && foreground() && saved.cameraHandle &&
           !saved.first;
}
bool trackingFrameMethod(Method method) {
    return trackingBound && method == trackingProjection;
}
void applyNativeTrackingCamera(Method method, void *data, void *stack) {
    if (method != trackingProjection || !trackingAllowed() || !trackingReady || !trackingThread ||
        GetCurrentThreadId() != trackingThread || !stack || !data || GetTickCount64() - trackingUpdated > 250)
        return;
    unsigned argc{}, slots{};
    memcpy(&argc, static_cast<char *>(data) + 16, 4);
    memcpy(&slots, static_cast<char *>(data) + 20, 4);
    if (argc != 11 || slots != 15)
        throw std::runtime_error("Tracking interpreter argument layout changed");
    Obj owner{}, widget{}, source{};
    auto bytes = static_cast<char *>(stack);
    memcpy(&owner, bytes, 8);
    memcpy(&widget, bytes + 8, 8);
    memcpy(&source, bytes + 80, 8);
    if (healthClass(owner) != trackingOwnerClass || healthClass(widget) != trackingWidgetClass ||
        healthClass(source) != trackingCameraClass || source != trackingTarget(trackingSourceHandle) ||
        source != trackingTarget(saved.cameraHandle) ||
        currentRoot() != trackingTarget(trackingSceneHandle) ||
        (api.read<Obj>(owner, trackingMap) != widget && api.read<Obj>(owner, trackingTask) != widget) ||
        !alive(api.read<Obj>(widget, trackingRoot)))
        return;
    auto projection = trackingTarget(trackingCameraHandle);
    if (!alive(projection) || healthClass(projection) != trackingCameraClass)
        return;
    // Only the original call's by-value Camera reference changes. Native inputs,
    // target key, strings, range/height helpers, output and animation are untouched.
    memcpy(bytes + 80, &projection, 8);
    if (GetTickCount64() - trackingLastLog >= 5000) {
        logDebug("Native tracking camera input: snapshotFrame=%d; single native projection/UI update, inactive camera",
            trackingSnapshotFrame);
        trackingLastLog = GetTickCount64();
    }
}
void nativeTrackingTick(Obj root, Obj camera, int frame) {
    if (!trackingAllowed()) {
        resetNativeTracking(false);
        return;
    }
    if (trackingReady && trackingSnapshotFrame == frame)
        return;
    try {
        if (trackingTarget(trackingSceneHandle) != root || trackingTarget(trackingSourceHandle) != camera ||
            !alive(trackingTarget(trackingCameraHandle))) {
            resetNativeTracking(false);
            trackingThread = GetCurrentThreadId();
            trackingHold(trackingSceneHandle, root);
            trackingHold(trackingSourceHandle, camera);
            auto object = trackingNewObject(trackingGameObjectClass);
            if (!object)
                throw std::runtime_error("Tracking projection GameObject allocation failed");
            trackingHold(trackingObjectHandle, object);
            api.call(trackingConstruct, object);
            bool inactive = false;
            void *activeArgs[] = {&inactive};
            api.call(trackingSetActive, object, activeArgs);
            int flags = 61; // Verified Unity HideFlags.HideAndDontSave.
            void *flagArgs[] = {&flags};
            api.call(trackingHideFlags, object, flagArgs);
            void *typeArgs[] = {cameraType};
            auto projection = api.call(trackingAddCamera, object, typeArgs);
            if (!alive(projection) || healthClass(projection) != trackingCameraClass)
                throw std::runtime_error("Tracking projection Camera creation failed");
            trackingHold(trackingCameraHandle, projection);
            api.call(trackingSetEnabled, projection, activeArgs);
            log("Native tracking projection camera created; GameObject inactive, Camera disabled, no render/UI replay");
        }
        trackingReady = false;
        auto projection = trackingTarget(trackingCameraHandle);
        void *copyArgs[] = {camera};
        api.call(trackingCopyCamera, projection, copyArgs);
        bool disabled = false;
        void *disabledArgs[] = {&disabled};
        api.call(trackingSetEnabled, projection, disabledArgs);
        auto sourceTransform = api.call(componentTransform, camera),
             projectionTransform = api.call(componentTransform, projection);
        auto position = api.value<Vec3>(getPosition, sourceTransform);
        auto orientation = api.value<Quat>(getRotation, sourceTransform);
        if (!finite(position))
            throw std::runtime_error("Tracking projection camera position unavailable");
        set(setPosition, projectionTransform, position);
        set(setRotation, projectionTransform, orientation);
        api.call(trackingResetView, projection);
        // Check copying on the actual Unity thread. Inactive camera projections
        // must agree with the rendered shoulder camera, including ADS/viewport.
        auto center = position + forward(yaw, pitch) * 10.f;
        for (auto world : {center, center + right(yaw) * 2.f + Vec3{0, 1, 0}}) {
            void *args[] = {&world};
            auto actual = api.value<Vec3>(worldToScreen, camera, args),
                 copied = api.value<Vec3>(worldToScreen, projection, args);
            if (!finite(actual) || !finite(copied) || length(actual - copied) > .05f)
                throw std::runtime_error("Tracking inactive camera projection mismatch");
        }
        trackingUpdated = GetTickCount64();
        trackingSnapshotFrame = frame;
        trackingThread = GetCurrentThreadId();
        trackingReady = true;
    } catch (const std::exception &ex) {
        trackingFailed = true;
        logError("Optional native tracking camera disabled: %s; original projection and other features retained",
            ex.what());
        resetNativeTracking(false);
    }
}
void bindNativeTracking() {
    try {
        trackingOwnerClass = api.find("Assembly-CSharp.dll", "", "EKPDPMELNNL");
        trackingWidgetClass = api.find("Assembly-CSharp.dll", "", "ENEOIFPIAGH");
        trackingCameraClass = api.find("UnityEngine.CoreModule.dll", "UnityEngine", "Camera");
        trackingGameObjectClass = api.find("UnityEngine.CoreModule.dll", "UnityEngine", "GameObject");
        auto object = api.find("UnityEngine.CoreModule.dll", "UnityEngine", "Object"),
             behaviour = api.find("UnityEngine.CoreModule.dll", "UnityEngine", "Behaviour");
        auto field = [](Class owner, const char *name, Class type, int expected) {
            auto f = healthField(owner, name);
            int offset{};
            memcpy(&offset, static_cast<char *>(f) + 24, 4);
            // These references are read via metadata, so a member's relocation
            // inside the owning object does not require a new mod version.
            (void)expected;
            if (offset < 16 || offset > api.instanceSize(owner) - 8 || healthDeclared(f) != type)
                throw std::runtime_error(std::string("Tracking field changed: ") + name);
            return f;
        };
        trackingMap = field(trackingOwnerClass, "CBOGJDBIAFD", trackingWidgetClass, 56);
        trackingTask = field(trackingOwnerClass, "HOGMPKGBNOF", trackingWidgetClass, 64);
        auto rectTransform = api.find("UnityEngine.CoreModule.dll", "UnityEngine", "RectTransform");
        trackingRoot = field(trackingWidgetClass, "LDFGNLHGNCM", rectTransform, 16);
        auto require = [](Class c, const char *name, const std::vector<const char *> &types,
                          const char *result = "System.Void", bool isStatic = false) {
            auto m = api.requireTyped(c, name, types, result, isStatic);
            auto paramType = reinterpret_cast<const void *(*)(Method, unsigned)>(api.exportAt(0x85));
            auto paramCount = reinterpret_cast<unsigned (*)(Method)>(api.exportAt(0x86));
            unsigned short flags{};
            memcpy(&flags, static_cast<const char *>(m) + 68, 2);
            if (bool(flags & 16) != isStatic || paramCount(m) != types.size() ||
                !api.equivalent(result, api.methodResult(m)))
                throw std::runtime_error(std::string("Tracking method signature changed: ") + name);
            for (unsigned i = 0; i < types.size(); i++)
                if (!api.equivalent(types[i], paramType(m, i)))
                    throw std::runtime_error(std::string("Tracking parameter type changed: ") + name);
            return m;
        };
        trackingProjection =
            require(trackingOwnerClass, "MECAMDGPIBG",
                    {"ENEOIFPIAGH", "UnityEngine.Vector3", "UnityEngine.Vector3", "UnityEngine.Vector3",
                     "System.String", "System.String", "UnityEngine.Camera", "UnityEngine.Rect",
                     "System.Single", "System.Single"});
        trackingConstruct = require(trackingGameObjectClass, ".ctor", {});
        trackingSetActive = require(trackingGameObjectClass, "SetActive", {"System.Boolean"});
        trackingAddCamera =
            require(trackingGameObjectClass, "AddComponent", {"System.Type"}, "UnityEngine.Component");
        trackingCopyCamera = require(trackingCameraClass, "CopyFrom", {"UnityEngine.Camera"});
        trackingSetEnabled = require(behaviour, "set_enabled", {"System.Boolean"});
        trackingResetView = require(trackingCameraClass, "ResetWorldToCameraMatrix", {});
        trackingHideFlags = require(object, "set_hideFlags", {"UnityEngine.HideFlags"});
        trackingDestroy = require(object, "Destroy", {"UnityEngine.Object"}, "System.Void", true);
        auto hideFlags = api.find("UnityEngine.CoreModule.dll", "UnityEngine", "HideFlags");
        auto hidden = api.field(hideFlags, "HideAndDontSave");
        if (!hidden || api.global<int>(hidden) != 61)
            throw std::runtime_error("Tracking HideFlags enum changed");
        trackingNewObject = reinterpret_cast<decltype(trackingNewObject)>(api.exportAt(0x99));
        trackingBound = true;
    } catch (const std::exception &ex) {
        trackingBound = false;
        logError("Optional native tracking metadata unavailable: %s", ex.what());
    }
    log("Native tracking bound=%d; registered map/task Camera argument, projection15 slots; single native UI update, shared hook",
        trackingBound);
}
