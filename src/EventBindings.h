#pragma once
#include <string>
#include <stdexcept>
namespace runtime {
template <typename Api, typename Class> auto listenerSet(Api &api, Class c) {
    decltype(api.fields(c, nullptr)) found{};
    void *iterator{};
    unsigned count{};
    while (auto f = api.fields(c, &iterator)) {
        if (++count > 16)
            throw std::runtime_error("Native event listener field bound");
        if (api.fieldFlags(f) & 16)
            continue;
        const auto type = std::string(api.typeName(api.fieldType(f)));
        if (type.compare(0, 35, "System.Collections.Generic.HashSet<"))
            continue;
        if (found)
            throw std::runtime_error("Native event listener set is ambiguous");
        found = f;
    }
    if (!found || api.fieldOffset(found) < 16 || api.fieldOffset(found) > api.instanceSize(c) - 8)
        throw std::runtime_error("Native event listener set unavailable");
    return found;
}
template <typename Api, typename Class, typename Field>
void eventBranches(Api &api, Class c, Field &first, Field &second) {
    first = second = nullptr;
    Class listener{};
    void *iterator{};
    unsigned count{};
    while (auto f = api.fields(c, &iterator)) {
        if (++count > 16)
            throw std::runtime_error("Native event branch field bound");
        if (api.fieldFlags(f) & 16)
            continue;
        auto type = api.classFromType(api.fieldType(f));
        if (!type || api.isValueType(type))
            continue;
        const auto name = std::string(api.typeName(api.classType(type)));
        if (name.compare(0, 7, "System.") == 0)
            continue;
        listenerSet(api, type);
        if (api.fieldOffset(f) < 16 || api.fieldOffset(f) > api.instanceSize(c) - 8)
            throw std::runtime_error("Native event branch outside owner");
        if (!first) {
            first = f;
            listener = type;
        } else if (!second && type == listener)
            second = f;
        else
            throw std::runtime_error("Native event branches are ambiguous");
    }
    if (!first || !second)
        throw std::runtime_error("Native event requires two matching listener channels");
}
} // namespace runtime
