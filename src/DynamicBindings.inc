// Included inside namespace runtime. All metadata work happens during binding;
// member resolutions are cached before gameplay callbacks begin.
inline const char *Api::checkedText(const char *p) {
    if (!p)
        throw std::runtime_error("Null runtime metadata text");
    size_t scanned{};
    while (scanned < 1024) {
        MEMORY_BASIC_INFORMATION info{};
        if (!VirtualQuery(p + scanned, &info, sizeof(info)) || info.State != MEM_COMMIT ||
            info.Protect & (PAGE_GUARD | PAGE_NOACCESS))
            break;
        auto available = reinterpret_cast<const char *>(info.BaseAddress) + info.RegionSize - (p + scanned);
        auto count = std::min(size_t(available), 1024 - scanned);
        if (memchr(p + scanned, 0, count))
            return p;
        scanned += count;
    }
    throw std::runtime_error("Runtime metadata text bound exceeded");
}
inline std::string Api::fullName(Class c) {
    if (!c)
        return "";
    std::string ns = checkedText(classNamespace(c)), name = checkedText(className(c));
    return ns.empty() ? name : ns + "." + name;
}
inline const BindingClass *Api::descriptor(const std::string &name, const char *image) {
    const BindingClass *found{};
    for (size_t i = 0; i < catalogCount; ++i) {
        const auto &candidate = catalog[i];
        if (name != candidate.name || (image && strcmp(image, candidate.image)))
            continue;
        if (found)
            return nullptr;
        found = &candidate;
    }
    return found;
}
inline const BindingClass *Api::descriptor(Class c) {
    auto it = classSpecs.find(c);
    if (it != classSpecs.end())
        return it->second;
    auto spec = descriptor(fullName(c));
    if (spec)
        classSpecs[c] = spec;
    return spec;
}
inline std::string Api::shape(Class c) {
    std::vector<std::string> rows;
    auto parent = classParent(c);
    rows.push_back("P:" + (parent ? coarseBinding(fullName(parent)) : ""));
    void *iter{};
    unsigned count{};
    while (auto f = fields(c, &iter)) {
        if (++count > 512)
            throw std::runtime_error("Binding field enumeration bound");
        rows.push_back("F:" + coarseBinding(checkedText(typeName(fieldType(f)))) + ":" +
                       (fieldFlags(f) & 16 ? "true:" : "false:") + stableBinding(checkedText(fieldName(f))));
    }
    iter = nullptr;
    count = 0;
    while (auto m = methods(c, &iter)) {
        if (++count > 2048)
            throw std::runtime_error("Binding method enumeration bound");
        auto argc = methodParamCount(m);
        if (argc > 64)
            throw std::runtime_error("Binding parameter enumeration bound");
        std::string type = checkedText(typeName(methodResult(m)));
        type += "(";
        for (unsigned i = 0; i < argc; ++i) {
            if (i)
                type += "|";
            type += checkedText(typeName(methodParam(m, i)));
        }
        type += ")";
        rows.push_back("M:" + coarseBinding(type) + ":" + stableBinding(checkedText(methodName(m))));
    }
    return joinBindingShape(std::move(rows));
}
inline const MigrationType *Api::migrationType(const std::string &name, bool target) {
    const MigrationType *found{};
    for (const auto &entry : migrationTypes) {
        if (name != (target ? entry.target : entry.name))
            continue;
        if (found && strcmp(found->name, entry.name))
            throw std::runtime_error("Ambiguous migration type: " + name);
        found = &entry;
    }
    return found;
}
inline bool Api::migrationValid(Class c) {
    if (!c)
        return false;
    auto cached = migrationValidated.find(c);
    if (cached != migrationValidated.end())
        return cached->second;
    auto spec = migrationType(fullName(c), true);
    bool valid = spec && shape(c) == spec->shape;
    migrationValidated[c] = valid;
    return valid;
}
inline const MigrationField *Api::migrationField(Class c, const char *name) {
    if (!migrationValid(c))
        return nullptr;
    auto owner = migrationType(fullName(c), true);
    for (const auto &entry : migrationFields)
        if (!strcmp(entry.owner, owner->name) && !strcmp(entry.name, name))
            return &entry;
    return nullptr;
}
inline const MigrationMethod *Api::migrationMethod(Class c, const char *name, int argc) {
    if (!migrationValid(c))
        return nullptr;
    auto owner = migrationType(fullName(c), true);
    const MigrationMethod *found{};
    for (const auto &entry : migrationMethods)
        if (!strcmp(entry.owner, owner->name) && !strcmp(entry.name, name) &&
            int(bindingParameters(entry.params).size()) == argc) {
            if (found)
                throw std::runtime_error(std::string("Ambiguous migration overload: ") + name);
            found = &entry;
        }
    return found;
}
inline bool Api::namedClass(Class c, const char *expected) {
    if (!c)
        return false;
    if (!strcmp(className(c), expected) || fullName(c) == expected)
        return true;
    auto migration = migrationType(expected);
    return migration && fullName(c) == migration->target && migrationValid(c);
}
inline Field Api::uniqueReferenceField(Class c, Class type, bool isStatic) {
    Field found{};
    if (!type || isValueType(type))
        throw std::runtime_error("Reference field requires a reference type");
    for (unsigned depth{}; c && depth < 8; ++depth, c = classParent(c)) {
        void *iterator{};
        unsigned count{};
        while (auto f = fields(c, &iterator)) {
            if (++count > 512)
                throw std::runtime_error("Typed reference field enumeration bound");
            if (bool(fieldFlags(f) & 16) != isStatic || classFromType(fieldType(f)) != type)
                continue;
            if (found)
                throw std::runtime_error("Ambiguous typed reference field");
            found = f;
        }
    }
    if (!found)
        throw std::runtime_error("Typed reference field unavailable");
    return found;
}
inline Class Api::find(const char *imageNameRequested, const char *ns, const char *name) {
    std::string requested = *ns ? std::string(ns) + "." + name : name;
    std::string key = std::string(imageNameRequested) + ":" + requested;
    auto it = classAliases.find(key);
    if (it != classAliases.end())
        return it->second;
    auto image = findImage(imageNameRequested);
    auto found = classFromName(image, ns, name);
    auto spec = bindingsEnabled ? descriptor(requested, imageNameRequested) : nullptr;
    if (!found && bindingsEnabled) {
        auto migration = migrationType(requested);
        if (migration && !strcmp(migration->image, imageNameRequested)) {
            std::string target = migration->target;
            auto dot = target.rfind('.');
            auto candidate = classFromName(image, dot == std::string::npos ? "" : target.substr(0, dot).c_str(),
                                          dot == std::string::npos ? target.c_str() : target.substr(dot + 1).c_str());
            if (!candidate) {
                auto count = imageClassCount(image);
                if (count > 100000)
                    throw std::runtime_error("Migration class enumeration bound");
                for (unsigned i = 0; i < count; ++i) {
                    auto item = imageClass(image, i);
                    if (!item || fullName(item) != target)
                        continue;
                    if (candidate)
                        throw std::runtime_error("Ambiguous nested migration type: " + requested);
                    candidate = item;
                }
            }
            if (candidate && migrationValid(candidate)) {
                found = candidate;
                ++resolvedClasses;
            }
        }
    }
    if (!found && !spec) {
        auto count = imageClassCount(image);
        if (count > 100000)
            throw std::runtime_error("Named nested class enumeration bound");
        for (unsigned i = 0; i < count; ++i) {
            auto item = imageClass(image, i);
            if (!item || fullName(item) != requested)
                continue;
            if (found)
                throw std::runtime_error("Ambiguous named nested type: " + requested);
            found = item;
        }
    }
    if (!found && spec) {
        auto index = shapeIndexes.find(image);
        if (index == shapeIndexes.end()) {
            std::unordered_map<std::string, std::vector<Class>> candidates;
            auto count = imageClassCount(image);
            if (count > 100000)
                throw std::runtime_error("Binding image class bound");
            // Index only counts first. Build full type-name shapes lazily for a
            // matching bucket, avoiding whole-game signature string allocation.
            for (unsigned i = 0; i < count; ++i) {
                auto candidate = imageClass(image, i);
                if (!candidate)
                    continue;
                void *iter{};
                unsigned fieldCount{}, methodCount{};
                while (fields(candidate, &iter) && fieldCount <= 512)
                    ++fieldCount;
                if (fieldCount > 512)
                    continue;
                iter = nullptr;
                while (methods(candidate, &iter) && methodCount <= 2048)
                    ++methodCount;
                if (methodCount > 2048)
                    continue;
                candidates[std::to_string(fieldCount) + ":" + std::to_string(methodCount)].push_back(
                    candidate);
            }
            index = shapeIndexes.emplace(image, std::move(candidates)).first;
        }
        for (const auto &bucket : index->second) {
            auto separator = bucket.first.find(':');
            auto fieldCount = std::stoul(bucket.first.substr(0, separator));
            auto methodCount = std::stoul(bucket.first.substr(separator + 1));
            if (fieldCount < spec->fieldCount || methodCount < spec->methodCount ||
                (fieldCount + methodCount + 1) * 100 > (spec->fieldCount + spec->methodCount + 1) * 112)
                continue;
            for (auto candidate : bucket.second) {
                auto liveShape = classShapes.find(candidate);
                if (liveShape == classShapes.end()) {
                    try {
                        liveShape = classShapes.emplace(candidate, shape(candidate)).first;
                    } catch (const std::exception &) {
                        continue;
                    }
                }
                if (!additiveBindingShape(spec->shape, liveShape->second))
                    continue;
                if (found)
                    throw std::runtime_error("Ambiguous structural class: " + requested);
                found = candidate;
            }
        }
        if (!found)
            throw std::runtime_error("Missing structural class: " + requested);
        ++resolvedClasses;
    }
    if (!found)
        throw std::runtime_error("Class not found: " + requested);
    classAliases[key] = found;
    if (spec)
        classSpecs[found] = spec;
    return found;
}
inline bool Api::equivalent(const char *expected, const void *actual) {
    std::string current = checkedText(typeName(actual));
    if (current == expected)
        return true;
    auto migration = migrationType(expected);
    if (migration) {
        auto dot = std::string(expected).rfind('.');
        auto expectedClass = find(migration->image, dot == std::string::npos ? "" : std::string(expected).substr(0, dot).c_str(),
                                  dot == std::string::npos ? expected : expected + dot + 1);
        return classFromType(actual) == expectedClass;
    }
    auto spec = descriptor(std::string(expected));
    if (spec) {
        std::string name = spec->name;
        auto dot = name.rfind('.');
        auto expectedClass = find(spec->image, dot == std::string::npos ? "" : name.substr(0, dot).c_str(),
                                  dot == std::string::npos ? name.c_str() : name.substr(dot + 1).c_str());
        return classFromType(actual) == expectedClass;
    }
    // For closed generic/array signatures, translate only catalog-confirmed
    // renamed class tokens. Unknown tokens must continue to match literally.
    std::string translated;
    std::string text(expected);
    for (size_t i = 0; i < text.size();) {
        if (!bindingIdentifier(text[i]) && text[i] != '.' && text[i] != '`') {
            translated += text[i++];
            continue;
        }
        auto end = i + 1;
        while (end < text.size() && (bindingIdentifier(text[end]) || text[end] == '.' || text[end] == '`'))
            ++end;
        auto token = text.substr(i, end - i);
        auto tokenSpec = descriptor(token);
        auto tokenMigration = migrationType(token);
        if ((tokenSpec || tokenMigration) && coarseBinding(token) != token) {
            auto dot = token.rfind('.');
            auto c = find(tokenSpec ? tokenSpec->image : tokenMigration->image, dot == std::string::npos ? "" : token.substr(0, dot).c_str(),
                          dot == std::string::npos ? token.c_str() : token.substr(dot + 1).c_str());
            translated += fullName(c);
        } else
            translated += token;
        i = end;
    }
    return translated == current;
}
inline bool Api::signature(Method m, const BindingMethod &spec) {
    auto params = bindingParameters(spec.parameters);
    if (methodParamCount(m) != params.size() || !equivalent(spec.result, methodResult(m)))
        return false;
    for (unsigned i = 0; i < params.size(); ++i)
        if (!equivalent(params[i].c_str(), methodParam(m, i)))
            return false;
    return true;
}
inline Field Api::field(Class c, const char *name) {
    if (!bindingsEnabled)
        return fieldRaw(c, name);
    if (!c)
        return nullptr;
    auto key = std::to_string(reinterpret_cast<uintptr_t>(c)) + ":" + name;
    auto cached = fieldAliases.find(key);
    if (cached != fieldAliases.end())
        return cached->second;
    auto found = fieldRaw(c, name);
    for (Class owner = c; owner; owner = classParent(owner)) {
        auto migration = migrationField(owner, name);
        if (!migration)
            continue;
        auto mapped = fieldRaw(owner, migration->target);
        if (!mapped || fieldFlags(mapped) != migration->flags || fieldOffset(mapped) != migration->offset ||
            strcmp(checkedText(typeName(fieldType(mapped))), migration->type))
            throw std::runtime_error(std::string("Migration field changed: ") + name);
        fieldAliases[key] = mapped;
        ++resolvedFields;
        return mapped;
    }
    const BindingField *spec{};
    Class owner = c;
    for (unsigned depth = 0; owner && depth < 8 && !spec; ++depth) {
        auto cls = descriptor(owner);
        if (cls)
            for (unsigned i = 0; i < cls->fieldCount; ++i) {
                auto &f = bindingFields[cls->fieldStart + i];
                if (!strcmp(f.name, name)) {
                    spec = &f;
                    break;
                }
            }
        if (!spec)
            owner = classParent(owner);
    }
    if (spec) {
        auto valid = [&](Field f) {
            return bool(fieldFlags(f) & 16) == spec->isStatic && equivalent(spec->type, fieldType(f));
        };
        if (found && !valid(found))
            throw std::runtime_error(std::string("Named field signature changed: ") + name);
        if (!found) {
            void *iter{};
            unsigned count{};
            while (auto f = fields(owner, &iter)) {
                if (++count > 512)
                    throw std::runtime_error("Field matching bound exceeded");
                if (!valid(f))
                    continue;
                if (found)
                    throw std::runtime_error(std::string("Ambiguous structural field: ") + name);
                found = f;
            }
            if (found)
                ++resolvedFields;
        }
    }
    if (found)
        fieldAliases[key] = found;
    return found;
}
inline Method Api::method(Class c, const char *name, int argc) {
    if (!bindingsEnabled)
        return methodRaw(c, name, argc);
    if (!c)
        return nullptr;
    auto key = std::to_string(reinterpret_cast<uintptr_t>(c)) + ":" + name + ":" + std::to_string(argc);
    auto cached = methodAliases.find(key);
    if (cached != methodAliases.end())
        return cached->second;
    auto found = methodRaw(c, name, argc);
    for (Class owner = c; owner; owner = classParent(owner)) {
        auto migration = migrationMethod(owner, name, argc);
        if (!migration)
            continue;
        Method mapped{};
        void *iter{};
        const auto params = bindingParameters(migration->params);
        while (auto candidate = methods(owner, &iter)) {
            if (strcmp(methodName(candidate), migration->target) || methodParamCount(candidate) != params.size() ||
                strcmp(typeName(methodResult(candidate)), migration->result))
                continue;
            unsigned impl{};
            bool valid = methodFlags(candidate, &impl) == migration->flags;
            for (unsigned i = 0; valid && i < params.size(); ++i)
                valid = params[i] == typeName(methodParam(candidate, i));
            if (!valid)
                continue;
            if (mapped)
                throw std::runtime_error(std::string("Ambiguous migrated method: ") + name);
            mapped = candidate;
        }
        if (!mapped)
            throw std::runtime_error(std::string("Migration method changed: ") + name);
        methodAliases[key] = mapped;
        ++resolvedMethods;
        return mapped;
    }
    const BindingMethod *spec{};
    Class owner = c;
    for (unsigned depth = 0; owner && depth < 8 && !spec; ++depth) {
        auto cls = descriptor(owner);
        if (cls)
            for (unsigned i = 0; i < cls->methodCount; ++i) {
                const auto &m = bindingMethods[cls->methodStart + i];
                if (!strcmp(m.name, name) && int(bindingParameters(m.parameters).size()) == argc) {
                    if (spec)
                        throw std::runtime_error(std::string("Ambiguous baseline method signature: ") + name);
                    spec = &m;
                }
            }
        if (!spec)
            owner = classParent(owner);
    }
    if (spec) {
        if (found && !signature(found, *spec))
            throw std::runtime_error(std::string("Named method signature changed: ") + name);
        if (!found) {
            void *iter{};
            unsigned count{};
            while (auto m = methods(owner, &iter)) {
                if (++count > 2048)
                    throw std::runtime_error("Method matching bound exceeded");
                if (!signature(m, *spec))
                    continue;
                if (found)
                    throw std::runtime_error(std::string("Ambiguous structural method: ") + name);
                found = m;
            }
            if (found)
                ++resolvedMethods;
        }
    }
    if (found)
        methodAliases[key] = found;
    return found;
}
inline Method Api::requireTyped(Class c, const char *name, const std::vector<const char *> &params,
                                const char *result, bool isStatic) {
    Method found{};
    for (unsigned depth = 0; c && depth < 8; ++depth, c = classParent(c)) {
        auto migration = migrationMethod(c, name, int(params.size()));
        auto currentName = migration ? migration->target : name;
        void *iter{};
        unsigned count{};
        while (auto m = methods(c, &iter)) {
            if (++count > 2048)
                throw std::runtime_error("Typed overload enumeration bound");
            if (strcmp(checkedText(methodName(m)), currentName) || methodParamCount(m) != params.size())
                continue;
            unsigned impl{};
            if (bool(methodFlags(m, &impl) & 16) != isStatic || !equivalent(result, methodResult(m)))
                continue;
            bool matches = true;
            for (unsigned i = 0; i < params.size(); ++i)
                if (!equivalent(params[i], methodParam(m, i))) {
                    matches = false;
                    break;
                }
            if (!matches)
                continue;
            if (found)
                throw std::runtime_error(std::string("Ambiguous typed method: ") + name);
            found = m;
        }
        // A declared exact overload takes precedence over inherited hiding.
        if (found)
            return found;
    }
    throw std::runtime_error(std::string("Typed method unavailable: ") + name);
}
inline void Api::validateAbi() {
    const auto vector = find("UnityEngine.CoreModule.dll", "UnityEngine", "Vector3");
    const auto quaternion = find("UnityEngine.CoreModule.dll", "UnityEngine", "Quaternion");
    if (!isValueType(vector) || instanceSize(vector) != 28 || !isValueType(quaternion) ||
        instanceSize(quaternion) != 32)
        throw std::runtime_error("Unsupported Unity value ABI");
    for (const auto &pair : {std::pair<const char *, int>{"x", 16}, {"y", 20}, {"z", 24}}) {
        auto f = fieldRaw(vector, pair.first);
        if (!f || fieldOffset(f) != pair.second || strcmp(typeName(fieldType(f)), "System.Single") ||
            *reinterpret_cast<const void **>(static_cast<char *>(f) + 8) != fieldType(f))
            throw std::runtime_error("Unsupported FieldInfo/value layout");
    }
    auto camera = find("UnityEngine.CoreModule.dll", "UnityEngine", "Camera");
    auto m = methodRaw(camera, "WorldToScreenPoint", 1);
    if (!m || methodParamCount(m) != 1 || classFromType(methodResult(m)) != vector ||
        classFromType(methodParam(m, 0)) != vector ||
        *reinterpret_cast<const void *const *>(static_cast<const char *>(m) + 32) != methodResult(m) ||
        strcmp(*reinterpret_cast<const char *const *>(static_cast<const char *>(m) + 16), methodName(m)))
        throw std::runtime_error("Unsupported MethodInfo ABI");
    unsigned impl{};
    auto flags = methodFlags(m, &impl);
    unsigned short direct{};
    memcpy(&direct, static_cast<const char *>(m) + 68, 2);
    if ((flags & 0xffff) != direct || flags & 16)
        throw std::runtime_error("Unsupported method flag ABI");
    auto array = arrayNew(vector, 2);
    if (!RuntimeImage::readable(array, 56))
        throw std::runtime_error("Unsupported array allocation/header");
    auto arrayClass = *static_cast<Class *>(array);
    size_t length{};
    memcpy(&length, static_cast<char *>(array) + 24, 8);
    if (length != 2 || elementClass(arrayClass) != vector || elementSize(arrayClass) != 12)
        throw std::runtime_error("Unsupported managed array ABI");
    bindingsEnabled = true;
}
