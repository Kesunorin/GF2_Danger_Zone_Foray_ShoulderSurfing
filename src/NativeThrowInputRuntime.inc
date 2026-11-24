// Feed the shoulder candidate BEFORE native item range/terrain processing.
// Native MILBMKMGJGB.ELCFJALJBAD -> HLCOBIHLLNM -> HPIAJADMIFI -> ThrowTargetPos.
// Input-time Camera has the temporary top-down pose; use the applied shoulder pose.
Class throwInputClass{};
Method throwInputUpdate{};
void *throwFrameEntry{};
using ThrowFrameEnter = void *(*)(void *, void *, void *);
ThrowFrameEnter originalThrowFrameEnter{};
bool throwInputBound{}, throwInputFailed{};
unsigned throwInputActorHandle{};
Vec3 throwInputCamera{}, throwInputForward{}, throwInputPlayer{};
ULONGLONG throwInputUpdated{}, throwInputLastLog{};
void resetNativeThrowInput() {
    if (throwInputActorHandle) {
        api.handleFree(throwInputActorHandle);
        throwInputActorHandle = 0;
    }
    throwInputUpdated = 0;
}
void bindNativeThrowInput(Class entity) {
    try {
        auto typeName = reinterpret_cast<const char *(*)(const void *)>(api.exportAt(0xd7));
        auto paramType = reinterpret_cast<const void *(*)(Method, unsigned)>(api.exportAt(0x85));
        throwInputClass = api.find("Assembly-CSharp.dll", "", "MILBMKMGJGB");
        auto input = api.find("Assembly-CSharp.dll", "", "AKGDBJLILIF");
        const auto inputType = std::string(typeName(api.classType(input))) + ".";
        if (std::string(typeName(api.classType(throwInputClass))).compare(0, inputType.size(), inputType))
            throw std::runtime_error("Throw range stage identity changed");
        throwInputUpdate = api.require(throwInputClass, "ELCFJALJBAD", 4);
        auto vectorClass = api.find("UnityEngine.CoreModule.dll", "UnityEngine", "Vector3");
        unsigned short methodFlags{};
        memcpy(&methodFlags, static_cast<const char *>(throwInputUpdate) + 68, 2);
        // ABI: this[0], candidate Vec3[1..2], native origin Vec3[3..4],
        // original item radius Single[5], current native entity[6].
        if (strcmp(typeName(paramType(throwInputUpdate, 0)), "UnityEngine.Vector3") ||
            strcmp(typeName(paramType(throwInputUpdate, 1)), "UnityEngine.Vector3") ||
            strcmp(typeName(paramType(throwInputUpdate, 2)), "System.Single") ||
            api.classFromType(paramType(throwInputUpdate, 3)) != entity ||
            api.classFromType(*(reinterpret_cast<const void *const *>(throwInputUpdate) + 4)) !=
                vectorClass ||
            (methodFlags & 0x10))
            throw std::runtime_error("Throw range signature changed");
        throwInputBound = true;
    } catch (const std::exception &e) {
        throwInputBound = false;
        logError("Optional quickbar throw input unavailable: %s", e.what());
    }
    log("Native quickbar throw input bound=%d; shoulder point before native item range/terrain",
        throwInputBound);
}
void cacheNativeThrowInput(Obj entity, Vec3 camera, Vec3 ray, Vec3 player) {
    if (!throwInputBound || throwInputFailed || !entity || !finite(camera) || !finite(ray) || !finite(player))
        return;
    if (!throwInputActorHandle || api.handleTarget(throwInputActorHandle) != entity) {
        resetNativeThrowInput();
        throwInputActorHandle = api.handleNew(entity, false);
    }
    throwInputCamera = camera;
    throwInputForward = ray;
    throwInputPlayer = player;
    throwInputUpdated = GetTickCount64();
}
bool applyNativeThrowInput(void *stack) {
    if (!throwInputBound || throwInputFailed || !enabled || !captured || !cfg.aim || uiBlocked ||
        !saved.cameraHandle || !foreground() || !throwInputActorHandle || !throwInputUpdated ||
        GetTickCount64() - throwInputUpdated > 250 || !stack)
        return false;
    Obj stage{}, entity{};
    Vec3 nativePoint{}, origin{}, target{};
    float radiusUnits{};
    memcpy(&stage, stack, 8);
    memcpy(&nativePoint, static_cast<char *>(stack) + 8, 12);
    memcpy(&origin, static_cast<char *>(stack) + 24, 12);
    memcpy(&radiusUnits, static_cast<char *>(stack) + 40, 4);
    memcpy(&entity, static_cast<char *>(stack) + 48, 8);
    if (!stage || *static_cast<Class *>(stage) != throwInputClass || !entity ||
        entity != api.handleTarget(throwInputActorHandle) || !finite(nativePoint) || !finite(origin) ||
        !std::isfinite(radiusUnits) || radiusUnits <= 0 || length(origin - throwInputPlayer) > 2.f ||
        !cameraGroundTarget(throwInputCamera, throwInputForward, origin, nativePoint.y, target))
        return false;
    // Only the 12-byte by-value candidate changes. Native origin, item radius,
    // entity, return value and all original limiting/terrain code stay intact.
    memcpy(static_cast<char *>(stack) + 8, &target, 12);
    auto now = GetTickCount64();
    if (now - throwInputLastLog >= 5000) {
        logDebug("Quickbar pre-range target=(%.3f %.3f %.3f) shoulder=(%.3f %.3f %.3f) nativeRadius=%.3fm; native range/terrain follows",
            nativePoint.x, nativePoint.y, nativePoint.z, target.x, target.y, target.z, radiusUnits / 100.f);
        throwInputLastLog = now;
    }
    return true;
}
void *throwFrameEnterHook(void *group, void *methodData, void *stack) {
    if (initialized && !faulted && !inMod && methodData) {
        auto method = *static_cast<Method *>(methodData);
        if (method != throwInputUpdate && !trackingFrameMethod(method) && !attackFrameMethod(method))
            return originalThrowFrameEnter(group, methodData, stack);
        inMod = true;
        if (attackFrameMethod(method)) {
            try {
                applyNativeAttackProjection(method, methodData, stack);
            } catch (const std::exception &e) {
                attackProjectionFailed = true;
                logError("Optional native attack projection disabled: %s; other features retained", e.what());
            }
        } else if (trackingFrameMethod(method)) {
            try {
                applyNativeTrackingCamera(method, methodData, stack);
            } catch (const std::exception &e) {
                trackingFailed = true;
                logError("Optional native tracking camera input disabled: %s; other features retained", e.what());
                try {
                    resetNativeTracking(false);
                } catch (...) {
                }
            }
        } else {
            try {
                applyNativeThrowInput(stack);
            } catch (const std::exception &e) {
                throwInputFailed = true;
                logError("Optional quickbar throw input disabled: %s", e.what());
                try {
                    resetNativeThrowInput();
                } catch (...) {
                }
            }
        }
        inMod = false;
    }
    return originalThrowFrameEnter(group, methodData, stack);
}
void queueNativeThrowInput() {
    if (!throwInputBound && !trackingBound && !attackProjectionBound)
        return;
    // Shared by precise optional throw, tracking and attack projection filters.
    try {
        // Invoker comes from the current managed camera/root methods. The entry
        // is resolved through its direct native call graph, never a fixed RVA.
        auto follow = api.find("Assembly-CSharp.dll", "GF.DarKov", "DarKovCameraFollow");
        auto currentInvoker = Api::invoker(api.require(follow, "LateUpdate"));
        throwFrameEntry = runtime::resolveFrameEntry(api.exportResolver->image(), currentInvoker);
        log("Dynamic shared frame entry RVA=%zx; current Invoker call graph and frame ABI validated",
            static_cast<unsigned char *>(throwFrameEntry) - api.exportResolver->image().base());
    } catch (const std::exception &e) {
        throwInputBound = trackingBound = attackProjectionBound = false;
        logError("Optional throw/tracking/attack frame entry unavailable: %s; core camera retained", e.what());
        return;
    }
    auto status = MH_CreateHook(throwFrameEntry, reinterpret_cast<void *>(&throwFrameEnterHook),
                                reinterpret_cast<void **>(&originalThrowFrameEnter));
    if (status != MH_OK) {
        throwInputBound = trackingBound = attackProjectionBound = false;
        logError("Optional throw/tracking/attack hook creation failed=%d", int(status));
        return;
    }
    status = MH_QueueEnableHook(throwFrameEntry);
    if (status != MH_OK) {
        MH_RemoveHook(throwFrameEntry);
        throwInputBound = trackingBound = attackProjectionBound = false;
        logError("Optional throw/tracking/attack hook queue failed=%d", int(status));
    }
}
