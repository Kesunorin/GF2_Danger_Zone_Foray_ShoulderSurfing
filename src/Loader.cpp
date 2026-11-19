#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <tlhelp32.h>
#include <iostream>
#include <filesystem>
#include <string>

int wmain(int argc, wchar_t **argv) {
    if (argc < 2) {
        std::wcerr << L"Usage: GF2Attach.exe <absolute DLL path> [PID]\n";
        return 1;
    }
    auto dll = std::filesystem::absolute(argv[1]);
    if (!std::filesystem::is_regular_file(dll)) {
        std::wcerr << L"DLL not found\n";
        return 1;
    }
    DWORD pid = argc > 2 ? std::wcstoul(argv[2], nullptr, 10) : 0;
    unsigned matches = 0;
    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    PROCESSENTRY32W p{sizeof(p)};
    if (Process32FirstW(snapshot, &p))
        do {
            if (_wcsicmp(p.szExeFile, L"GF2_Exilium.exe") == 0) {
                if (!pid)
                    pid = p.th32ProcessID;
                matches++;
            }
        } while (Process32NextW(snapshot, &p));
    CloseHandle(snapshot);
    if (!pid || (argc <= 2 && matches != 1)) {
        std::wcerr << L"Start exactly one GF2_Exilium process, then retry\n";
        return 2;
    }
    HANDLE process = OpenProcess(PROCESS_CREATE_THREAD | PROCESS_QUERY_INFORMATION | PROCESS_VM_OPERATION |
                                     PROCESS_VM_WRITE | PROCESS_VM_READ,
                                 FALSE, pid);
    if (!process) {
        std::wcerr << L"OpenProcess error " << GetLastError()
                   << L". Run the attach script at the game's privilege level.\n";
        return 3;
    }
    wchar_t exe[32768];
    DWORD n = 32768;
    if (!QueryFullProcessImageNameW(process, 0, exe, &n) ||
        _wcsicmp(std::filesystem::path(exe).filename().c_str(), L"GF2_Exilium.exe")) {
        CloseHandle(process);
        return 4;
    }
    HANDLE mods = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32, pid);
    MODULEENTRY32W m{sizeof(m)};
    uintptr_t remoteLoad{};
    auto localBase = GetModuleHandleW(L"kernel32.dll");
    // LoadLibraryW may resolve into KernelBase. Find its actual owning module.
    auto localLoad = GetProcAddress(localBase, "LoadLibraryW");
    HMODULE owner{};
    GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                       reinterpret_cast<LPCWSTR>(localLoad), &owner);
    wchar_t ownerPath[32768];
    GetModuleFileNameW(owner, ownerPath, 32768);
    auto ownerName = std::filesystem::path(ownerPath).filename();
    bool already = false, otherVersion = false;
    if (Module32FirstW(mods, &m))
        do {
            if (_wcsicmp(m.szModule, dll.filename().c_str()) == 0)
                already = true;
            else if (_wcsnicmp(m.szModule, L"GF2ShoulderSurfing", 17) == 0)
                otherVersion = true;
            if (_wcsicmp(m.szModule, ownerName.c_str()) == 0)
                remoteLoad = reinterpret_cast<uintptr_t>(m.modBaseAddr) +
                             (reinterpret_cast<uintptr_t>(localLoad) - reinterpret_cast<uintptr_t>(owner));
        } while (Module32NextW(mods, &m));
    CloseHandle(mods);
    if (otherVersion) {
        std::wcerr
            << L"Another ShoulderSurfing version is loaded. Restart the game before attaching this build.\n";
        CloseHandle(process);
        return 10;
    }
    if (already) {
        std::wcout << L"Mod already loaded. F7 toggles it; restart the game before attaching a new build.\n";
        CloseHandle(process);
        return 0;
    }
    if (!remoteLoad) {
        std::wcerr << L"Cannot resolve remote LoadLibraryW\n";
        CloseHandle(process);
        return 5;
    }
    auto path = dll.wstring();
    SIZE_T bytes = (path.size() + 1) * sizeof(wchar_t), written{};
    auto buffer = VirtualAllocEx(process, nullptr, bytes, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (!buffer || !WriteProcessMemory(process, buffer, path.c_str(), bytes, &written) || written != bytes) {
        std::wcerr << L"Cannot write DLL path\n";
        if (buffer)
            VirtualFreeEx(process, buffer, 0, MEM_RELEASE);
        CloseHandle(process);
        return 6;
    }
    auto thread = CreateRemoteThread(
        process, nullptr, 0, reinterpret_cast<LPTHREAD_START_ROUTINE>(remoteLoad), buffer, 0, nullptr);
    if (!thread) {
        std::wcerr << L"CreateRemoteThread error " << GetLastError() << L"\n";
        VirtualFreeEx(process, buffer, 0, MEM_RELEASE);
        CloseHandle(process);
        return 7;
    }
    auto wait = WaitForSingleObject(thread, 15000);
    if (wait != WAIT_OBJECT_0) {
        std::wcerr << L"Load timed out; buffer retained until game exits. Check logs before retrying.\n";
        CloseHandle(thread);
        CloseHandle(process);
        return 8;
    }
    DWORD code{};
    GetExitCodeThread(thread, &code);
    CloseHandle(thread);
    VirtualFreeEx(process, buffer, 0, MEM_RELEASE);
    CloseHandle(process);
    if (!code) {
        std::wcerr << L"LoadLibraryW failed\n";
        return 9;
    }
    std::wcout << L"DLL loaded into PID " << pid << L". Check " << dll.stem().wstring()
               << L".log for initialization.\n";
    return 0;
}
