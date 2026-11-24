#pragma once
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <string>
#include <stdexcept>
#include <cstring>
#include <cstdio>
#include <memory>
#include <unordered_map>
#include "RuntimeExports.h"
#include "RuntimeEntry.h"
#include "BindingCatalog.h"
#include "BindingMigration.h"
#include "BindingShape.h"
#include "EventBindings.h"
namespace runtime {
using Obj = void *;
using Class = void *;
using Method = const void *;
using Field = void *;
struct Api {
    HMODULE module{};
    Obj (*domainGet)(){};
    Obj *(*assemblies)(Obj, size_t *){};
    Class (*classFromName)(Obj, const char *, const char *){};
    Method (*methodRaw)(Class, const char *, int){};
    Field (*fieldRaw)(Class, const char *){};
    void (*staticGet)(Field, void *){};
    void (*fieldGet)(Obj, Field, void *){};
    int (*stringLength)(Obj){};
    const wchar_t *(*stringChars)(Obj){};
    const char *(*className)(Class){};
    Class (*classFromType)(const void *){};
    Class (*elementClass)(Class){};
    int (*elementSize)(Class){};
    int (*fieldFlags)(Field){};
    bool (*isValueType)(Class){};
    int (*instanceSize)(Class){};
    const void *(*classType)(Class){};
    Obj (*typeObject)(const void *){};
    Obj (*invoke)(Method, Obj, void **, Obj *){};
    Obj (*threadAttach)(Obj){};
    void (*threadDetach)(Obj){};
    Obj (*arrayNew)(Class, size_t){};
    unsigned (*handleNew)(Obj, bool){};
    Obj (*handleTarget)(unsigned){};
    void (*handleFree)(unsigned){};
    void (*formatException)(Obj, char *, int){};
    const char *(*classNamespace)(Class){};
    Class (*classParent)(Class){};
    Field (*fields)(Class, void **){};
    Method (*methods)(Class, void **){};
    unsigned (*imageClassCount)(Obj){};
    Class (*imageClass)(Obj, unsigned){};
    const char *(*typeName)(const void *){};
    const void *(*fieldType)(Field){};
    const char *(*fieldName)(Field){};
    int (*fieldOffset)(Field){};
    const char *(*methodName)(Method){};
    const void *(*methodResult)(Method){};
    const void *(*methodParam)(Method, unsigned){};
    unsigned (*methodParamCount)(Method){};
    unsigned (*methodFlags)(Method, unsigned *){};
    void *(*unbox)(Obj){};
    Obj (*assemblyImage)(Obj){};
    const char *(*imageName)(Obj){};
    std::unique_ptr<RuntimeExports> exportResolver;
    bool bindingsEnabled{};
    unsigned resolvedClasses{}, resolvedFields{}, resolvedMethods{};
    const BindingClass *catalog = bindingClasses;
    size_t catalogCount = sizeof(bindingClasses) / sizeof(bindingClasses[0]);
    std::unordered_map<std::string, Class> classAliases;
    std::unordered_map<Class, const BindingClass *> classSpecs;
    std::unordered_map<std::string, Field> fieldAliases;
    std::unordered_map<std::string, Method> methodAliases;
    std::unordered_map<Obj, std::unordered_map<std::string, std::vector<Class>>> shapeIndexes;
    std::unordered_map<Class, std::string> classShapes;
    std::unordered_map<Class, bool> migrationValidated;
    FARPROC exportAt(unsigned index) {
        if (!exportResolver)
            throw std::runtime_error("Runtime API resolver not initialized");
        return exportResolver->resolve(index);
    }
    void initialize() {
        module = GetModuleHandleW(L"GameAssembly.dll");
        if (!module)
            throw std::runtime_error("GameAssembly is not loaded");
        exportResolver = std::make_unique<RuntimeExports>(module);
#define BIND(member, index) member = reinterpret_cast<decltype(member)>(exportAt(index))
        BIND(domainGet, 0x4b);
        BIND(assemblies, 0x4c);
        BIND(classFromName, 0x12);
        BIND(methodRaw, 0x21);
        BIND(fieldRaw, 0x1c);
        BIND(staticGet, 0x59);
        BIND(fieldGet, 0x53);
        BIND(className, 0x23);
        BIND(instanceSize, 0x31);
        BIND(classFromType, 0x14);
        BIND(elementClass, 0x1a);
        BIND(elementSize, 0x04);
        BIND(fieldFlags, 0x4e);
        BIND(isValueType, 0x3a);
        BIND(classType, 0x2b);
        BIND(typeObject, 0xd9);
        BIND(stringLength, 0xc6);
        BIND(stringChars, 0xc3);
        BIND(invoke, 0xaf);
        BIND(threadAttach, 0xcb);
        BIND(threadDetach, 0xcd);
        BIND(arrayNew, 0x07);
        BIND(handleNew, 0x71);
        BIND(handleTarget, 0x70);
        BIND(handleFree, 0x6f);
        BIND(formatException, 0x5b);
        BIND(classNamespace, 0x24);
        BIND(classParent, 0x26);
        BIND(fields, 0x1d);
        BIND(methods, 0x22);
        BIND(imageClassCount, 0x77);
        BIND(imageClass, 0x76);
        BIND(typeName, 0xd7);
        BIND(fieldType, 0x52);
        BIND(fieldName, 0x4f);
        BIND(fieldOffset, 0x50);
        BIND(methodName, 0x83);
        BIND(methodResult, 0x88);
        BIND(methodParam, 0x85);
        BIND(methodParamCount, 0x86);
        BIND(methodFlags, 0x81);
        BIND(unbox, 0x9a);
#undef BIND
        // Standard getters are preferred when exposed; this client's encrypted
        // variants are disabled, so use the independently validated old layout.
        assemblyImage = reinterpret_cast<decltype(assemblyImage)>(
            exportResolver->image().resolveExport("il2cpp_assembly_get_image", 256, true));
        imageName = reinterpret_cast<decltype(imageName)>(
            exportResolver->image().resolveExport("il2cpp_image_get_name", 256, true));
    }
    Obj imageOf(Obj assembly) {
        if (assemblyImage)
            return assemblyImage(assembly);
        if (!RuntimeImage::readable(assembly, 8))
            throw std::runtime_error("Invalid assembly layout");
        auto image = *static_cast<Obj *>(assembly);
        if (!RuntimeImage::readable(image, 8))
            throw std::runtime_error("Invalid image layout");
        return image;
    }
    const char *nameOfImage(Obj image) {
        if (imageName)
            return imageName(image);
        if (!RuntimeImage::readable(image, 8))
            throw std::runtime_error("Invalid image name layout");
        return checkedText(*static_cast<const char **>(image));
    }
    Obj findImage(const char *requestedImage) {
        size_t n{};
        auto list = assemblies(domainGet(), &n);
        if (n > 1024)
            throw std::runtime_error("Invalid assembly count");
        if (n && !RuntimeImage::readable(list, n * sizeof(Obj)))
            throw std::runtime_error("Invalid assembly list");
        for (size_t i = 0; i < n; i++) {
            auto image = imageOf(list[i]);
            if (!strcmp(nameOfImage(image), requestedImage))
                return image;
        }
        throw std::runtime_error(std::string("Image not found: ") + requestedImage);
    }
    static const char *checkedText(const char *p);
    std::string fullName(Class c);
    const BindingClass *descriptor(const std::string &name, const char *image = nullptr);
    const BindingClass *descriptor(Class c);
    std::string shape(Class c);
    const MigrationType *migrationType(const std::string &name, bool target = false);
    bool migrationValid(Class c);
    const MigrationField *migrationField(Class c, const char *name);
    const MigrationMethod *migrationMethod(Class c, const char *name, int argc);
    bool namedClass(Class c, const char *expected);
    Field uniqueReferenceField(Class c, Class type, bool isStatic = false);
    Class find(const char *image, const char *ns, const char *name);
    bool equivalent(const char *expected, const void *actual);
    bool signature(Method m, const BindingMethod &spec);
    Field field(Class c, const char *name);
    Method method(Class c, const char *name, int argc);
    void validateAbi();
    Method requireTyped(Class c, const char *name, const std::vector<const char *> &params,
                        const char *result, bool isStatic = false);
    Method require(Class c, const char *name, int argc = 0) {
        auto m = method(c, name, argc);
        if (!m)
            throw std::runtime_error(std::string("Method not found: ") + name);
        return m;
    }
    Obj call(Method m, Obj self = nullptr, void **args = nullptr) {
        Obj ex{};
        auto result = invoke(m, self, args, &ex);
        if (ex) {
            char text[1024]{};
            formatException(ex, text, 1024);
            throw std::runtime_error(text);
        }
        return result;
    }
    template <typename T> T value(Method m, Obj self = nullptr, void **args = nullptr) {
        auto boxed = call(m, self, args);
        if (!boxed)
            throw std::runtime_error("Null boxed result");
        T result;
        std::memcpy(&result, unbox ? unbox(boxed) : static_cast<char *>(boxed) + 16, sizeof(T));
        return result;
    }
    template <typename T> T read(Obj obj, Field f) {
        T v{};
        fieldGet(obj, f, &v);
        return v;
    }
    template <typename T> T global(Field f) {
        T v{};
        staticGet(f, &v);
        return v;
    }
    static void *invoker(Method m) {
        return *(reinterpret_cast<void *const *>(m) + 1);
    }
};
#include "DynamicBindings.inc"
} // namespace runtime
