// Keep only registered native _Roof fade groups visible in shoulder view.
// Borrow their hidden endpoint; requests, membership and renderer enablement
// remain native. Native cached fade/aggregation methods apply and restore it.
Class roofOwnerClass{}, roofFadeClass{}, roofSetClass{}, roofSlotArrayClass{}, roofRendererListClass{},
    roofRendererArrayClass{};
Field roofRegistry{}, roofSlots{}, roofLast{}, roofVersion{}, roofOwnerFade{}, roofVisible{}, roofHidden{},
    roofCurrent{}, roofRenderers{}, roofListCount{}, roofListItems{};
Method roofApplyFade{}, roofRecompute{};
bool roofBound{}, roofFailed{};
DWORD roofThread{};
unsigned roofRootHandle{};
ULONGLONG roofNextScan{}, roofLastLog{};
struct RoofEntry {
    unsigned owner{}, fade{};
    RoofFadeEndpoint endpoint;
};
std::vector<RoofEntry> roofEntries;
Obj roofTarget(unsigned handle) {
    return handle ? api.handleTarget(handle) : nullptr;
}
bool roofBinding(const RoofEntry &entry, Obj &fade) {
    auto owner = roofTarget(entry.owner);
    fade = roofTarget(entry.fade);
    return healthClass(owner) == roofOwnerClass && alive(owner) && healthClass(fade) == roofFadeClass &&
           alive(fade) && api.read<Obj>(owner, roofOwnerFade) == fade;
}
bool roofCached(Method method) {
    // Never request compilation of an uncached HybridCLR body. These two
    // methods were read from naturally cached native roof execution.
    void *data{};
    if (method)
        memcpy(&data, static_cast<const char *>(method) + 80, 8);
    if (!data || *static_cast<Method *>(data) != method)
        return false;
    unsigned count{}, slots{};
    memcpy(&count, static_cast<char *>(data) + 16, 4);
    memcpy(&slots, static_cast<char *>(data) + 20, 4);
    return count == 3 && slots == 3;
}
void freeRoofEntry(RoofEntry &entry) {
    for (auto handle : {entry.owner, entry.fade})
        if (handle)
            api.handleFree(handle);
    entry = {};
}
void restoreRoofEntry(RoofEntry &entry) {
    Obj fade{};
    float original{};
    if (!entry.endpoint.owned || !roofBinding(entry, fade) ||
        !entry.endpoint.restore(api.read<float>(fade, roofHidden), original))
        return;
    memcpy(static_cast<char *>(fade) + 44, &original, 4);
    if (roofThread == GetCurrentThreadId() && roofCached(roofRecompute)) {
        // Reconsider CURRENT native requests, not a captured old hidden flag.
        bool force = true, nativeOption = false;
        void *args[] = {&force, &nativeOption};
        api.call(roofRecompute, fade, args);
        log("Native roof endpoint restored: fade=%p hidden=%.3f; current native requests reevaluated", fade,
            original);
    }
}
void resetNativeRoof(bool retry) {
    for (auto &entry : roofEntries) {
        try {
            restoreRoofEntry(entry);
        } catch (const std::exception &e) {
            logError("Native roof restoration skipped: %s", e.what());
        }
        freeRoofEntry(entry);
    }
    roofEntries.clear();
    if (roofRootHandle)
        api.handleFree(roofRootHandle);
    roofRootHandle = 0;
    roofThread = 0;
    roofNextScan = 0;
    if (retry)
        roofFailed = false;
}
std::vector<Obj> registeredRoofs() {
    auto registry = api.global<Obj>(roofRegistry);
    if (!registry)
        return {};
    if (healthClass(registry) != roofSetClass)
        throw std::runtime_error("Roof registry class changed");
    int count = api.read<int>(registry, roofLast), version = api.read<int>(registry, roofVersion);
    auto slots = api.read<Obj>(registry, roofSlots);
    if (count < 0 || count > 2048)
        throw std::runtime_error("Roof registry count bound");
    if (!count)
        return {};
    size_t capacity{};
    if (healthClass(slots) != roofSlotArrayClass || api.elementSize(roofSlotArrayClass) != 16)
        throw std::runtime_error("Roof registry slots layout changed");
    memcpy(&capacity, static_cast<char *>(slots) + 24, 8);
    if (capacity > 4096 || size_t(count) > capacity)
        throw std::runtime_error("Roof registry capacity bound");
    std::vector<Obj> owners;
    for (int i = 0; i < count; ++i) {
        auto item = static_cast<char *>(slots) + 32 + i * 16;
        int hash{};
        Obj owner{};
        memcpy(&hash, item, 4);
        memcpy(&owner, item + 8, 8);
        if (hash >= 0 && healthClass(owner) == roofOwnerClass)
            owners.push_back(owner);
    }
    if (version != api.read<int>(registry, roofVersion) || api.read<Obj>(registry, roofSlots) != slots ||
        api.global<Obj>(roofRegistry) != registry)
        throw std::runtime_error("Roof registry changed during read");
    return owners;
}
bool eligibleRoof(Obj owner, Obj &fade) {
    if (!alive(owner))
        return false;
    auto readParent = [](Obj node) { return alive(node) ? api.call(getParent, node) : nullptr; };
    auto readName = [](Obj node) { return nameOf(node); };
    if (!nativeRoofHierarchy(api.call(componentTransform, owner), readName, readParent))
        return false;
    fade = api.read<Obj>(owner, roofOwnerFade);
    if (healthClass(fade) != roofFadeClass || !alive(fade) ||
        !nativeRoofHierarchy(api.call(componentTransform, fade), readName, readParent))
        return false;
    auto renderers = api.read<Obj>(fade, roofRenderers);
    if (healthClass(renderers) != roofRendererListClass)
        return false;
    int count = api.read<int>(renderers, roofListCount);
    auto items = api.read<Obj>(renderers, roofListItems);
    size_t capacity{};
    if (count < 1 || count > 4096 || healthClass(items) != roofRendererArrayClass ||
        api.elementSize(roofRendererArrayClass) != 8)
        return false;
    memcpy(&capacity, static_cast<char *>(items) + 24, 8);
    return size_t(count) <= capacity && capacity <= 8192;
}
void nativeRoofTick(Obj root) {
    if (!roofBound || roofFailed || !enabled || !saved.cameraHandle || saved.first) {
        if (!roofEntries.empty())
            resetNativeRoof(false);
        return;
    }
    if (roofThread && roofThread != GetCurrentThreadId())
        return;
    if (roofTarget(roofRootHandle) != root) {
        resetNativeRoof(false);
        roofRootHandle = api.handleNew(root, false);
        if (!roofRootHandle)
            return;
        roofThread = GetCurrentThreadId();
    }
    auto now = GetTickCount64();
    if (now < roofNextScan)
        return;
    roofNextScan = now + 250;
    try {
        if (!roofCached(roofApplyFade) || !roofCached(roofRecompute))
            return;
        auto owners = registeredRoofs();
        for (auto it = roofEntries.begin(); it != roofEntries.end();) {
            Obj fade{};
            auto owner = roofTarget(it->owner);
            if (std::find(owners.begin(), owners.end(), owner) == owners.end() || !roofBinding(*it, fade)) {
                restoreRoofEntry(*it);
                freeRoofEntry(*it);
                it = roofEntries.erase(it);
            } else
                ++it;
        }
        for (auto owner : owners) {
            Obj fade{};
            if (!eligibleRoof(owner, fade))
                continue;
            auto it = std::find_if(roofEntries.begin(), roofEntries.end(), [owner](const RoofEntry &entry) {
                return roofTarget(entry.owner) == owner;
            });
            if (it == roofEntries.end()) {
                if (roofEntries.size() >= 128)
                    continue;
                RoofEntry entry;
                entry.owner = api.handleNew(owner, false);
                entry.fade = api.handleNew(fade, false);
                if (!entry.owner || !entry.fade) {
                    freeRoofEntry(entry);
                    throw std::runtime_error("Roof GC handle allocation failed");
                }
                roofEntries.push_back(entry);
                it = roofEntries.end() - 1;
                log("Native roof acquired: owner=%ls fade=%p renderers=%d", nameOf(owner).c_str(), fade,
                    api.read<int>(api.read<Obj>(fade, roofRenderers), roofListCount));
            }
            float hidden = api.read<float>(fade, roofHidden), visible = api.read<float>(fade, roofVisible),
                  current = api.read<float>(fade, roofCurrent), replacement{};
            if (!roofFadeValue(current) || !it->endpoint.apply(hidden, visible, replacement))
                continue;
            if (hidden != replacement)
                memcpy(static_cast<char *>(fade) + 44, &replacement, 4);
            if (hidden != replacement || current != replacement) {
                float duration = 0;
                void *args[] = {&replacement, &duration};
                api.call(roofApplyFade, fade, args);
            }
        }
        if (now - roofLastLog >= 5000) {
            size_t owned{};
            for (const auto &entry : roofEntries)
                owned += entry.endpoint.owned;
            logDebug("Native roof visibility: registered=%zu tracked=%zu preserved=%zu; native fade endpoint only",
                owners.size(), roofEntries.size(), owned);
            roofLastLog = now;
        }
    } catch (const std::exception &e) {
        roofFailed = true;
        logError("Optional native roof visibility disabled: %s; other features retained", e.what());
        resetNativeRoof(false);
    }
}
void bindNativeRoof() {
    try {
        roofOwnerClass = api.find("Assembly-CSharp.dll", "GF.DarKov", "DarKovSceneOcclusionCulling");
        roofFadeClass = api.find("Assembly-CSharp.dll", "DarkSpace", "DarKovOverallDitherFade");
        auto single = api.find("mscorlib.dll", "System", "Single");
        auto field = [single](Class owner, const char *name, Class type, int expected) {
            auto f = healthField(owner, name, type != single);
            int offset{};
            memcpy(&offset, static_cast<char *>(f) + 24, 4);
            if (offset != expected || healthDeclared(f) != type)
                throw std::runtime_error(std::string("Roof field changed: ") + name);
            return f;
        };
        roofOwnerFade = field(roofOwnerClass, "fadeTarget", roofFadeClass, 32);
        roofVisible = field(roofFadeClass, "visibleFade", single, 40);
        roofHidden = field(roofFadeClass, "hiddenFade", single, 44);
        roofCurrent = field(roofFadeClass, "currentFade", single, 52);
        roofRenderers = healthField(roofFadeClass, "renderers");
        roofRendererListClass = healthDeclared(roofRenderers);
        roofListCount = healthField(roofRendererListClass, "_size", false);
        roofListItems = healthField(roofRendererListClass, "_items");
        roofRendererArrayClass = healthDeclared(roofListItems);
        auto renderer = api.find("UnityEngine.CoreModule.dll", "UnityEngine", "Renderer");
        if (api.elementSize(roofRendererArrayClass) != 8 ||
            api.elementClass(roofRendererArrayClass) != renderer)
            throw std::runtime_error("Roof renderer array layout changed");
        roofRegistry = api.field(roofOwnerClass, "PIAPPFFOOKI");
        roofSetClass = healthDeclared(roofRegistry);
        if (!roofRegistry || !(api.fieldFlags(roofRegistry) & 16) || !roofSetClass)
            throw std::runtime_error("Roof static registry changed");
        roofSlots = healthField(roofSetClass, "_slots");
        roofLast = healthField(roofSetClass, "_lastIndex", false);
        roofVersion = healthField(roofSetClass, "_version", false);
        roofSlotArrayClass = healthDeclared(roofSlots);
        auto slot = api.elementClass(roofSlotArrayClass);
        int valueOffset{};
        auto value = healthField(slot, "value");
        auto hash = healthField(slot, "hashCode", false);
        int hashOffset{};
        memcpy(&hashOffset, static_cast<char *>(hash) + 24, 4);
        memcpy(&valueOffset, static_cast<char *>(value) + 24, 4);
        if (api.elementSize(roofSlotArrayClass) != 16 || hashOffset != 16 || valueOffset != 24 ||
            healthDeclared(value) != roofOwnerClass)
            throw std::runtime_error("Roof registry slot layout changed");
        roofApplyFade = api.require(roofFadeClass, "NNHDOOOBLLE", 2);
        roofRecompute = api.require(roofFadeClass, "NKEDJLEDCPF", 2);
        auto typeName = reinterpret_cast<const char *(*)(const void *)>(api.exportAt(0xd7));
        auto paramType = reinterpret_cast<const void *(*)(Method, unsigned)>(api.exportAt(0x85));
        for (auto method : {roofApplyFade, roofRecompute}) {
            const char *kind = method == roofApplyFade ? "System.Single" : "System.Boolean";
            unsigned short flags{};
            memcpy(&flags, static_cast<const char *>(method) + 68, 2);
            if ((flags & 0x10) || strcmp(typeName(paramType(method, 0)), kind) ||
                strcmp(typeName(paramType(method, 1)), kind) ||
                strcmp(typeName(*(reinterpret_cast<const void *const *>(method) + 4)), "System.Void"))
                throw std::runtime_error("Roof native fade signature changed");
        }
        roofBound = true;
    } catch (const std::exception &e) {
        logError("Optional native roof visibility binding unavailable: %s", e.what());
    }
    log("Native roof visibility bound=%d; registered _Roof groups, cached fade methods only", roofBound);
}
