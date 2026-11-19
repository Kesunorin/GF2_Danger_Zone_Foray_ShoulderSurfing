// Existing camera callback only. Read current native shoot timer; no game
// timing/scatter/action writes, new hooks, duration multiplication or clock.
Class zoomEntityClass{}, zoomShootClass{}, zoomWeaponClass{};
Field zoomShoot{}, zoomOwner{}, zoomActive{}, zoomRemaining{}, zoomSeconds{};
bool zoomBound{}, zoomFailed{}, zoomWasActive{};
Obj zoomActor{}, zoomWeapon{};
float zoomLastDuration{};
void resetNativeAdsZoom() {
    zoomActor = zoomWeapon = nullptr;
    zoomWasActive = false;
    zoomLastDuration = 0;
    zoomFailed = false;
}
void bindNativeAdsZoom(Class entity) {
    try {
        zoomEntityClass = entity;
        zoomShoot = checkedOpticField(entity, "MKCGBMLGPKA", true);
        zoomShootClass = opticFieldType(zoomShoot);
        if (zoomShootClass != api.find("Assembly-CSharp.dll", "", "PCALAAPACOK"))
            throw std::runtime_error("Native ADS timer class changed");
        zoomOwner = checkedOpticField(zoomShootClass, "LMGIFJEKBHJ", true);
        if (opticFieldType(zoomOwner) != entity)
            throw std::runtime_error("Native ADS owner class changed");
        zoomActive = checkedOpticField(zoomShootClass, "KMFIKHKKIJE", false, "Boolean");
        zoomRemaining = checkedOpticField(zoomShootClass, "DPDANBOKLLC", false, "Single");
        zoomSeconds = checkedOpticField(zoomShootClass, "KPJKIFLBPBF", false, "Single");
        for (auto expected : {std::pair<Field, int>{zoomOwner, 8},
                              {zoomActive, 1},
                              {zoomRemaining, 4},
                              {zoomSeconds, 4}}) {
            int offset{};
            memcpy(&offset, static_cast<char *>(expected.first) + 24, 4);
            if (offset < 16 || offset > api.instanceSize(zoomShootClass) - expected.second)
                throw std::runtime_error("Native ADS timer field outside owning object");
        }
        zoomWeaponClass = opticFieldType(checkedOpticField(entity, "KJMEEDLGOKA", true));
        if (zoomWeaponClass != api.find("Assembly-CSharp.dll", "", "KHFCPNNADNC"))
            throw std::runtime_error("Native ADS weapon class changed");
        zoomBound = true;
    } catch (const std::exception &e) {
        zoomBound = false;
        logError("Optional native ADS zoom unavailable: %s; previous ADS FOV retained", e.what());
    }
    log("Native ADS zoom bound=%d enabled=%d; linear projection scale from game remaining progress",
        zoomBound, cfg.linearADSZoom);
}
float nativeAdsZoomFov(Obj entity, float targetFov, bool engaged) {
    if (!engaged) {
        resetNativeAdsZoom();
        return cfg.fov;
    }
    if (!cfg.linearADSZoom || !cfg.gameADS || !adsState.available || !zoomBound || zoomFailed)
        return targetFov;
    try {
        if (!entity || *static_cast<Class *>(entity) != zoomEntityClass)
            return targetFov;
        auto weapon = api.read<Obj>(entity, entityWeapon), shoot = api.read<Obj>(entity, zoomShoot);
        if (!weapon || *static_cast<Class *>(weapon) != zoomWeaponClass || !shoot ||
            *static_cast<Class *>(shoot) != zoomShootClass || api.read<Obj>(shoot, zoomOwner) != entity)
            return targetFov;
        if (entity != zoomActor || weapon != zoomWeapon) {
            resetNativeAdsZoom();
            zoomActor = entity;
            zoomWeapon = weapon;
        }
        bool active = api.read<unsigned char>(shoot, zoomActive) != 0;
        // Crosshair and shoot state matched in the capture. If a callback observes
        // the one-frame start before the timer is active, wait at ordinary FOV.
        if (!active) {
            zoomWasActive = false;
            return cfg.fov;
        }
        auto sample = nativeAdsProgress(true, api.read<float>(shoot, zoomRemaining),
                                        api.read<float>(shoot, zoomSeconds));
        if (!sample.valid)
            throw std::runtime_error("Native ADS remaining time outside verified domain");
        if (sample.duration > 0 && (!zoomWasActive || std::abs(sample.duration - zoomLastDuration) > .005f)) {
            zoomLastDuration = sample.duration;
            logDebug("Native ADS zoom duration=%.4fs progress=%.4f; current weapon+attachments, no second modifier",
                sample.duration, sample.progress);
        }
        zoomWasActive = true;
        return linearZoomFov(cfg.fov, targetFov, sample.progress);
    } catch (const std::exception &e) {
        zoomFailed = true;
        logError("Optional native ADS zoom disabled: %s; previous ADS FOV retained", e.what());
        return targetFov;
    }
}
