// Read the current native laser source in the existing callback. No endpoint
// tracking, model pitch substitution, extra hooks or laser/projectile writes.
Class laserEntityClass{}, laserWeaponClass{}, laserBridgeClass{}, laserViewClass{}, laserComponentClass{},
    laserEffectClass{}, laserLineClass{};
Field laserBridgeField{}, laserViewField{}, laserViewWeapon{}, laserComponentField{}, laserEffectField{},
    laserOwnerField{}, laserLineField{};
Method laserCount{}, laserWorld{}, laserPoint{}, laserTransformPoint{};
bool laserBound{}, laserFailed{}, laserPresent{};
NativeRayCalibration laserCalibration;
Obj laserActor{}, laserWeapon{}, laserView{}, laserEffect{}, laserLine{};
int laserFrame = -1;
ULONGLONG laserLastValid{}, laserLastFire{};
void resetNativeLaser() {
    laserCalibration.reset();
    laserActor = laserWeapon = laserView = laserEffect = laserLine = nullptr;
    laserFrame = -1;
    laserLastValid = laserLastFire = 0;
    laserPresent = false;
    laserFailed = false;
}
void bindNativeLaser(Class entity) {
    try {
        laserEntityClass = entity;
        laserWeaponClass = opticFieldType(entityWeapon);
        auto link = [&](Class owner, const char *fieldName, const char *typeName,
                        const char *assembly = "Assembly-CSharp.dll", const char *ns = "") {
            auto field = checkedOpticField(owner, fieldName, true);
            if (opticFieldType(field) != api.find(assembly, ns, typeName))
                throw std::runtime_error("Native laser field class changed");
            return field;
        };
        laserBridgeField = link(entity, "BEKKLJNMAID", "BINHBMDLOOL");
        laserBridgeClass = opticFieldType(laserBridgeField);
        laserViewField = link(laserBridgeClass, "PKKAIFFMMAK", "FCHLBPMPGCO");
        laserViewClass = opticFieldType(laserViewField);
        laserViewWeapon = link(laserViewClass, "<ILADGKLKELG>k__BackingField", "KHFCPNNADNC");
        laserComponentField = link(laserViewClass, "HAFCFLIAJBF", "DGBLCJDFNLO");
        laserComponentClass = opticFieldType(laserComponentField);
        laserEffectField = link(laserComponentClass, "JNBHIINFIJM", "ELECHKDOALO");
        laserEffectClass = opticFieldType(laserEffectField);
        laserOwnerField = link(laserEffectClass, "EKKBNLLNINF", "FCHLBPMPGCO");
        laserLineField = link(laserEffectClass, "FIPFLFACKHH", "LineRenderer", "UnityEngine.CoreModule.dll",
                              "UnityEngine");
        laserLineClass = opticFieldType(laserLineField);
        laserCount = api.require(laserLineClass, "get_positionCount");
        laserWorld = api.require(laserLineClass, "get_useWorldSpace");
        laserPoint = api.require(laserLineClass, "GetPosition", 1);
        laserTransformPoint = api.require(transformClass, "TransformPoint", 1);
        laserBound = true;
    } catch (const std::exception &e) {
        laserBound = false;
        logError("Optional native laser convergence unavailable: %s; reference aim retained", e.what());
    }
    log("Native laser convergence bound=%d enabled=%d; current owner+weapon source, bounded yaw/pitch correction",
        laserBound, cfg.laserConvergence);
}
// true reserves convergence for the visible native laser. The first valid
// source is used immediately; no pose-settling delay or model correction.
bool convergeNativeLaser(Obj entity, Obj aim, Vec3 reference, Vec3 target, Vec3 &direction, int phase) {
    if (!cfg.laserConvergence || !cfg.gameADS || !adsState.available || !adsState.active || !laserBound ||
        laserFailed) {
        if (laserPresent)
            resetNativeLaser();
        return false;
    }
    try {
        auto typed = [](Obj obj, Class type) { return obj && *static_cast<Class *>(obj) == type; };
        if (!typed(entity, laserEntityClass))
            return false;
        auto weapon = api.read<Obj>(entity, entityWeapon), bridge = api.read<Obj>(entity, laserBridgeField);
        if (!typed(weapon, laserWeaponClass) || !typed(bridge, laserBridgeClass)) {
            resetNativeLaser();
            return false;
        }
        auto view = api.read<Obj>(bridge, laserViewField);
        if (!typed(view, laserViewClass) || api.read<Obj>(view, laserViewWeapon) != weapon) {
            resetNativeLaser();
            return false;
        }
        auto component = api.read<Obj>(view, laserComponentField);
        if (!typed(component, laserComponentClass)) {
            resetNativeLaser();
            return false;
        }
        auto effect = api.read<Obj>(component, laserEffectField);
        if (!typed(effect, laserEffectClass) || api.read<Obj>(effect, laserOwnerField) != view) {
            resetNativeLaser();
            return false;
        }
        auto line = api.read<Obj>(effect, laserLineField);
        if (!typed(line, laserLineClass) || !alive(line) || !api.value<bool>(rendererEnabled, line) ||
            !api.value<bool>(objectActive, api.call(getGameObject, line))) {
            resetNativeLaser();
            return false;
        }
        int count = api.value<int>(laserCount, line);
        if (count < 2 || count > 16) {
            resetNativeLaser();
            return false;
        }
        if (entity != laserActor || weapon != laserWeapon || view != laserView || effect != laserEffect ||
            line != laserLine) {
            resetNativeLaser();
            laserActor = entity;
            laserWeapon = weapon;
            laserView = view;
            laserEffect = effect;
            laserLine = line;
        }
        laserPresent = true;
        auto now = GetTickCount64();
        if ((GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0)
            laserLastFire = now;
        bool frozen = laserLastFire && now - laserLastFire < 250;
        int frame = api.value<int>(getFrame);
        if ((phase == 1 && frame != laserFrame) || !laserCalibration.ready) {
            // Acquire on the first existing aim callback that sees a valid line. The
            // usual LateUpdate read still refreshes it once per frame after native pose.
            if (phase == 1)
                laserFrame = frame;
            int first = 0, last = count - 1;
            void *firstArgs[] = {&first};
            void *lastArgs[] = {&last};
            auto start = api.value<Vec3>(laserPoint, line, firstArgs),
                 end = api.value<Vec3>(laserPoint, line, lastArgs);
            if (!api.value<bool>(laserWorld, line)) {
                auto tr = api.call(componentTransform, line);
                void *startArgs[] = {&start};
                void *endArgs[] = {&end};
                start = api.value<Vec3>(laserTransformPoint, tr, startArgs);
                end = api.value<Vec3>(laserTransformPoint, tr, endArgs);
            }
            // Infer vertex order from the game reference, not the animated model bore.
            if (length(end - reference) < length(start - reference))
                std::swap(start, end);
            RaySourceOffset offset{};
            bool valid = nativeRayOffset(reference, start, end, api.value<Vec3>(aimDirection, aim), offset);
            bool ready = laserCalibration.ready;
            if (valid) {
                laserLastValid = now;
                laserCalibration.sample(true, offset, api.value<float>(getDelta), frozen);
            } else {
                laserLastValid = 0;
                laserCalibration.reset();
            }
            if (ready != laserCalibration.ready)
                logDebug("Native laser source ready=%d right=%.5f up=%.5f weapon=%p; immediate acquisition, established firing geometry frozen",
                    laserCalibration.ready, laserCalibration.offset.right, laserCalibration.offset.up,
                    weapon);
        }
        if (laserCalibration.ready && laserLastValid && now - laserLastValid <= 100)
            direction = nativeRayConvergence(reference, target, direction, laserCalibration.offset);
        return true;
    } catch (const std::exception &e) {
        resetNativeLaser();
        laserFailed = true;
        logError("Optional native laser convergence disabled: %s; reference aim retained", e.what());
        return false;
    }
}
