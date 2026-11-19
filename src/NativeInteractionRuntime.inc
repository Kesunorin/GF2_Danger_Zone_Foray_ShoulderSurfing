// Existing camera LateUpdate only. Reproject registered interact dots from their
// bound entity's displayed model; never alter interaction, visibility or icons.
Class interactOwnerClass{}, interactItemClass{}, interactDictionaryClass{}, interactArrayClass{};
Field interactInstance{}, interactItems{}, interactCount{}, interactVersion{}, interactArray{};
Field interactEntity{}, interactClone{}, interactModel{}, interactEntityId{};
bool interactBound{}, interactFailed{};
unsigned interactOwnerHandle{};
int interactLastLog = -600;
struct InteractBinding {
    unsigned key{};
    Obj item{}, entity{}, clone{}, model{};
};
struct InteractEntry {
    unsigned key{}, item{}, entity{}, clone{}, model{};
    HudPosition position;
};
std::vector<InteractEntry> interactEntries;
Obj interactModelObject(Obj entity) {
    if (healthClass(entity) != healthEntityClass)
        return nullptr;
    auto component = healthRef(entity, healthViewComponent);
    if (healthClass(component) != healthViewComponentClass)
        return nullptr;
    auto view = healthRef(component, healthView);
    if (healthClass(view) != healthViewClass)
        return nullptr;
    auto model = healthRef(view, interactModel);
    return alive(model) ? model : nullptr;
}
bool interactIdentity(const InteractBinding &b) {
    return healthClass(b.item) == interactItemClass && healthRef(b.item, interactEntity) == b.entity &&
           healthClass(b.entity) == healthEntityClass &&
           api.read<unsigned>(b.entity, interactEntityId) == b.key && alive(b.clone) &&
           healthRef(b.item, interactClone) == b.clone && alive(b.model) &&
           interactModelObject(b.entity) == b.model;
}
std::vector<InteractBinding> interactSnapshot(Obj owner) {
    auto dictionary = healthRef(owner, interactItems);
    if (!dictionary)
        return {};
    if (healthClass(dictionary) != interactDictionaryClass)
        throw std::runtime_error("Interact dictionary class changed");
    int count = api.read<int>(dictionary, interactCount),
        version = api.read<int>(dictionary, interactVersion);
    auto array = healthRef(dictionary, interactArray);
    if (count < 0 || count > 64)
        throw std::runtime_error("Interact item bound");
    if (!array)
        return {};
    size_t capacity{};
    memcpy(&capacity, static_cast<char *>(array) + 24, 8);
    if (healthClass(array) != interactArrayClass || capacity > 256 || size_t(count) > capacity ||
        api.elementSize(healthClass(array)) != 24)
        throw std::runtime_error("Interact array layout changed");
    std::vector<InteractBinding> rows;
    for (int i = 0; i < count; i++) {
        auto p = static_cast<char *>(array) + 32 + i * 24;
        int hash{};
        memcpy(&hash, p, 4);
        if (hash < 0)
            continue;
        unsigned key{};
        Obj item{};
        memcpy(&key, p + 8, 4);
        memcpy(&item, p + 16, 8);
        if (healthClass(item) != interactItemClass)
            continue;
        auto entity = healthRef(item, interactEntity);
        InteractBinding b{key, item, entity, healthRef(item, interactClone), interactModelObject(entity)};
        if (!interactIdentity(b))
            continue;
        if (std::any_of(rows.begin(), rows.end(), [&](const auto &row) {
                return row.key == key || row.item == item || row.clone == b.clone;
            }))
            throw std::runtime_error("Duplicate interact binding");
        rows.push_back(b);
    }
    if (version != api.read<int>(dictionary, interactVersion) ||
        dictionary != healthRef(owner, interactItems))
        return {};
    return rows;
}
InteractBinding interactEntryBinding(const InteractEntry &e) {
    return {e.key, api.handleTarget(e.item), api.handleTarget(e.entity), api.handleTarget(e.clone),
            api.handleTarget(e.model)};
}
bool sameInteractBinding(const InteractBinding &a, const InteractBinding &b) {
    return a.key == b.key && a.item == b.item && a.entity == b.entity && a.clone == b.clone &&
           a.model == b.model;
}
bool interactRegistered(const InteractBinding &b, const std::vector<InteractBinding> &registered) {
    return interactIdentity(b) && std::any_of(registered.begin(), registered.end(),
                                              [&](const auto &row) { return sameInteractBinding(row, b); });
}
void restoreInteractEntry(InteractEntry &e, const std::vector<InteractBinding> &registered) {
    auto b = interactEntryBinding(e);
    Vec3 native{};
    if (e.position.owned && interactRegistered(b, registered) &&
        e.position.restore(api.value<Vec3>(getPosition, b.clone), native))
        set(setPosition, b.clone, native);
    e.position = {};
}
void freeInteractEntry(InteractEntry &e) {
    for (auto h : {e.item, e.entity, e.clone, e.model})
        if (h)
            api.handleFree(h);
    e = {};
}
void resetNativeInteraction(bool retry) {
    std::vector<InteractBinding> registered;
    try {
        auto owner = interactOwnerHandle ? api.handleTarget(interactOwnerHandle) : nullptr;
        if (interactBound && healthClass(owner) == interactOwnerClass &&
            api.global<Obj>(interactInstance) == owner)
            registered = interactSnapshot(owner);
    } catch (const std::exception &e) {
        logError("Interact position cleanup skipped: %s", e.what());
    }
    for (auto &e : interactEntries) {
        try {
            restoreInteractEntry(e, registered);
        } catch (const std::exception &ex) {
            logError("Interact position restore skipped: %s", ex.what());
        }
        freeInteractEntry(e);
    }
    interactEntries.clear();
    if (interactOwnerHandle)
        api.handleFree(interactOwnerHandle);
    interactOwnerHandle = 0;
    if (retry)
        interactFailed = false;
}
void nativeInteractionTick(Obj gameCamera) {
    if (!cfg.interactFix || !enabled || !captured || uiBlocked || !foreground() || !saved.cameraHandle) {
        if (interactOwnerHandle || !interactEntries.empty())
            resetNativeInteraction(false);
        return;
    }
    if (!interactBound || interactFailed)
        return;
    try {
        auto owner = api.global<Obj>(interactInstance);
        if (healthClass(owner) != interactOwnerClass) {
            resetNativeInteraction(false);
            return;
        }
        if (!interactOwnerHandle || api.handleTarget(interactOwnerHandle) != owner) {
            resetNativeInteraction(false);
            interactOwnerHandle = api.handleNew(owner, false);
        }
        auto registered = interactSnapshot(owner);
        std::vector<InteractBinding> active;
        for (const auto &b : registered) {
            if (!voiceActive(b.clone) || nameOf(b.clone) != L"ESMainInteractDotItem(Clone)")
                continue;
            auto parent = api.call(getParent, b.clone);
            if (alive(parent) && nameOf(parent) == L"HUDRoot")
                active.push_back(b);
        }
        for (size_t i = 0; i < interactEntries.size();) {
            auto &e = interactEntries[i];
            auto b = interactEntryBinding(e);
            if (!interactRegistered(b, active)) {
                restoreInteractEntry(e, registered);
                freeInteractEntry(e);
                interactEntries.erase(interactEntries.begin() + i);
            } else
                i++;
        }
        for (const auto &b : active) {
            bool known = false;
            for (const auto &e : interactEntries)
                if (sameInteractBinding(interactEntryBinding(e), b)) {
                    known = true;
                    break;
                }
            if (!known)
                interactEntries.push_back({b.key,
                                           api.handleNew(b.item, false),
                                           api.handleNew(b.entity, false),
                                           api.handleNew(b.clone, false),
                                           api.handleNew(b.model, false),
                                           {}});
        }
        int width = api.value<int>(getWidth), height = api.value<int>(getHeight), corrected = 0;
        for (auto &e : interactEntries) {
            auto b = interactEntryBinding(e);
            if (!interactRegistered(b, registered))
                continue;
            auto tr = api.call(getTransform, b.model);
            if (!alive(tr))
                continue;
            auto world = api.value<Vec3>(getPosition, tr);
            void *args[] = {&world};
            Vec3 screen{};
            if (!headBarScreenPoint(api.value<Vec3>(worldToScreen, gameCamera, args), width, height, screen))
                continue;
            Obj uiCamera{};
            int mode{};
            if (!healthCanvas(b.clone, uiCamera, mode))
                continue;
            auto observed = api.value<Vec3>(getPosition, b.clone);
            Vec3 desired = screen;
            if (mode == 0)
                desired.z = observed.z;
            else {
                void *a[] = {&observed};
                auto oldScreen = api.value<Vec3>(worldToScreen, uiCamera, a);
                if (!finite(oldScreen) || oldScreen.z <= 0)
                    continue;
                screen.z = oldScreen.z;
                void *u[] = {&screen};
                desired = api.value<Vec3>(screenToWorld, uiCamera, u);
            }
            if (e.position.update(observed, desired)) {
                if (!sameHudPosition(observed, desired))
                    set(setPosition, b.clone, desired);
                corrected++;
            }
        }
        int frame = api.value<int>(getFrame);
        if (frame - interactLastLog >= 600) {
            logDebug("Native interact position: registered=%zu active=%zu corrected=%d; bound model anchor, clone position only",
                registered.size(), interactEntries.size(), corrected);
            interactLastLog = frame;
        }
    } catch (const std::exception &e) {
        interactFailed = true;
        logError("Optional native interact position disabled: %s; camera retained", e.what());
        resetNativeInteraction(false);
    }
}
void bindNativeInteraction() {
    try {
        if (!healthBound)
            throw std::runtime_error("Shared entity/canvas metadata unavailable");
        interactOwnerClass = api.find("Assembly-CSharp.dll", "", "IGPIGHGKGII");
        interactInstance = api.field(interactOwnerClass, "LOGIELGNLNF");
        if (!interactInstance || !(api.fieldFlags(interactInstance) & 16) ||
            healthDeclared(interactInstance) != interactOwnerClass)
            throw std::runtime_error("Interact singleton changed");
        interactItems = healthField(interactOwnerClass, "NJFLJKOAFAA");
        interactDictionaryClass = healthDeclared(interactItems);
        interactCount = healthField(interactDictionaryClass, "count", false);
        interactVersion = healthField(interactDictionaryClass, "version", false);
        interactArray = healthField(interactDictionaryClass, "entries");
        interactArrayClass = healthDeclared(interactArray);
        auto ec = api.elementClass(interactArrayClass);
        int offset{};
        for (auto spec : {std::pair<const char *, int>{"hashCode", 16}, {"key", 24}, {"value", 32}}) {
            auto f = healthField(ec, spec.first, spec.second == 32);
            memcpy(&offset, static_cast<char *>(f) + 24, 4);
            if (offset != spec.second)
                throw std::runtime_error("Interact dictionary offsets changed");
            if (spec.second == 32)
                interactItemClass = healthDeclared(f);
            else if (strcmp(api.className(healthDeclared(f)), spec.second == 16 ? "Int32" : "UInt32"))
                throw std::runtime_error("Interact dictionary primitive changed");
        }
        if (api.elementSize(interactArrayClass) != 24 ||
            interactItemClass != api.find("Assembly-CSharp.dll", "", "OCODOAMCENE"))
            throw std::runtime_error("Interact dictionary item changed");
        interactEntity = healthField(interactItemClass, "<JACGEKHOMFD>k__BackingField");
        interactClone = healthField(interactItemClass, "HDFCFFBCCLD");
        interactModel = healthField(healthViewClass, "<NFKAHIGFKBM>k__BackingField");
        interactEntityId = healthField(healthEntityClass, "<JJCNCEOGHAK>k__BackingField", false);
        if (healthDeclared(interactEntity) != healthEntityClass ||
            strcmp(api.className(healthDeclared(interactClone)), "Transform") ||
            strcmp(api.className(healthDeclared(interactModel)), "GameObject") ||
            strcmp(api.className(healthDeclared(interactEntityId)), "UInt32"))
            throw std::runtime_error("Interact anchor binding changed");
        interactBound = true;
    } catch (const std::exception &e) {
        interactBound = false;
        logError("Native interact metadata unavailable: %s", e.what());
    }
    log("Native interact position bound=%d enabled=%d; registered entity/model binding; no new hooks",
        interactBound, cfg.interactFix);
}
