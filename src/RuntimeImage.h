#pragma once
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <cstdint>
#include <cstring>
#include <stdexcept>
#include <string>
#include <vector>
#include <algorithm>
#include <cstdio>

namespace runtime {
// Only reads this process's loaded image. No disk RVA is used for a Hook.
class RuntimeImage {
    unsigned char *base_{};
    size_t size_{};
    IMAGE_NT_HEADERS64 *nt_{};

  public:
    struct Export {
        std::string name;
        FARPROC address;
    };
    static bool readable(const void *p, size_t bytes) {
        auto cursor = reinterpret_cast<uintptr_t>(p);
        if (!cursor || bytes > UINTPTR_MAX - cursor)
            return false;
        const auto end = cursor + bytes;
        while (cursor < end) {
            MEMORY_BASIC_INFORMATION info{};
            if (!VirtualQuery(reinterpret_cast<void *>(cursor), &info, sizeof(info)) ||
                info.State != MEM_COMMIT || (info.Protect & (PAGE_GUARD | PAGE_NOACCESS)))
                return false;
            auto next = reinterpret_cast<uintptr_t>(info.BaseAddress) + info.RegionSize;
            if (next <= cursor)
                return false;
            cursor = next;
        }
        return true;
    }
    explicit RuntimeImage(HMODULE module) : base_(reinterpret_cast<unsigned char *>(module)) {
        if (!readable(base_, sizeof(IMAGE_DOS_HEADER)))
            throw std::runtime_error("Unreadable runtime image");
        const auto dos = reinterpret_cast<IMAGE_DOS_HEADER *>(base_);
        if (dos->e_magic != IMAGE_DOS_SIGNATURE || dos->e_lfanew < 64 || dos->e_lfanew > 1048576 ||
            !readable(base_ + dos->e_lfanew, sizeof(IMAGE_NT_HEADERS64)))
            throw std::runtime_error("Invalid runtime DOS header");
        nt_ = reinterpret_cast<IMAGE_NT_HEADERS64 *>(base_ + dos->e_lfanew);
        if (nt_->Signature != IMAGE_NT_SIGNATURE || nt_->FileHeader.Machine != IMAGE_FILE_MACHINE_AMD64 ||
            nt_->OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR64_MAGIC ||
            nt_->FileHeader.NumberOfSections == 0 || nt_->FileHeader.NumberOfSections > 96 ||
            nt_->OptionalHeader.SizeOfImage < 4096 || nt_->OptionalHeader.SizeOfImage > 0x60000000)
            throw std::runtime_error("Unsupported runtime PE architecture/layout");
        size_ = nt_->OptionalHeader.SizeOfImage;
        if (!contains(IMAGE_FIRST_SECTION(nt_),
                      nt_->FileHeader.NumberOfSections * sizeof(IMAGE_SECTION_HEADER)))
            throw std::runtime_error("Invalid runtime sections");
    }
    unsigned char *base() const {
        return base_;
    }
    size_t size() const {
        return size_;
    }
    bool contains(const void *p, size_t bytes = 1) const {
        const auto address = reinterpret_cast<uintptr_t>(p), begin = reinterpret_cast<uintptr_t>(base_);
        return address >= begin && bytes <= size_ && address - begin <= size_ - bytes && readable(p, bytes);
    }
    bool executable(const void *p, size_t bytes = 1) const {
        if (!contains(p, bytes))
            return false;
        auto rva = reinterpret_cast<const unsigned char *>(p) - base_;
        const auto sections = IMAGE_FIRST_SECTION(nt_);
        for (unsigned i = 0; i < nt_->FileHeader.NumberOfSections; ++i) {
            const auto &s = sections[i];
            const auto length = std::max(s.Misc.VirtualSize, s.SizeOfRawData);
            if ((s.Characteristics & IMAGE_SCN_MEM_EXECUTE) && rva >= s.VirtualAddress && bytes <= length &&
                size_t(rva - s.VirtualAddress) <= length - bytes)
                return true;
        }
        return false;
    }
    std::string stringAt(DWORD rva) const {
        if (rva >= size_)
            throw std::runtime_error("Runtime string RVA out of bounds");
        auto p = base_ + rva;
        for (size_t n = 0; n < 512 && contains(p, n + 1); ++n)
            if (!p[n])
                return std::string(reinterpret_cast<char *>(p), n);
        throw std::runtime_error("Runtime export name out of bounds");
    }
    FARPROC resolveExport(const char *standard, unsigned index, bool optional = false) const {
        const auto dir = nt_->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXPORT];
        if (dir.Size < sizeof(IMAGE_EXPORT_DIRECTORY) || dir.VirtualAddress >= size_ ||
            dir.Size > size_ - dir.VirtualAddress)
            throw std::runtime_error("Invalid runtime export directory");
        auto e = reinterpret_cast<IMAGE_EXPORT_DIRECTORY *>(base_ + dir.VirtualAddress);
        if (!contains(e, sizeof(*e)) || e->NumberOfNames > 65536 || e->NumberOfFunctions > 65536 ||
            !contains(base_ + e->AddressOfNames, size_t(e->NumberOfNames) * 4) ||
            !contains(base_ + e->AddressOfNameOrdinals, size_t(e->NumberOfNames) * 2) ||
            !contains(base_ + e->AddressOfFunctions, size_t(e->NumberOfFunctions) * 4))
            throw std::runtime_error("Invalid runtime export table");
        const auto names = reinterpret_cast<DWORD *>(base_ + e->AddressOfNames);
        const auto ordinals = reinterpret_cast<WORD *>(base_ + e->AddressOfNameOrdinals);
        const auto functions = reinterpret_cast<DWORD *>(base_ + e->AddressOfFunctions);
        char prefix[8]{};
        sprintf_s(prefix, "i%02x", index);
        FARPROC named{}, indexed{};
        for (DWORD i = 0; i < e->NumberOfNames; ++i) {
            auto name = stringAt(names[i]);
            bool exact = standard && name == standard;
            bool legacy = index <= 255 && name.compare(0, 3, prefix) == 0;
            if (!exact && !legacy)
                continue;
            if (ordinals[i] >= e->NumberOfFunctions)
                throw std::runtime_error("Invalid runtime export ordinal");
            const auto rva = functions[ordinals[i]];
            if (rva >= dir.VirtualAddress && rva - dir.VirtualAddress < dir.Size)
                throw std::runtime_error("Forwarded runtime API unsupported");
            if (rva >= size_ || !executable(base_ + rva))
                throw std::runtime_error("Runtime API outside executable image");
            auto &slot = exact ? named : indexed;
            if (slot)
                throw std::runtime_error(std::string("Ambiguous runtime API: ") +
                                         (standard ? standard : prefix));
            slot = reinterpret_cast<FARPROC>(base_ + rva);
        }
        // Standard names take precedence. Legacy encrypted suffixes are allowed
        // to change, but their verified ordinal scheme is still required.
        if (named)
            return named;
        if (indexed)
            return indexed;
        if (optional)
            return nullptr;
        throw std::runtime_error(std::string("Missing runtime API: ") + (standard ? standard : prefix));
    }
    std::vector<Export> exports() const {
        const auto dir = nt_->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXPORT];
        if (dir.Size < sizeof(IMAGE_EXPORT_DIRECTORY) || dir.VirtualAddress >= size_ ||
            dir.Size > size_ - dir.VirtualAddress)
            throw std::runtime_error("Invalid runtime export directory");
        auto e = reinterpret_cast<IMAGE_EXPORT_DIRECTORY *>(base_ + dir.VirtualAddress);
        if (!contains(e, sizeof(*e)) || e->NumberOfNames > 65536 || e->NumberOfFunctions > 65536 ||
            !contains(base_ + e->AddressOfNames, size_t(e->NumberOfNames) * 4) ||
            !contains(base_ + e->AddressOfNameOrdinals, size_t(e->NumberOfNames) * 2) ||
            !contains(base_ + e->AddressOfFunctions, size_t(e->NumberOfFunctions) * 4))
            throw std::runtime_error("Invalid runtime export table");
        auto names = reinterpret_cast<DWORD *>(base_ + e->AddressOfNames);
        auto ordinals = reinterpret_cast<WORD *>(base_ + e->AddressOfNameOrdinals);
        auto functions = reinterpret_cast<DWORD *>(base_ + e->AddressOfFunctions);
        std::vector<Export> result;
        for (DWORD i = 0; i < e->NumberOfNames; ++i) {
            if (ordinals[i] >= e->NumberOfFunctions)
                throw std::runtime_error("Invalid runtime export ordinal");
            auto rva = functions[ordinals[i]];
            if (rva >= size_ || (rva >= dir.VirtualAddress && rva - dir.VirtualAddress < dir.Size))
                continue;
            if (executable(base_ + rva))
                result.push_back({stringAt(names[i]), reinterpret_cast<FARPROC>(base_ + rva)});
        }
        return result;
    }
    bool functionBounds(const void *p, unsigned char *&begin, unsigned char *&end) const {
        if (!executable(p))
            return false;
        const auto dir = nt_->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXCEPTION];
        if (dir.VirtualAddress >= size_ || dir.Size > size_ - dir.VirtualAddress ||
            dir.Size % sizeof(RUNTIME_FUNCTION) || !contains(base_ + dir.VirtualAddress, dir.Size))
            return false;
        const auto entries = reinterpret_cast<RUNTIME_FUNCTION *>(base_ + dir.VirtualAddress);
        const auto count = dir.Size / sizeof(RUNTIME_FUNCTION);
        const auto rva = static_cast<DWORD>(reinterpret_cast<const unsigned char *>(p) - base_);
        size_t lo = 0, hi = count;
        while (lo < hi) {
            auto mid = lo + (hi - lo) / 2;
            if (entries[mid].BeginAddress <= rva)
                lo = mid + 1;
            else
                hi = mid;
        }
        if (!lo)
            return false;
        const auto &f = entries[lo - 1];
        if (f.BeginAddress > rva || f.EndAddress <= rva || f.EndAddress > size_ ||
            f.BeginAddress >= f.EndAddress ||
            !executable(base_ + f.BeginAddress, f.EndAddress - f.BeginAddress))
            return false;
        begin = base_ + f.BeginAddress;
        end = base_ + f.EndAddress;
        return true;
    }
};
} // namespace runtime
