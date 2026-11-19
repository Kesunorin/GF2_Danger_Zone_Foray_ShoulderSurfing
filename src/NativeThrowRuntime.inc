// Native ground-target selection only. No quickbar key assumptions, custom
// projectile simulation, inventory writes or new gameplay entry points.
Class throwAimClass{};
Field throwDirectField{}, throwGroundField{}, throwSourceField{};
bool throwBound{}, throwFailed{};
void bindNativeThrow(Class aim) {
    try {
        throwAimClass = aim;
        throwDirectField = checkedOpticField(aim, "IIDDJIBHKNM", false);
        throwGroundField = checkedOpticField(aim, "GOKHJNGGJKC", false);
        auto nullable = opticFieldType(throwGroundField);
        if (nullable != opticFieldType(throwDirectField) ||
            api.instanceSize(nullable) - 16 != sizeof(NativeOptionalPoint))
            throw std::runtime_error("Throw optional point size changed");
        auto value = checkedOpticField(nullable, "value", false),
             present = checkedOpticField(nullable, "has_value", false, "Boolean");
        int valueOffset{}, presentOffset{};
        memcpy(&valueOffset, static_cast<char *>(value) + 24, 4);
        memcpy(&presentOffset, static_cast<char *>(present) + 24, 4);
        if (valueOffset != 16 || presentOffset != 28 ||
            opticFieldType(value) != api.find("UnityEngine.CoreModule.dll", "UnityEngine", "Vector3"))
            throw std::runtime_error("Throw optional point layout changed");
        throwSourceField = checkedOpticField(aim, "OPIHOKKAAKM", false, "Int32");
        throwBound = true;
    } catch (const std::exception &e) {
        throwBound = false;
        logError("Optional throw target binding unavailable: %s", e.what());
    }
    log("Native throw target bound=%d; native ground selection, independent of quickbar key/content",
        throwBound);
}
bool nativeThrowTarget(Obj aim, Vec3 camera, Vec3 cameraForward, Vec3 player, Vec3 &target) {
    if (!throwBound || throwFailed || !aim || *static_cast<Class *>(aim) != throwAimClass)
        return false;
    try {
        auto direct = api.read<NativeOptionalPoint>(aim, throwDirectField),
             ground = api.read<NativeOptionalPoint>(aim, throwGroundField);
        if (!groundTargetSelection(direct, ground, api.read<int>(aim, throwSourceField), player))
            return false;
        return cameraGroundTarget(camera, cameraForward, player, ground.value.y, target);
    } catch (const std::exception &e) {
        throwFailed = true;
        logError("Optional throw target disabled: %s; ordinary aiming retained", e.what());
        return false;
    }
}
