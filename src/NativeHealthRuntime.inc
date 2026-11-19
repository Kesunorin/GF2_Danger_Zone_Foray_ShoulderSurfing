// Existing camera LateUpdate only. Read native bindings; write clone position only.
Class healthEntityClass{}, healthViewComponentClass{}, healthViewClass{}, healthEventClass{},
    healthOwnerClass{}, healthItemClass{};
Class healthSetClass{}, healthDictionaryClass{}, healthDelegateClass{};
Class healthSlotArrayClass{}, healthItemArrayClass{};
Field healthViewComponent{}, healthView{}, healthEvent{}, healthPoint{}, healthPositionEvent{},
    healthEventFirst{}, healthEventSecond{}, healthEventSet{};
Field healthSetSlots{}, healthSetLast{}, healthSetVersion{}, healthDelegateTarget{}, healthItems{},
    healthCount{}, healthVersion{}, healthArray{};
Field healthItemEvent{}, healthItemObject{};
int healthSlotStride{}, healthDictionaryStride{};
Method healthCanvasMode{};
bool healthBound{}, healthFailed{};
unsigned healthOwnerHandle{};
ULONGLONG healthNextSearch{};
int healthLastLog = -600;
struct HealthEntry {
    unsigned item{}, event{}, clone{};
    HudPosition position;
};
std::vector<HealthEntry> healthEntries;
Class healthClass(Obj obj) {
    Class c{};
    if (obj)
        memcpy(&c, obj, 8);
    return c;
}
Class healthDeclared(Field f) {
    const void *type{};
    if (f)
        memcpy(&type, static_cast<char *>(f) + 8, 8);
    return type ? api.classFromType(type) : nullptr;
}
Field healthField(Class c, const char *key, bool reference = true) {
    auto f = api.field(c, key);
    int offset{};
    if (f)
        memcpy(&offset, static_cast<char *>(f) + 24, 4);
    auto declared = healthDeclared(f);
    if (!f || !declared || (api.fieldFlags(f) & 16) || offset < 16 || api.isValueType(declared) == reference)
        throw std::runtime_error(std::string("Health field layout: ") + key);
    return f;
}
Obj healthRef(Obj obj, Field f) {
    return obj ? api.read<Obj>(obj, f) : nullptr;
}
Obj healthClone(Obj item) {
    auto go = healthRef(item, healthItemObject);
    return alive(go) ? api.call(getTransform, go) : nullptr;
}
bool healthBinding(HealthEntry &entry, Obj &clone) {
    auto item = api.handleTarget(entry.item), event = api.handleTarget(entry.event);
    clone = api.handleTarget(entry.clone);
    return healthClass(item) == healthItemClass && healthRef(item, healthItemEvent) == event &&
           alive(clone) && healthClone(item) == clone;
}
void restoreHealthEntry(HealthEntry &entry) {
    Obj clone{};
    Vec3 native{};
    if (entry.position.owned && healthBinding(entry, clone) &&
        entry.position.restore(api.value<Vec3>(getPosition, clone), native))
        set(setPosition, clone, native);
    entry.position = {};
}
void freeHealthEntry(HealthEntry &entry) {
    for (auto h : {entry.item, entry.event, entry.clone})
        if (h)
            api.handleFree(h);
    entry = {};
}
void resetNativeHealth(bool retry) {
    for (auto &entry : healthEntries) {
        try {
            restoreHealthEntry(entry);
        } catch (const std::exception &e) {
            logError("Native health position cleanup skipped: %s", e.what());
        }
        freeHealthEntry(entry);
    }
    healthEntries.clear();
    if (healthOwnerHandle)
        api.handleFree(healthOwnerHandle);
    healthOwnerHandle = 0;
    healthNextSearch = 0;
    if (retry)
        healthFailed = false;
}
Obj healthPlayerEvent(Obj root) {
    auto ik = playerIK(root);
    if (!alive(ik))
        return nullptr;
    auto entity = healthRef(ik, ikEntity);
    if (healthClass(entity) != healthEntityClass)
        return nullptr;
    auto component = healthRef(entity, healthViewComponent);
    if (healthClass(component) != healthViewComponentClass)
        return nullptr;
    auto view = healthRef(component, healthView);
    if (healthClass(view) != healthViewClass)
        return nullptr;
    auto event = healthRef(view, healthEvent);
    return healthClass(event) == healthEventClass ? event : nullptr;
}
Obj discoverHealthOwner(Obj event) {
    auto positionEvent = healthRef(event, healthPositionEvent);
    if (!positionEvent)
        return nullptr;
    Obj found{};
    for (auto branch : {healthEventFirst, healthEventSecond}) {
        auto listeners = healthRef(positionEvent, branch), setObject = healthRef(listeners, healthEventSet);
        if (!setObject)
            continue;
        if (healthClass(setObject) != healthSetClass)
            throw std::runtime_error("Health subscription class changed");
        int version = api.read<int>(setObject, healthSetVersion),
            count = api.read<int>(setObject, healthSetLast);
        auto slots = healthRef(setObject, healthSetSlots);
        if (count < 0 || count > 32)
            throw std::runtime_error("Health subscription bound");
        if (!slots)
            continue;
        size_t capacity{};
        memcpy(&capacity, static_cast<char *>(slots) + 24, 8);
        if (healthClass(slots) != healthSlotArrayClass || capacity > 64 || size_t(count) > capacity ||
            api.elementSize(healthClass(slots)) != healthSlotStride)
            throw std::runtime_error("Health subscription array changed");
        for (int i = 0; i < count; i++) {
            auto p = static_cast<char *>(slots) + 32 + i * healthSlotStride;
            int hash{};
            memcpy(&hash, p, 4);
            if (hash < 0)
                continue;
            Obj delegate{};
            memcpy(&delegate, p + 8, 8);
            if (healthClass(delegate) != healthDelegateClass)
                continue;
            auto target = healthRef(delegate, healthDelegateTarget);
            if (healthClass(target) != healthOwnerClass)
                continue;
            if (found && found != target)
                throw std::runtime_error("Ambiguous native health owner");
            found = target;
        }
        if (version != api.read<int>(setObject, healthSetVersion))
            return nullptr;
    }
    return found;
}
std::vector<Obj> healthItemsSnapshot(Obj owner) {
    auto dictionary = healthRef(owner, healthItems);
    if (!dictionary)
        return {};
    if (healthClass(dictionary) != healthDictionaryClass)
        throw std::runtime_error("Health dictionary class changed");
    int count = api.read<int>(dictionary, healthCount), version = api.read<int>(dictionary, healthVersion);
    auto array = healthRef(dictionary, healthArray);
    if (count < 0 || count > 64)
        throw std::runtime_error("Health item bound");
    if (!array)
        return {};
    size_t capacity{};
    memcpy(&capacity, static_cast<char *>(array) + 24, 8);
    if (healthClass(array) != healthItemArrayClass || capacity > 256 || size_t(count) > capacity ||
        api.elementSize(healthClass(array)) != healthDictionaryStride)
        throw std::runtime_error("Health dictionary array changed");
    std::vector<Obj> result;
    for (int i = 0; i < count; i++) {
        auto p = static_cast<char *>(array) + 32 + i * healthDictionaryStride;
        int hash{};
        memcpy(&hash, p, 4);
        if (hash < 0)
            continue;
        Obj item{};
        memcpy(&item, p + 16, 8);
        if (healthClass(item) == healthItemClass) {
            if (std::find(result.begin(), result.end(), item) != result.end())
                throw std::runtime_error("Duplicate health item");
            result.push_back(item);
        }
    }
    if (version != api.read<int>(dictionary, healthVersion) || dictionary != healthRef(owner, healthItems))
        return {};
    return result;
}
bool healthCanvas(Obj clone, Obj &camera, int &mode) {
    void *args[] = {canvasType};
    auto canvas = api.call(getCanvasParent, clone, args);
    if (!alive(canvas))
        return false;
    mode = api.value<int>(healthCanvasMode, canvas);
    camera = api.call(getCanvasCamera, canvas);
    return mode == 0 || alive(camera);
}
void nativeHealthTick(Obj root, Obj gameCamera) {
    if (!cfg.healthFix || !enabled || !captured || uiBlocked || !foreground() || !saved.cameraHandle) {
        if (healthOwnerHandle || !healthEntries.empty())
            resetNativeHealth(false);
        return;
    }
    if (!healthBound || healthFailed)
        return;
    try {
        auto playerEvent = healthPlayerEvent(root);
        if (!playerEvent) {
            resetNativeHealth(false);
            return;
        }
        if (!healthOwnerHandle) {
            auto now = GetTickCount64();
            if (now < healthNextSearch)
                return;
            healthNextSearch = now + 500;
            auto owner = discoverHealthOwner(playerEvent);
            if (!owner)
                return;
            healthOwnerHandle = api.handleNew(owner, false);
        }
        auto owner = api.handleTarget(healthOwnerHandle);
        if (healthClass(owner) != healthOwnerClass) {
            resetNativeHealth(false);
            return;
        }
        auto items = healthItemsSnapshot(owner);
        std::vector<Obj> active;
        for (auto item : items) {
            auto event = healthRef(item, healthItemEvent);
            if (event == playerEvent || healthClass(event) != healthEventClass)
                continue;
            auto clone = healthClone(item), point = healthRef(event, healthPoint);
            if (!voiceActive(clone) || !alive(point))
                continue;
            auto parent = api.call(getParent, clone);
            if (!alive(parent) || nameOf(parent) != L"HUDRoot" || nameOf(clone) != L"ESHUDItem(Clone)")
                continue;
            active.push_back(item);
        }
        for (size_t i = 0; i < healthEntries.size();) {
            auto &entry = healthEntries[i];
            auto item = api.handleTarget(entry.item);
            Obj clone{};
            if (std::find(active.begin(), active.end(), item) == active.end() ||
                !healthBinding(entry, clone)) {
                restoreHealthEntry(entry);
                freeHealthEntry(entry);
                healthEntries.erase(healthEntries.begin() + i);
            } else
                i++;
        }
        for (auto item : active) {
            bool known = false;
            for (auto &e : healthEntries)
                if (api.handleTarget(e.item) == item) {
                    known = true;
                    break;
                }
            if (!known) {
                auto event = healthRef(item, healthItemEvent), clone = healthClone(item);
                healthEntries.push_back({api.handleNew(item, false),
                                         api.handleNew(event, false),
                                         api.handleNew(clone, false),
                                         {}});
            }
        }
        int width = api.value<int>(getWidth), height = api.value<int>(getHeight), corrected = 0;
        for (auto &entry : healthEntries) {
            Obj clone{};
            if (!healthBinding(entry, clone))
                continue;
            auto event = api.handleTarget(entry.event), point = healthRef(event, healthPoint);
            if (!alive(point))
                continue;
            auto world = api.value<Vec3>(getPosition, point);
            if (!finite(world))
                continue;
            void *args[] = {&world};
            Vec3 screen{};
            if (!headBarScreenPoint(api.value<Vec3>(worldToScreen, gameCamera, args), width, height, screen))
                continue;
            Obj uiCamera{};
            int mode{};
            if (!healthCanvas(clone, uiCamera, mode))
                continue;
            auto observed = api.value<Vec3>(getPosition, clone);
            Vec3 desired = screen;
            if (mode == 0)
                desired.z = observed.z;
            else {
                void *projectArgs[] = {&observed};
                auto oldScreen = api.value<Vec3>(worldToScreen, uiCamera, projectArgs);
                if (!finite(oldScreen) || oldScreen.z <= 0)
                    continue;
                screen.z = oldScreen.z;
                void *unprojectArgs[] = {&screen};
                desired = api.value<Vec3>(screenToWorld, uiCamera, unprojectArgs);
            }
            if (entry.position.update(observed, desired)) {
                if (!sameHudPosition(observed, desired))
                    set(setPosition, clone, desired);
                corrected++;
            }
        }
        int frame = api.value<int>(getFrame);
        if (frame - healthLastLog >= 600) {
            logDebug("Native health position: bound=%d registered=%zu enemyAnchors=%zu corrected=%d; native HPPoint, clone position only",
                healthBound, items.size(), healthEntries.size(), corrected);
            healthLastLog = frame;
        }
    } catch (const std::exception &e) {
        healthFailed = true;
        logError("Optional native health position disabled: %s; camera retained", e.what());
        resetNativeHealth(false);
    }
}
void bindNativeHealth() {
    try {
        healthEntityClass = healthDeclared(ikEntity);
        healthViewComponent = healthField(healthEntityClass, "BEKKLJNMAID");
        healthViewComponentClass = healthDeclared(healthViewComponent);
        healthView = healthField(healthViewComponentClass, "PKKAIFFMMAK");
        healthViewClass = healthDeclared(healthView);
        healthEvent = healthField(healthViewClass, "DMIFOPEKFFI");
        healthEventClass = healthDeclared(healthEvent);
        healthPoint = healthField(healthEventClass, "ELJDBGBNGKI");
        healthPositionEvent = healthField(healthEventClass, "DJIGCBBJPGK");
        auto ev = healthDeclared(healthPositionEvent);
        healthEventFirst = healthField(ev, "NHAMOBBJGFA");
        healthEventSecond = healthField(ev, "EDBCECCNKDH");
        healthEventSet = healthField(healthDeclared(healthEventFirst), "PFCLPNPJDNI");
        healthSetClass = healthDeclared(healthEventSet);
        healthSetSlots = healthField(healthSetClass, "_slots");
        healthSetLast = healthField(healthSetClass, "_lastIndex", false);
        healthSetVersion = healthField(healthSetClass, "_version", false);
        auto slotsClass = healthDeclared(healthSetSlots), slot = api.elementClass(slotsClass);
        healthSlotArrayClass = slotsClass;
        healthSlotStride = api.elementSize(slotsClass);
        auto value = healthField(slot, "value");
        healthDelegateClass = healthDeclared(value);
        int offset{};
        memcpy(&offset, static_cast<char *>(value) + 24, 4);
        if (healthSlotStride != 16 || offset != 24)
            throw std::runtime_error("Health slot layout changed");
        healthDelegateTarget = healthField(healthDelegateClass, "m_target");
        healthOwnerClass = api.find("Assembly-CSharp.dll", "", "OCLKHNIGHFH");
        healthItems = healthField(healthOwnerClass, "DHDCFBIHPLA");
        healthDictionaryClass = healthDeclared(healthItems);
        healthCount = healthField(healthDictionaryClass, "count", false);
        healthVersion = healthField(healthDictionaryClass, "version", false);
        healthArray = healthField(healthDictionaryClass, "entries");
        auto arrayClass = healthDeclared(healthArray), entryClass = api.elementClass(arrayClass);
        healthItemArrayClass = arrayClass;
        healthDictionaryStride = api.elementSize(arrayClass);
        auto itemValue = healthField(entryClass, "value");
        healthItemClass = healthDeclared(itemValue);
        memcpy(&offset, static_cast<char *>(itemValue) + 24, 4);
        if (healthDictionaryStride != 24 || offset != 32)
            throw std::runtime_error("Health dictionary layout changed");
        healthItemEvent = healthField(healthItemClass, "<PDDBEMEJCCD>k__BackingField");
        healthItemObject = healthField(healthItemClass, "<PIFHBCMMPFP>k__BackingField");
        if (healthDeclared(healthItemEvent) != healthEventClass)
            throw std::runtime_error("Health target entity changed");
        auto canvas = api.find("UnityEngine.UIModule.dll", "UnityEngine", "Canvas");
        healthCanvasMode = api.require(canvas, "get_renderMode");
        healthBound = true;
    } catch (const std::exception &e) {
        healthBound = false;
        logError("Native health metadata unavailable: %s", e.what());
    }
    log("Native health position bound=%d enabled=%d; native entity/HPPoint binding; no new hooks",
        healthBound, cfg.healthFix);
}
