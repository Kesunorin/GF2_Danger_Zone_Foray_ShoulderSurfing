// Run only on the existing game Update callback, before the original scheduler.
// No interpreter compilation, scheduler invocation, forced object activation,
// scene initialization or change to loading budgets/spatial index/occlusion.
Class streamingDynamicClass{}, streamingLoaderClass{}, streamingConfigClass{}, streamingRegistryArrayClass{},
    streamingGoClass{}, streamingCameraClass{};
Field streamingDynamicRoot{}, streamingLoaderField{}, streamingConfigField{}, streamingMultiplier{},
    streamingRegistry{}, streamingLoaderRoot{}, streamingLoaderCamera{}, streamingDirty{};
std::array<Field, 5> streamingRadiusFields{};
unsigned streamingRootHandle{}, streamingGoHandle{}, streamingDynamicHandle{}, streamingLoaderHandle{},
    streamingConfigHandle{}, streamingArtHandle{}, streamingCameraHandle{};
StreamingDistanceOwnership streamingOwnership;
ULONGLONG streamingNextCheck{};
DWORD streamingThread{};
bool streamingBound{}, streamingFailed{};
void refreshStreaming(Obj loader) {
    // GDANINFMKFE is the native full distance-classification request. Update
    // tests it before PositionDirtyDistance and writes the classifier result
    // back to the same flag. This neither invokes Update nor moves the player.
    const unsigned char request = 1;
    memcpy(static_cast<char *>(loader) + 435, &request, 1);
    log("Native streaming refresh requested; next native Update reclassifies current position");
}
Obj streamingTarget(unsigned handle) {
    return handle ? api.handleTarget(handle) : nullptr;
}
void streamingHold(unsigned &handle, Obj object) {
    if (streamingTarget(handle) == object)
        return;
    auto replacement = object ? api.handleNew(object, false) : 0;
    if (object && !replacement)
        throw std::runtime_error("Streaming GC handle allocation failed");
    if (handle)
        api.handleFree(handle);
    handle = replacement;
}
StreamingRadii readStreamingRadii(Obj config) {
    StreamingRadii radii{};
    for (size_t i = 0; i < radii.size(); i++)
        radii[i] = api.read<float>(config, streamingRadiusFields[i]);
    return radii;
}
void writeStreamingRadii(Obj config, const StreamingRadii &radii) {
    static_assert(sizeof(StreamingRadii) == 20, "Streaming radii must contain exactly five Singles");
    // Binding verifies the five consecutive primitive Single fields at 16..32.
    // No managed references are written, so no GC write barrier is needed.
    memcpy(static_cast<char *>(config) + 16, radii.data(), sizeof(radii));
}
Obj streamingService() {
    auto array = api.global<Obj>(streamingRegistry);
    if (!array)
        return nullptr;
    if (healthClass(array) != streamingRegistryArrayClass)
        throw std::runtime_error("Streaming service registry array class changed");
    auto handle = api.handleNew(array, false);
    if (!handle)
        throw std::runtime_error("Streaming registry GC handle allocation failed");
    struct ScopedHandle {
        unsigned value;
        ~ScopedHandle() {
            api.handleFree(value);
        }
    } keep{handle};
    size_t count{};
    memcpy(&count, static_cast<char *>(array) + 24, 8);
    if (count > 256)
        throw std::runtime_error("Streaming service registry array bound");
    Obj result{};
    for (size_t i = 0; i < count; i++) {
        Obj service{};
        memcpy(&service, static_cast<char *>(array) + 32 + i * 8, 8);
        if (healthClass(service) == streamingDynamicClass) {
            if (result)
                throw std::runtime_error("Multiple registered streaming services");
            result = service;
        }
    }
    if (api.global<Obj>(streamingRegistry) != array)
        throw std::runtime_error("Streaming service registry changed during read");
    return result;
}
bool streamingRegistered() {
    if (!streamingThread || streamingThread != GetCurrentThreadId())
        return false;
    auto root = streamingTarget(streamingRootHandle), go = streamingTarget(streamingGoHandle),
         dynamic = streamingTarget(streamingDynamicHandle), loader = streamingTarget(streamingLoaderHandle),
         config = streamingTarget(streamingConfigHandle), art = streamingTarget(streamingArtHandle),
         camera = streamingTarget(streamingCameraHandle);
    return root && currentRoot() == root && alive(go) && api.read<Obj>(root, streamingDynamicRoot) == go &&
           healthClass(art) == streamingGoClass && alive(art) &&
           healthClass(camera) == streamingCameraClass && alive(camera) &&
           streamingTarget(saved.cameraHandle) == camera && streamingService() == dynamic &&
           healthClass(dynamic) == streamingDynamicClass && healthClass(loader) == streamingLoaderClass &&
           healthClass(config) == streamingConfigClass &&
           api.read<Obj>(dynamic, streamingLoaderField) == loader &&
           api.read<Obj>(loader, streamingLoaderRoot) == art &&
           api.read<Obj>(loader, streamingLoaderCamera) == camera &&
           api.read<Obj>(loader, streamingConfigField) == config;
}
void resetNativeStreaming(bool retry) {
    try {
        if (streamingOwnership.owned && streamingRegistered()) {
            auto config = streamingTarget(streamingConfigHandle);
            auto current = readStreamingRadii(config);
            StreamingRadii restored{};
            if (streamingOwnership.restore(current, api.read<float>(config, streamingMultiplier), restored)) {
                writeStreamingRadii(config, restored);
                refreshStreaming(streamingTarget(streamingLoaderHandle));
                log("Native streaming radii restored and native refresh requested");
            } else if (current != streamingOwnership.native) {
                log("Native streaming restoration skipped: fresh native values or radius order changed");
            }
        }
    } catch (const std::exception &ex) {
        logError("Native streaming cleanup skipped: %s", ex.what());
    }
    for (auto h : {streamingRootHandle, streamingGoHandle, streamingDynamicHandle, streamingLoaderHandle,
                   streamingConfigHandle, streamingArtHandle, streamingCameraHandle})
        if (h)
            api.handleFree(h);
    streamingRootHandle = streamingGoHandle = streamingDynamicHandle = streamingLoaderHandle =
        streamingConfigHandle = streamingArtHandle = streamingCameraHandle = 0;
    streamingOwnership = {};
    streamingThread = 0;
    streamingNextCheck = 0;
    if (retry)
        streamingFailed = false;
    streamingStatus(streamingFailed || !streamingBound ? 3 : cfg.streamingMultiplier > 1 && enabled ? 1 : 0);
}
void nativeStreamingTick() {
    if (streamingThread && streamingThread != GetCurrentThreadId())
        return;
    if (!enabled || cfg.streamingMultiplier <= 1) {
        if (streamingRootHandle)
            resetNativeStreaming(false);
        streamingStatus(cfg.streamingMultiplier > 1 ? 1 : 0);
        return;
    }
    if (!streamingBound || streamingFailed || !saved.cameraHandle || saved.first) {
        streamingStatus(!streamingBound || streamingFailed ? 3 : 1);
        return;
    }
    auto now = GetTickCount64();
    if (now < streamingNextCheck)
        return;
    streamingNextCheck = now + 500;
    try {
        auto root = currentRoot();
        if (!root)
            return;
        auto go = api.read<Obj>(root, streamingDynamicRoot);
        if (!alive(go))
            return;
        auto dynamic = streamingService();
        if (healthClass(dynamic) != streamingDynamicClass)
            return;
        auto loader = api.read<Obj>(dynamic, streamingLoaderField);
        if (healthClass(loader) != streamingLoaderClass)
            return;
        // Native initialization stores the supplied _ArtNode, not the separate
        // DK_DynamicLoadRoot. Bind the unique registered loader through its
        // native Camera reference and the actual camera held by this scene.
        auto art = api.read<Obj>(loader, streamingLoaderRoot),
             camera = api.read<Obj>(loader, streamingLoaderCamera);
        if (healthClass(art) != streamingGoClass || !alive(art) ||
            healthClass(camera) != streamingCameraClass || !alive(camera) ||
            camera != streamingTarget(saved.cameraHandle))
            return;
        auto config = api.read<Obj>(loader, streamingConfigField);
        if (healthClass(config) != streamingConfigClass)
            return;
        bool same =
            streamingTarget(streamingRootHandle) == root && streamingTarget(streamingGoHandle) == go &&
            streamingTarget(streamingDynamicHandle) == dynamic &&
            streamingTarget(streamingLoaderHandle) == loader &&
            streamingTarget(streamingConfigHandle) == config && streamingTarget(streamingArtHandle) == art &&
            streamingTarget(streamingCameraHandle) == camera;
        if (!same) {
            resetNativeStreaming(false);
            streamingThread = GetCurrentThreadId();
            streamingHold(streamingRootHandle, root);
            streamingHold(streamingGoHandle, go);
            streamingHold(streamingDynamicHandle, dynamic);
            streamingHold(streamingLoaderHandle, loader);
            streamingHold(streamingConfigHandle, config);
            streamingHold(streamingArtHandle, art);
            streamingHold(streamingCameraHandle, camera);
            log("Native streaming acquired: root=%p dynamicRoot=%p art=%p camera=%p loader=%p; registered loader camera matches current shoulder camera",
                root, go, art, camera, loader);
        }
        auto current = readStreamingRadii(config);
        float multiplier = api.read<float>(config, streamingMultiplier);
        if (streamingOwnership.owned) {
            if (!streamingOwnership.unchanged(current) || !validStreamingRadii(current, multiplier))
                throw std::runtime_error(
                    "Native streaming config changed; original fresh values retained, F9 retries");
            return;
        }
        StreamingRadii scaled{};
        if (!scaledStreamingRadii(current, multiplier, cfg.streamingMultiplier, scaled))
            throw std::runtime_error("Native streaming radii/multiplier outside verified ordering or limits");
        streamingOwnership.native = current;
        streamingOwnership.written = scaled;
        streamingOwnership.owned = true;
        writeStreamingRadii(config, scaled);
        refreshStreaming(loader);
        streamingStatus(2, scaled[2], scaled[3]);
        log("Native streaming factor=%.2f config=%p required=%.2f->%.2f near=%.2f->%.2f preload=%.2f->%.2f unload=%.2f->%.2f; native refresh requested, original budgets retained",
            cfg.streamingMultiplier, config, current[0], scaled[0], current[2], scaled[2], current[3],
            scaled[3], current[4], scaled[4]);
    } catch (const std::exception &ex) {
        streamingFailed = true;
        logError("Optional native streaming distance disabled: %s; other features retained", ex.what());
        resetNativeStreaming(false);
    }
}
void bindNativeStreaming() {
    try {
        streamingDynamicClass = api.find("Assembly-CSharp.dll", "", "JPENEFIDOEE");
        streamingLoaderClass = api.find("Assembly-CSharp.dll", "", "ANGJJNFCPGC");
        streamingConfigClass = api.find("Assembly-CSharp.dll", "", "LEHBAOFBNEK");
        if (api.isValueType(streamingConfigClass) || api.instanceSize(streamingConfigClass) != 200)
            throw std::runtime_error("Streaming config class/size changed");
        auto go = api.find("UnityEngine.CoreModule.dll", "UnityEngine", "GameObject"),
             single = api.find("mscorlib.dll", "System", "Single");
        streamingGoClass = go;
        streamingCameraClass = api.find("UnityEngine.CoreModule.dll", "UnityEngine", "Camera");
        auto field = [](Class owner, const char *name, Class type, int expected) {
            auto f = healthField(owner, name, !api.isValueType(type));
            int offset{};
            memcpy(&offset, static_cast<char *>(f) + 24, 4);
            if (offset != expected || healthDeclared(f) != type)
                throw std::runtime_error(std::string("Streaming field changed: ") + name);
            return f;
        };
        streamingDynamicRoot = field(rootClass, "DynamicLoadRoot", go, 152);
        streamingLoaderField = field(streamingDynamicClass, "KKPJPMHFKFL", streamingLoaderClass, 32);
        streamingConfigField = field(streamingLoaderClass, "GLNPDFLGKJB", streamingConfigClass, 360);
        streamingLoaderRoot = field(streamingLoaderClass, "<IKFPOMMHHBM>k__BackingField", go, 488);
        streamingLoaderCamera =
            field(streamingLoaderClass, "<CLMECPNCKBP>k__BackingField", streamingCameraClass, 480);
        auto manager = api.find("Assembly-CSharp.dll", "", "LPFJNJKGPFP"),
             serviceBase = api.find("Assembly-CSharp.dll", "", "BIEPDOIMCMB");
        streamingRegistry = api.field(manager, "FFGIEKMGIPN");
        streamingRegistryArrayClass = healthDeclared(streamingRegistry);
        if (!streamingRegistry || !(api.fieldFlags(streamingRegistry) & 16) || !streamingRegistryArrayClass ||
            api.elementSize(streamingRegistryArrayClass) != 8 ||
            api.elementClass(streamingRegistryArrayClass) != serviceBase)
            throw std::runtime_error("Streaming managed service registry changed");
        // JPENEFIDOEE is a managed service, not a Unity Component. Never use
        // GameObject.GetComponent or Unity's native liveness check on it.
        auto parent = reinterpret_cast<Class (*)(Class)>(api.exportAt(0x26));
        bool foundBase{};
        unsigned depth{};
        for (Class c = streamingDynamicClass; c && depth++ < 8; c = parent(c)) {
            if (c == serviceBase) {
                foundBase = true;
                break;
            }
            if (!strcmp(api.className(c), "Component"))
                throw std::runtime_error("Streaming service must be managed");
        }
        if (!foundBase)
            throw std::runtime_error("Streaming service base changed");
        const char *names[] = {"KIHDEAEAKIN", "IMMBKLGMGGK", "AKJLKJCLGOF", "DMKJAIHIPGK", "FPKLGNAAHKB"};
        for (size_t i = 0; i < streamingRadiusFields.size(); i++)
            streamingRadiusFields[i] = field(streamingConfigClass, names[i], single, int(16 + i * 4));
        streamingMultiplier = field(streamingConfigClass, "PHHIOPOBBCC", single, 172);
        auto boolean = api.find("mscorlib.dll", "System", "Boolean");
        streamingDirty = field(streamingLoaderClass, "GDANINFMKFE", boolean, 435);
        streamingBound = true;
    } catch (const std::exception &ex) {
        streamingBound = false;
        logError("Optional native streaming metadata unavailable: %s", ex.what());
    }
    log("Native streaming bound=%d; registered loader camera identity, separate art root, five linked radii, existing Update only",
        streamingBound);
}
