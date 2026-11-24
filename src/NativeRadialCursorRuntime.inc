// Read the native RadialMenu input mode, independent of a physical T binding.
// All reads occur in the existing game callback. Managed services are not Unity components.
Class radialRegistryClass{}, radialContextClass{}, radialMasterClass{}, radialInputClass{};
Field radialRegistry{}, radialMasterField{}, radialInputField{}, radialModeField{};
int radialModeValue{};
bool radialBound{}, radialFailed{};
void bindNativeRadialCursor() {
    try {
        auto declared = [](Field f) -> Class {
            const void *type{};
            if (f)
                memcpy(&type, static_cast<char *>(f) + 8, 8);
            return type ? api.classFromType(type) : nullptr;
        };
        auto field = [&](Class owner, const char *name, Class type) {
            auto f = api.field(owner, name);
            int offset{};
            if (f)
                memcpy(&offset, static_cast<char *>(f) + 24, 4);
            if (!f || (api.fieldFlags(f) & 16) || offset < 16 ||
                offset > api.instanceSize(owner) - (api.isValueType(type) ? 4 : 8) || declared(f) != type)
                throw std::runtime_error(std::string("Radial input field changed: ") + name);
            return f;
        };
        radialContextClass = api.find("Assembly-CSharp.dll", "", "CBBDNCPODFN");
        radialMasterClass = api.find("Assembly-CSharp.dll", "", "JKHJKOIKDHO");
        radialInputClass = api.find("Assembly-CSharp.dll", "", "AKGDBJLILIF");
        auto mode = api.find("Assembly-CSharp.dll", "", "ADFKBBEONIM");
        radialMasterField = field(radialContextClass, "<KNMKGLNOIBD>k__BackingField", radialMasterClass);
        radialInputField = field(radialMasterClass, "<KEHGKAADKCL>k__BackingField", radialInputClass);
        radialModeField = field(radialInputClass, "NKMMPDEGGPH", mode);
        auto literal = api.field(mode, "RadialMenu");
        if (!literal || !(api.fieldFlags(literal) & 64) || declared(literal) != mode ||
            !api.isValueType(mode) || api.instanceSize(mode) != 20)
            throw std::runtime_error("Radial input enum changed");
        radialModeValue = api.global<int>(literal);
        if (radialModeValue != 5)
            throw std::runtime_error("Radial input enum value changed");
        auto manager = api.find("Assembly-CSharp.dll", "", "LPFJNJKGPFP"),
             base = api.find("Assembly-CSharp.dll", "", "BIEPDOIMCMB");
        radialRegistry = api.field(manager, "FFGIEKMGIPN");
        radialRegistryClass = declared(radialRegistry);
        if (!radialRegistry || !(api.fieldFlags(radialRegistry) & 16) || !radialRegistryClass ||
            api.elementSize(radialRegistryClass) != 8 || api.elementClass(radialRegistryClass) != base)
            throw std::runtime_error("Radial service registry changed");
        radialBound = true;
    } catch (const std::exception &e) {
        radialBound = false;
        logError("Optional radial cursor unavailable: %s; existing menu handling retained", e.what());
    }
    log("Native radial cursor bound=%d; registered input RadialMenu=%d, no key override", radialBound,
        radialModeValue);
}
bool nativeRadialRequestsCursor() {
    if (!radialBound || radialFailed || !api.global<Obj>(rootInstance))
        return false;
    try {
        auto objectClass = [](Obj obj) { return obj ? *static_cast<Class *>(obj) : nullptr; };
        auto array = api.global<Obj>(radialRegistry);
        if (!array)
            return false;
        if (objectClass(array) != radialRegistryClass)
            throw std::runtime_error("Radial registry identity changed");
        auto handle = api.handleNew(array, false);
        if (!handle)
            throw std::runtime_error("Radial registry GC handle unavailable");
        struct Hold {
            unsigned handle;
            ~Hold() { api.handleFree(handle); }
        } hold{handle};
        array = api.handleTarget(handle);
        size_t count{};
        memcpy(&count, static_cast<char *>(array) + 24, 8);
        if (count > 256)
            throw std::runtime_error("Radial service registry bound");
        Obj context{};
        for (size_t i = 0; i < count; ++i) {
            Obj candidate{};
            memcpy(&candidate, static_cast<char *>(array) + 32 + i * 8, 8);
            if (objectClass(candidate) != radialContextClass)
                continue;
            if (context)
                throw std::runtime_error("Multiple registered radial contexts");
            context = candidate;
        }
        if (api.global<Obj>(radialRegistry) != array)
            throw std::runtime_error("Radial service registry changed during read");
        if (!context)
            return false;
        auto master = api.read<Obj>(context, radialMasterField);
        if (objectClass(master) != radialMasterClass)
            return false;
        auto input = api.read<Obj>(master, radialInputField);
        return objectClass(input) == radialInputClass && api.read<int>(input, radialModeField) == radialModeValue;
    } catch (const std::exception &e) {
        radialFailed = true;
        logError("Optional radial cursor disabled: %s; existing menu handling retained", e.what());
        return false;
    }
}
