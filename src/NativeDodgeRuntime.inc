// Field-only native RollSpeed observation, existing aim/turn setters for intent.
// No action calls, new hooks, movement/position/stamina/animation writes.
Class dodgeEntityClass{}, dodgeViewClass{}, dodgeSelectorClass{};
Field dodgeView{}, dodgeSelector{}, dodgeSpeed{};
bool dodgeBound{}, dodgeFailed{};
Obj dodgeActor{};
DodgeDirection dodgeDirection;
DodgeAimMode dodgeLastMode = DodgeAimMode::Normal;
void resetDodgeDirection() {
    dodgeDirection.reset((GetAsyncKeyState(VK_SPACE) & 0x8000) != 0);
    dodgeActor = nullptr;
    dodgeLastMode = DodgeAimMode::Normal;
    dodgeFailed = false;
}
void bindNativeDodge(Class entity) {
    try {
        dodgeEntityClass = entity;
        dodgeView = checkedOpticField(entity, "<JCGNKAGPKOE>k__BackingField", true);
        dodgeViewClass = opticFieldType(dodgeView);
        dodgeSelector = checkedOpticField(dodgeViewClass, "<GMPDBLAKGHJ>k__BackingField", true);
        dodgeSelectorClass = opticFieldType(dodgeSelector);
        if (dodgeViewClass != api.find("Assembly-CSharp.dll", "", "GJLFHDJEMEK") ||
            dodgeSelectorClass != api.find("Assembly-CSharp.dll", "", "MBJFJMLJJKE"))
            throw std::runtime_error("Dodge selector type changed");
        dodgeSpeed = checkedOpticField(dodgeSelectorClass, "KGOBFDGOOGP", false, "FGPKFFEOKNO");
        auto attributes = api.find("StaticTable.dll", "", "FGPKFFEOKNO");
        if (opticFieldType(dodgeSpeed) != attributes)
            throw std::runtime_error("Dodge speed type changed");
        for (auto expected : {std::pair<const char *, int>{"AimWalkSpeed", 5005},
                              {"HipFireMoveSpeed", 5006},
                              {"WalkSpeed", 5007},
                              {"RunSpeed", 5008},
                              {"RollSpeed", 5009}}) {
            auto f = api.field(attributes, expected.first);
            if (!f || (api.fieldFlags(f) & 0x50) != 0x50 || opticFieldType(f) != attributes ||
                api.global<int>(f) != expected.second)
                throw std::runtime_error("Dodge speed enum changed");
        }
        dodgeBound = true;
    } catch (const std::exception &e) {
        dodgeBound = false;
        logError("Optional directional dodge metadata unavailable: %s", e.what());
    }
    log("Directional dodge bound=%d enabled=%d; camera-relative input, native RollSpeed; no new hooks",
        dodgeBound, cfg.dodgeFix);
}
DodgeAimMode nativeDodgeMode(Obj entity, int phase) {
    if (!cfg.dodgeFix || !dodgeBound || dodgeFailed || uiBlocked || !enabled || !captured || !foreground()) {
        dodgeDirection.reset((GetAsyncKeyState(VK_SPACE) & 0x8000) != 0);
        dodgeActor = nullptr;
        return DodgeAimMode::Normal;
    }
    try {
        if (!entity || *static_cast<Class *>(entity) != dodgeEntityClass)
            return DodgeAimMode::Normal;
        if (entity != dodgeActor) {
            dodgeDirection.reset((GetAsyncKeyState(VK_SPACE) & 0x8000) != 0);
            dodgeActor = entity;
        }
        auto view = api.read<Obj>(entity, dodgeView);
        if (!view || *static_cast<Class *>(view) != dodgeViewClass) {
            dodgeDirection.reset();
            return DodgeAimMode::Normal;
        }
        auto selector = api.read<Obj>(view, dodgeSelector);
        if (!selector || *static_cast<Class *>(selector) != dodgeSelectorClass) {
            dodgeDirection.reset();
            return DodgeAimMode::Normal;
        }
        int speed = api.read<int>(selector, dodgeSpeed);
        bool valid = speed >= 5005 && speed <= 5009;
        bool space = (GetAsyncKeyState(VK_SPACE) & 0x8000) != 0;
        auto intent = cameraRelativeDodge(
            yaw, (GetAsyncKeyState('W') & 0x8000) != 0, (GetAsyncKeyState('A') & 0x8000) != 0,
            (GetAsyncKeyState('S') & 0x8000) != 0, (GetAsyncKeyState('D') & 0x8000) != 0);
        auto mode = dodgeDirection.sample(valid, speed, phase == 0, space, intent, GetTickCount64() * .001);
        if (mode != dodgeLastMode) {
            logDebug("Directional dodge mode=%d speed=%d intent=(%.3f %.3f %.3f); native duration/cost retained",
                int(mode), speed, dodgeDirection.requested.x, dodgeDirection.requested.y,
                dodgeDirection.requested.z);
            dodgeLastMode = mode;
        }
        return mode;
    } catch (const std::exception &e) {
        dodgeFailed = true;
        dodgeDirection.reset();
        logError("Optional directional dodge disabled: %s; normal aim retained", e.what());
        return DodgeAimMode::Normal;
    }
}
