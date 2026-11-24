// Maintenance helper: metadata only, no hooks, Unity/gameplay invocation,
// action calls, object field reads/writes or HybridCLR compilation.
#include "Runtime.h"
#include <filesystem>
#include <fstream>
namespace {
HMODULE ownModule{};
std::string quote(const std::string &s) {
    std::string result = "\"";
    for (unsigned char c : s) {
        if (c == '\\' || c == '"') {
            result += '\\';
            result += char(c);
        } else if (c < 32) {
            char escape[8]{};
            snprintf(escape, sizeof(escape), "\\u%04x", unsigned(c));
            result += escape;
        } else
            result += char(c);
    }
    return result + '"';
}
void collectMetadata() {
    wchar_t path[32768]{};
    GetModuleFileNameW(ownModule, path, 32768);
    const auto directory = std::filesystem::path(path).parent_path();
    const auto output = directory / (L"metadata-" + std::to_wstring(GetCurrentProcessId()) + L".json");
    auto temporary = output;
    temporary += L".tmp";
    runtime::Api api;
    runtime::Obj thread{};
    try {
        api.initialize();
        auto domain = api.domainGet();
        if (!domain)
            throw std::runtime_error("Runtime metadata not initialized; retry after game loading");
        thread = api.threadAttach(domain);
        if (!thread)
            throw std::runtime_error("Metadata thread attachment failed");
        // Reuse the production API and value-layout checks. The only allocated
        // object is the small disposable Vector3[] ABI fixture.
        api.validateAbi();
        std::ofstream file(temporary, std::ios::binary | std::ios::trunc);
        if (!file)
            throw std::runtime_error("Cannot create metadata output");
        file << "{\"schema\":1,\"pid\":" << GetCurrentProcessId()
             << ",\"gameWrites\":false,\"gameplayMethodsInvoked\":false,\"interpreterMetadataCompiled\":false,\"classes\":[";
        bool first = true;
        for (const auto imageName : {"Assembly-CSharp.dll", "StaticTable.dll"}) {
            auto image = api.findImage(imageName);
            auto count = api.imageClassCount(image);
            if (count > 100000)
                throw std::runtime_error("Image class enumeration bound");
            for (unsigned i = 0; i < count; ++i) {
                auto c = api.imageClass(image, i);
                if (!c)
                    continue;
                if (!first)
                    file << ',';
                first = false;
                auto parent = api.classParent(c);
                file << "{\"image\":" << quote(imageName) << ",\"name\":" << quote(api.fullName(c))
                     << ",\"parent\":" << quote(parent ? api.fullName(parent) : "")
                     << ",\"instanceSize\":" << api.instanceSize(c) << ",\"fields\":[";
                void *iterator{};
                unsigned fieldCount{}, methodCount{};
                bool skipped{};
                while (auto f = api.fields(c, &iterator)) {
                    if (fieldCount == 512) { skipped = true; break; }
                    if (fieldCount++)
                        file << ',';
                    file << "{\"name\":" << quote(api.checkedText(api.fieldName(f)))
                         << ",\"type\":" << quote(api.checkedText(api.typeName(api.fieldType(f))))
                         << ",\"flags\":" << api.fieldFlags(f) << ",\"offset\":" << api.fieldOffset(f) << '}';
                }
                file << "],\"methods\":[";
                iterator = nullptr;
                if (!skipped)
                    while (auto m = api.methods(c, &iterator)) {
                        if (methodCount == 2048) { skipped = true; break; }
                        auto parameters = api.methodParamCount(m);
                        if (parameters > 64) { skipped = true; break; }
                        if (methodCount++)
                            file << ',';
                        unsigned impl{};
                        file << "{\"name\":" << quote(api.checkedText(api.methodName(m)))
                             << ",\"returnType\":" << quote(api.checkedText(api.typeName(api.methodResult(m))))
                             << ",\"flags\":" << api.methodFlags(m, &impl) << ",\"params\":[";
                        for (unsigned p = 0; p < parameters; ++p) {
                            if (p)
                                file << ',';
                            file << quote(api.checkedText(api.typeName(api.methodParam(m, p))));
                        }
                        file << "]}";
                    }
                file << "],\"skipped\":" << (skipped ? "true" : "false") << '}';
            }
        }
        file << "],\"complete\":true,\"standardApis\":" << api.exportResolver->standardCount
             << ",\"fingerprintApis\":" << api.exportResolver->fingerprintCount << '}';
        file.close();
        if (!file)
            throw std::runtime_error("Metadata output write failed");
        std::filesystem::rename(temporary, output);
    } catch (const std::exception &e) {
        std::ofstream file(output, std::ios::binary | std::ios::trunc);
        file << "{\"schema\":1,\"pid\":" << GetCurrentProcessId() << ",\"complete\":false,\"error\":" << quote(e.what()) << '}';
    }
    if (thread)
        api.threadDetach(thread);
}
DWORD WINAPI collect(void *) {
    // Destroy Api, streams and temporary buffers before unloading this DLL.
    collectMetadata();
    FreeLibraryAndExitThread(ownModule, 0);
}
} // namespace
BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        ownModule = instance;
        DisableThreadLibraryCalls(instance);
        auto thread = CreateThread(nullptr, 0, collect, nullptr, 0, nullptr);
        if (!thread)
            return FALSE;
        CloseHandle(thread);
    }
    return TRUE;
}
