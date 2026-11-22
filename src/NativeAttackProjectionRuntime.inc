// The shared native HUD helper projects its by-value world point into UI.
// Only the verified model-anchor input of a registered attack dot is remapped.
// Re-express that point through its original camera so its ONE native UI update
// lands at the final shoulder projection. Target/entity/animation stay native.
Class attackProjectionClass{}, attackItemClass{}, attackDictionaryClass{}, attackArrayClass{},
    attackUiClass{};
Field attackItems{}, attackCount{}, attackVersion{}, attackArray{}, attackEntity{}, attackClone{},
    attackDelegate{}, attackDelegateOwner{}, attackRect{}, attackUiInstance{}, attackSourceCamera{};
Method attackProjectionMethod{};
bool attackProjectionBound{}, attackProjectionFailed{};
ULONGLONG attackLastLog{};
void resetNativeAttackProjection(bool retry) {
    if (retry)
        attackProjectionFailed = false;
}
bool attackFrameMethod(Method method) {
    return attackProjectionBound && method == attackProjectionMethod;
}
bool attackOwnerRegistered(Obj owner, Vec3 world) {
    auto service = api.global<Obj>(interactInstance);
    if (healthClass(service) != interactOwnerClass || healthClass(owner) != attackProjectionClass)
        return false;
    auto rect = healthRef(owner, attackRect);
    if (!alive(rect) || nameOf(rect) != L"HUDRoot")
        return false;
    auto dictionary = healthRef(service, attackItems);
    if (healthClass(dictionary) != attackDictionaryClass)
        return false;
    int count = api.read<int>(dictionary, attackCount), version = api.read<int>(dictionary, attackVersion);
    auto array = healthRef(dictionary, attackArray);
    if (!array || count == 0)
        return false;
    size_t capacity{};
    memcpy(&capacity, static_cast<char *>(array) + 24, 8);
    if (count < 0 || count > 256 || healthClass(array) != attackArrayClass || capacity > 256 ||
        size_t(count) > capacity || api.elementSize(attackArrayClass) != 24)
        throw std::runtime_error("Attack marker dictionary layout changed");
    auto hold = api.handleNew(array, false);
    if (!hold)
        throw std::runtime_error("Attack marker registry GC hold unavailable");
    struct Hold {
        unsigned h;
        ~Hold() {
            api.handleFree(h);
        }
    } keep{hold};
    bool found = false;
    for (int i = 0; i < count; ++i) {
        auto bytes = static_cast<char *>(array) + 32 + i * 24;
        int hash{};
        Obj item{};
        memcpy(&hash, bytes, 4);
        memcpy(&item, bytes + 16, 8);
        if (hash < 0 || healthClass(item) != attackItemClass)
            continue;
        auto entity = healthRef(item, attackEntity), clone = healthRef(item, attackClone),
             delegate = healthRef(item, attackDelegate);
        if (healthClass(entity) == healthEntityClass && delegate &&
            healthRef(delegate, attackDelegateOwner) == owner && alive(clone) && voiceActive(clone) &&
            nameOf(clone) == L"ESCanAttackDot(Clone)" && api.call(getParent, clone) == rect) {
            unsigned key{};
            memcpy(&key, bytes + 8, 4);
            if (api.read<unsigned>(entity, interactEntityId) != key)
                continue;
            // NOFKBPLABMA is also used by player bars and unrelated HUD dots.
            // The captured explosive marker uses the original model pivot.
            // Preserve that exact native anchor; never infer ownership by owner alone.
            auto model = interactModelObject(entity);
            if (!alive(model))
                continue;
            auto anchor = api.value<Vec3>(getPosition, api.call(getTransform, model));
            if (finite(anchor) && length(anchor - world) <= .01f && interactModelObject(entity) == model)
                found = true;
        }
    }
    return found && version == api.read<int>(dictionary, attackVersion) &&
           array == healthRef(dictionary, attackArray) && dictionary == healthRef(service, attackItems) &&
           api.global<Obj>(interactInstance) == service;
}
bool applyNativeAttackProjection(Method method, void *data, void *stack) {
    if (!attackFrameMethod(method) || attackProjectionFailed || !cfg.interactFix || !enabled || !captured ||
        uiBlocked || !foreground() || !trackingReady || !trackingAllowed() || !trackingThread ||
        GetCurrentThreadId() != trackingThread || GetTickCount64() - trackingUpdated > 250 || !data ||
        !stack || currentRoot() != trackingTarget(trackingSceneHandle))
        return false;
    unsigned count{}, slots{};
    memcpy(&count, static_cast<char *>(data) + 16, 4);
    memcpy(&slots, static_cast<char *>(data) + 20, 4);
    if (count != 2 || slots != 3)
        throw std::runtime_error("Attack projection interpreter layout changed");
    Obj owner{};
    Vec3 world{};
    memcpy(&owner, stack, 8);
    memcpy(&world, static_cast<char *>(stack) + 8, 12);
    if (!finite(world) || !attackOwnerRegistered(owner, world))
        return false;
    auto manager = api.global<Obj>(attackUiInstance), projection = trackingTarget(trackingCameraHandle);
    if (healthClass(manager) != attackUiClass || !alive(projection) ||
        healthClass(projection) != trackingCameraClass)
        return false;
    // Native W2S uses GKMJHHEGAAP / HJPINKNIDEH (+240). GJHOGJMNJFN
    // (+232) is the UI camera used by the following rectangle conversion.
    // Use the actually observed native W2S camera, not the UI camera.
    auto source = healthRef(manager, attackSourceCamera);
    if (!alive(source) || healthClass(source) != trackingCameraClass)
        return false;
    void *args[] = {&world};
    auto desired = api.value<Vec3>(worldToScreen, projection, args),
         nativeScreen = api.value<Vec3>(worldToScreen, source, args);
    if (!finite(desired) || !finite(nativeScreen) || nativeScreen.z <= .0001f)
        return false;
    // W2S mirrors behind-camera targets into otherwise valid screen coordinates.
    // The native helper's Vector3 -> HUD conversion loses this signed depth, so
    // returning without a rewrite would leave the original top-down dot visible.
    // Move only this registered marker's point outside the canvas, as for bars;
    // the next front-facing projection restores it without changing active/alpha.
    bool behind = desired.z <= .08f;
    if (behind) {
        Vec3 outside{};
        if (!headBarScreenPoint(desired, api.value<int>(getWidth), api.value<int>(getHeight), outside))
            return false;
        desired = outside;
    }
    Vec3 throughNative{desired.x, desired.y, nativeScreen.z};
    void *unproject[] = {&throughNative};
    auto remapped = api.value<Vec3>(screenToWorld, source, unproject);
    if (!finite(remapped))
        return false;
    void *check[] = {&remapped};
    auto verified = api.value<Vec3>(worldToScreen, source, check);
    // Actual Unity float projections at this map's coordinates produced about
    // 0.061px round-trip error. Keep a subpixel bound while allowing roundoff.
    if (!finite(verified) || std::hypot(verified.x - desired.x, verified.y - desired.y) > .25f ||
        api.global<Obj>(attackUiInstance) != manager || healthRef(manager, attackSourceCamera) != source)
        return false;
    // Only the helper's 12-byte value argument changes. Native world/target data
    // is not stored back, and its UI conversion, visibility and animation run once.
    memcpy(static_cast<char *>(stack) + 8, &remapped, 12);
    if (GetTickCount64() - attackLastLog >= 5000) {
        logDebug(
            "Native attack marker projection: world=(%.3f %.3f %.3f) shoulderScreen=(%.2f %.2f) depth=%.3f behind=%d; one native UI update",
            world.x, world.y, world.z, desired.x, desired.y, desired.z, behind);
        attackLastLog = GetTickCount64();
    }
    return true;
}
void bindNativeAttackProjection() {
    try {
        if (!interactBound || !trackingBound)
            throw std::runtime_error("Shared interaction/projection camera metadata unavailable");
        attackProjectionClass = api.find("Assembly-CSharp.dll", "", "DHCHPCDALBJ");
        attackItemClass = api.find("Assembly-CSharp.dll", "", "FKOECNCFPHC");
        auto rect = api.find("UnityEngine.CoreModule.dll", "UnityEngine", "RectTransform"),
             transform = api.find("UnityEngine.CoreModule.dll", "UnityEngine", "Transform");
        auto field = [](Class c, const char *name, Class type, int expected) {
            auto f = healthField(c, name);
            int offset{};
            memcpy(&offset, static_cast<char *>(f) + 24, 4);
            if (offset != expected || (type && healthDeclared(f) != type))
                throw std::runtime_error(std::string("Attack marker field changed: ") + name);
            return f;
        };
        attackRect = field(attackProjectionClass, "DCCHDHMDJMM", rect, 40);
        attackItems = field(interactOwnerClass, "IOAEGBJLNAP", nullptr, 72);
        attackDictionaryClass = healthDeclared(attackItems);
        attackCount = healthField(attackDictionaryClass, "count", false);
        attackVersion = healthField(attackDictionaryClass, "version", false);
        attackArray = healthField(attackDictionaryClass, "entries");
        attackArrayClass = healthDeclared(attackArray);
        auto element = api.elementClass(attackArrayClass);
        field(element, "value", attackItemClass, 32);
        if (api.elementSize(attackArrayClass) != 24)
            throw std::runtime_error("Attack marker dictionary stride changed");
        attackEntity = field(attackItemClass, "<JACGEKHOMFD>k__BackingField", healthEntityClass, 16);
        attackClone = field(attackItemClass, "HDFCFFBCCLD", transform, 64);
        attackDelegate = field(attackItemClass, "INMNEPFAJPP", nullptr, 24);
        attackDelegateOwner = healthField(healthDeclared(attackDelegate), "m_target");
        attackUiClass = api.find("Assembly-CSharp.dll", "", "GFLCGAOBBKC");
        auto parent = reinterpret_cast<Class (*)(Class)>(api.exportAt(0x26));
        auto singleton = parent(parent(attackUiClass));
        attackUiInstance = api.field(singleton, "BOABAJCEPNO");
        if (!attackUiInstance || !(api.fieldFlags(attackUiInstance) & 16) ||
            healthDeclared(attackUiInstance) != attackUiClass)
            throw std::runtime_error("Attack projection singleton changed");
        attackSourceCamera = field(attackUiClass, "HJPINKNIDEH", trackingCameraClass, 240);
        attackProjectionMethod = api.require(attackProjectionClass, "NOFKBPLABMA", 1);
        auto params = reinterpret_cast<const void *(*)(Method, unsigned)>(api.exportAt(0x85));
        auto vector = api.find("UnityEngine.CoreModule.dll", "UnityEngine", "Vector3");
        unsigned short flags{};
        memcpy(&flags, static_cast<const char *>(attackProjectionMethod) + 68, 2);
        if ((flags & 16) || api.classFromType(params(attackProjectionMethod, 0)) != vector ||
            api.classFromType(*(reinterpret_cast<const void *const *>(attackProjectionMethod) + 4)) != vector)
            throw std::runtime_error("Attack projection signature changed");
        attackProjectionBound = true;
    } catch (const std::exception &e) {
        attackProjectionBound = false;
        logError("Optional native attack marker projection unavailable: %s", e.what());
    }
    log("Native attack marker projection bound=%d; registered model-anchor attack input, native scene camera240 and single UI update",
        attackProjectionBound);
}
