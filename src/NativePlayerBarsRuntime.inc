// Existing camera callback only. Native player widgets retain their pixel layout.
// Endurance has an independent owner; never activate or edit its image/fill/tween.
Class playerBarsOwnerClass{}, playerBarsEventClass{}, playerBarsListenersClass{}, playerBarsSetClass{},
    playerBarsSlotsClass{}, playerBarsDelegateClass{};
Field playerBarsEvent{}, playerBarsFirst{}, playerBarsSecond{}, playerBarsSet{}, playerBarsSlots{},
    playerBarsLast{}, playerBarsVersion{}, playerBarsTarget{}, playerBarsOwnerEvent{}, playerBarsClone{};
bool playerBarsBound{}, playerBarsFailed{};
int playerBarsLastLog = -600;
struct PlayerBarsBinding {
    HealthEntry hp;
    unsigned healthOwner{}, enduranceOwner{}, enduranceClone{};
    HudPosition endurance;
};
PlayerBarsBinding playerBars;
Obj discoverPlayerEndurance(Obj event) {
    auto ev = healthRef(event, playerBarsEvent);
    if (!ev)
        return nullptr;
    if (healthClass(ev) != playerBarsEventClass)
        throw std::runtime_error("Player endurance event changed");
    Obj found{};
    for (auto branch : {playerBarsFirst, playerBarsSecond}) {
        auto listeners = healthRef(ev, branch);
        if (!listeners)
            continue;
        if (healthClass(listeners) != playerBarsListenersClass)
            throw std::runtime_error("Player endurance listeners changed");
        auto setObject = healthRef(listeners, playerBarsSet);
        if (!setObject)
            continue;
        if (healthClass(setObject) != playerBarsSetClass)
            throw std::runtime_error("Player endurance set changed");
        int version = api.read<int>(setObject, playerBarsVersion),
            count = api.read<int>(setObject, playerBarsLast);
        auto slots = healthRef(setObject, playerBarsSlots);
        if (count < 0 || count > 32)
            throw std::runtime_error("Player endurance subscription bound");
        if (!slots)
            continue;
        size_t capacity{};
        memcpy(&capacity, static_cast<char *>(slots) + 24, 8);
        if (healthClass(slots) != playerBarsSlotsClass || capacity > 64 || size_t(count) > capacity ||
            api.elementSize(playerBarsSlotsClass) != 16)
            throw std::runtime_error("Player endurance slot layout changed");
        for (int i = 0; i < count; i++) {
            auto p = static_cast<char *>(slots) + 32 + i * 16;
            int hash{};
            memcpy(&hash, p, 4);
            if (hash < 0)
                continue;
            Obj delegate{};
            memcpy(&delegate, p + 8, 8);
            if (healthClass(delegate) != playerBarsDelegateClass)
                continue;
            auto owner = healthRef(delegate, playerBarsTarget);
            if (healthClass(owner) != playerBarsOwnerClass || healthRef(owner, playerBarsOwnerEvent) != event)
                continue;
            if (found && found != owner)
                throw std::runtime_error("Ambiguous player endurance owner");
            found = owner;
        }
        if (version != api.read<int>(setObject, playerBarsVersion) ||
            slots != healthRef(setObject, playerBarsSlots))
            return nullptr;
    }
    return found;
}
bool playerHealthRegistered(Obj event) {
    auto owner = api.handleTarget(playerBars.healthOwner), item = api.handleTarget(playerBars.hp.item);
    Obj clone{};
    if (healthClass(owner) != healthOwnerClass || discoverHealthOwner(event) != owner ||
        api.handleTarget(playerBars.hp.event) != event || !healthBinding(playerBars.hp, clone))
        return false;
    auto items = healthItemsSnapshot(owner);
    return std::find(items.begin(), items.end(), item) != items.end();
}
bool playerEnduranceBinding(Obj event, Obj &clone) {
    auto owner = api.handleTarget(playerBars.enduranceOwner);
    clone = api.handleTarget(playerBars.enduranceClone);
    if (!event || healthClass(owner) != playerBarsOwnerClass ||
        healthRef(owner, playerBarsOwnerEvent) != event || discoverPlayerEndurance(event) != owner ||
        !alive(clone) || healthRef(owner, playerBarsClone) != clone)
        return false;
    auto parent = api.call(getParent, clone);
    if (!alive(parent) || nameOf(parent) != L"Trans_EnduranceBar" ||
        nameOf(clone) != L"ESMainEnduranceBarItem(Clone)")
        return false;
    auto all = api.call(getParent, parent);
    return alive(all) && nameOf(all) == L"AllRoot";
}
void resetNativePlayerBars(bool retry) {
    // loadConfig also calls this before Api::initialize at DLL startup.
    auto event = playerBars.hp.event ? api.handleTarget(playerBars.hp.event) : nullptr;
    try {
        Obj clone{};
        Vec3 native{};
        if (playerBars.endurance.owned && playerEnduranceBinding(event, clone) &&
            playerBars.endurance.restore(api.value<Vec3>(getPosition, clone), native))
            set(setPosition, clone, native);
    } catch (const std::exception &e) {
        logError("Player endurance position cleanup skipped: %s", e.what());
    }
    try {
        if (playerBars.hp.position.owned && playerHealthRegistered(event))
            restoreHealthEntry(playerBars.hp);
    } catch (const std::exception &e) {
        logError("Player health position cleanup skipped: %s", e.what());
    }
    freeHealthEntry(playerBars.hp);
    for (auto h : {playerBars.healthOwner, playerBars.enduranceOwner, playerBars.enduranceClone})
        if (h)
            api.handleFree(h);
    playerBars = {};
    if (retry)
        playerBarsFailed = false;
}
Vec3 playerNativePosition(const HudPosition &position, Vec3 observed) {
    return position.owned && sameHudPosition(observed, position.written) ? position.native : observed;
}
bool playerUiScreen(Obj clone, Vec3 position, Vec3 &screen) {
    Obj camera{};
    int mode{};
    if (!healthCanvas(clone, camera, mode) || !finite(position))
        return false;
    if (mode == 0)
        screen = position;
    else {
        void *args[] = {&position};
        screen = api.value<Vec3>(worldToScreen, camera, args);
    }
    return finite(screen) && (mode == 0 || screen.z > 0);
}
bool playerUiDesired(Obj clone, Vec3 observed, Vec3 screen, Vec3 &desired) {
    Obj camera{};
    int mode{};
    if (!healthCanvas(clone, camera, mode) || !finite(observed) || !finite(screen))
        return false;
    if (mode == 0) {
        desired = screen;
        desired.z = observed.z;
    } else {
        Vec3 depth{};
        if (!playerUiScreen(clone, observed, depth))
            return false;
        screen.z = depth.z;
        void *args[] = {&screen};
        desired = api.value<Vec3>(screenToWorld, camera, args);
    }
    return finite(desired);
}
void nativePlayerBarsTick(Obj root, Obj gameCamera) {
    if (!cfg.playerBarsFix || !enabled || !captured || uiBlocked || !foreground() || !saved.cameraHandle) {
        if (playerBars.hp.item || playerBars.enduranceOwner)
            resetNativePlayerBars(false);
        return;
    }
    if (!playerBarsBound || playerBarsFailed || !healthBound)
        return;
    try {
        auto event = healthPlayerEvent(root);
        if (!event) {
            resetNativePlayerBars(false);
            return;
        }
        if (playerBars.hp.item && !playerHealthRegistered(event))
            resetNativePlayerBars(false);
        if (!playerBars.hp.item) {
            auto owner = discoverHealthOwner(event);
            if (!owner)
                return;
            auto items = healthItemsSnapshot(owner);
            Obj selected{}, clone{};
            for (auto item : items)
                if (healthRef(item, healthItemEvent) == event) {
                    if (selected)
                        throw std::runtime_error("Duplicate player health binding");
                    selected = item;
                }
            if (!selected)
                return;
            clone = healthClone(selected);
            auto point = healthRef(event, healthPoint);
            if (!alive(clone) || !alive(point))
                return;
            auto parent = api.call(getParent, clone);
            if (!alive(parent) || nameOf(parent) != L"HUDRoot" || nameOf(clone) != L"ESHUDItem(Clone)")
                return;
            playerBars.hp = {
                api.handleNew(selected, false), api.handleNew(event, false), api.handleNew(clone, false), {}};
            playerBars.healthOwner = api.handleNew(owner, false);
        }
        Obj enduranceClone{};
        if (playerBars.enduranceOwner && !playerEnduranceBinding(event, enduranceClone)) {
            // Owner has rebound or unsubscribed: never write the old widget.
            for (auto h : {playerBars.enduranceOwner, playerBars.enduranceClone})
                if (h)
                    api.handleFree(h);
            playerBars.enduranceOwner = playerBars.enduranceClone = 0;
            playerBars.endurance = {};
        }
        if (!playerBars.enduranceOwner) {
            auto owner = discoverPlayerEndurance(event);
            if (owner) {
                auto clone = healthRef(owner, playerBarsClone);
                if (alive(clone)) {
                    playerBars.enduranceOwner = api.handleNew(owner, false);
                    playerBars.enduranceClone = api.handleNew(clone, false);
                    if (!playerEnduranceBinding(event, enduranceClone)) {
                        for (auto h : {playerBars.enduranceOwner, playerBars.enduranceClone})
                            if (h)
                                api.handleFree(h);
                        playerBars.enduranceOwner = playerBars.enduranceClone = 0;
                    }
                }
            }
        }
        auto hpClone = api.handleTarget(playerBars.hp.clone), point = healthRef(event, healthPoint);
        if (!voiceActive(hpClone) || !alive(point))
            return;
        auto world = api.value<Vec3>(getPosition, point);
        if (!finite(world))
            return;
        void *args[] = {&world};
        Vec3 screen{};
        if (!headBarScreenPoint(api.value<Vec3>(worldToScreen, gameCamera, args), api.value<int>(getWidth),
                                api.value<int>(getHeight), screen))
            return;
        auto hpObserved = api.value<Vec3>(getPosition, hpClone);
        Vec3 nativeHpScreen{}, hpDesired{};
        if (!playerUiScreen(hpClone, playerNativePosition(playerBars.hp.position, hpObserved),
                            nativeHpScreen) ||
            !playerUiDesired(hpClone, hpObserved, screen, hpDesired))
            return;
        int enduranceCorrected = 0;
        if (enduranceClone && !voiceActive(enduranceClone)) {
            auto observed = api.value<Vec3>(getPosition, enduranceClone);
            Vec3 native{};
            if (playerBars.endurance.restore(observed, native))
                set(setPosition, enduranceClone, native);
            playerBars.endurance = {};
        }
        if (voiceActive(enduranceClone)) {
            auto observed = api.value<Vec3>(getPosition, enduranceClone);
            Vec3 nativeScreen{}, desired{};
            if (playerUiScreen(enduranceClone, playerNativePosition(playerBars.endurance, observed),
                               nativeScreen)) {
                // Native screen delta keeps the white arc left of the blue bar at all yaw,
                // pitch/FOV/shoulder angles. No interpretation of BBFMHMMJACA is required.
                Vec3 target = screen + (nativeScreen - nativeHpScreen);
                target.z = screen.z;
                if (playerUiDesired(enduranceClone, observed, target, desired) &&
                    playerBars.endurance.update(observed, desired)) {
                    if (!sameHudPosition(observed, desired))
                        set(setPosition, enduranceClone, desired);
                    enduranceCorrected = 1;
                }
            }
        }
        if (playerBars.hp.position.update(hpObserved, hpDesired) && !sameHudPosition(hpObserved, hpDesired))
            set(setPosition, hpClone, hpDesired);
        int frame = api.value<int>(getFrame);
        if (frame - playerBarsLastLog >= 600) {
            logDebug("Native player bars: health=1 endurance=%d; HPPoint and native screen spacing; clone position only",
                enduranceCorrected);
            playerBarsLastLog = frame;
        }
    } catch (const std::exception &e) {
        playerBarsFailed = true;
        logError("Optional native player bars disabled: %s; camera retained", e.what());
        resetNativePlayerBars(false);
    }
}
void bindNativePlayerBars() {
    try {
        playerBarsOwnerClass = api.find("Assembly-CSharp.dll", "", "FOBOFONBEHH");
        playerBarsOwnerEvent = healthField(playerBarsOwnerClass, "OOMBBCNJKDB");
        playerBarsClone = healthField(playerBarsOwnerClass, "HDFCFFBCCLD");
        if (healthDeclared(playerBarsOwnerEvent) != healthEventClass ||
            healthDeclared(playerBarsClone) != transformClass)
            throw std::runtime_error("Player endurance owner layout changed");
        playerBarsEvent = healthField(healthEventClass, "<LANACFHLFGH>k__BackingField");
        playerBarsEventClass = healthDeclared(playerBarsEvent);
        playerBarsFirst = healthField(playerBarsEventClass, "NHAMOBBJGFA");
        playerBarsSecond = healthField(playerBarsEventClass, "EDBCECCNKDH");
        playerBarsListenersClass = healthDeclared(playerBarsFirst);
        if (healthDeclared(playerBarsSecond) != playerBarsListenersClass)
            throw std::runtime_error("Player endurance branch mismatch");
        playerBarsSet = healthField(playerBarsListenersClass, "PFCLPNPJDNI");
        playerBarsSetClass = healthDeclared(playerBarsSet);
        playerBarsSlots = healthField(playerBarsSetClass, "_slots");
        playerBarsLast = healthField(playerBarsSetClass, "_lastIndex", false);
        playerBarsVersion = healthField(playerBarsSetClass, "_version", false);
        playerBarsSlotsClass = healthDeclared(playerBarsSlots);
        auto slot = api.elementClass(playerBarsSlotsClass), value = healthField(slot, "value"),
             hash = healthField(slot, "hashCode", false);
        int offset{}, hashOffset{};
        memcpy(&offset, static_cast<char *>(value) + 24, 4);
        memcpy(&hashOffset, static_cast<char *>(hash) + 24, 4);
        if (api.elementSize(playerBarsSlotsClass) != 16 || offset != 24 || hashOffset != 16)
            throw std::runtime_error("Player endurance delegate slot mismatch");
        playerBarsDelegateClass = healthDeclared(value);
        playerBarsTarget = healthField(playerBarsDelegateClass, "m_target");
        playerBarsBound = true;
    } catch (const std::exception &e) {
        playerBarsBound = false;
        logError("Native player bars metadata unavailable: %s", e.what());
    }
    log("Native player bars bound=%d enabled=%d; no new hooks", playerBarsBound, cfg.playerBarsFix);
}
