#pragma once
#include "RuntimeImage.h"
#include "RuntimeExportCatalog.h"
#include "../vendor/minhook/src/hde/hde64.h"
#include <unordered_map>
namespace runtime {
inline std::vector<unsigned char> runtimeFingerprint(const RuntimeImage &image, const void *entry,
                                                     unsigned depth = 0) {
    std::vector<unsigned char> result;
    auto p = static_cast<const unsigned char *>(entry);
    while (result.size() < 96 && image.executable(p, 16)) {
        hde64s instruction{};
        auto length = hde64_disasm(p, &instruction);
        if (!length || instruction.flags & F_ERROR || result.size() + length > 96)
            break;
        std::vector<unsigned char> bytes(p, p + length);
        const unsigned immediate = instruction.flags & F_IMM64   ? 8
                                   : instruction.flags & F_IMM32 ? 4
                                   : instruction.flags & F_IMM16 ? 2
                                   : instruction.flags & F_IMM8  ? 1
                                                                 : 0;
        if ((instruction.flags & F_DISP32) && instruction.modrm_mod == 0 && instruction.modrm_rm == 5 &&
            !instruction.p_67) {
            if (length < immediate + 4)
                return {};
            std::fill(bytes.begin() + length - immediate - 4, bytes.begin() + length - immediate,
                      static_cast<unsigned char>(0));
        }
        if (instruction.flags & F_RELATIVE) {
            if (!immediate || length < immediate)
                return {};
            std::fill(bytes.end() - immediate, bytes.end(), static_cast<unsigned char>(0));
        }
        result.insert(result.end(), bytes.begin(), bytes.end());
        if (instruction.opcode == 0xc3 || instruction.opcode == 0xc2)
            break;
        if (instruction.opcode == 0xe9 || instruction.opcode == 0xeb ||
            (instruction.opcode == 0xff && instruction.modrm_reg == 4)) {
            if ((instruction.flags & F_RELATIVE) && depth < 2) {
                auto relative = immediate == 1
                                    ? static_cast<int32_t>(static_cast<int8_t>(instruction.imm.imm8))
                                    : static_cast<int32_t>(instruction.imm.imm32);
                auto target = reinterpret_cast<uintptr_t>(p + length) + relative;
                auto tail = runtimeFingerprint(image, reinterpret_cast<void *>(target), depth + 1);
                result.insert(result.end(), tail.begin(), tail.end());
            }
            break;
        }
        p += length;
    }
    return result;
}
class RuntimeExports {
    RuntimeImage image_;
    std::vector<RuntimeImage::Export> exports_;
    std::unordered_map<FARPROC, std::vector<unsigned char>> fingerprints_;
    std::unordered_map<unsigned, FARPROC> cache_;

  public:
    unsigned standardCount{}, fingerprintCount{};
    explicit RuntimeExports(HMODULE module) : image_(module), exports_(image_.exports()) {}
    const RuntimeImage &image() const {
        return image_;
    }
    FARPROC resolve(unsigned index) {
        auto cached = cache_.find(index);
        if (cached != cache_.end())
            return cached->second;
        const RuntimeExportSpec *spec{};
        for (const auto &candidate : runtimeExportSpecs)
            if (candidate.index == index) {
                spec = &candidate;
                break;
            }
        if (!spec)
            throw std::runtime_error("No semantic descriptor for runtime API index");
        FARPROC named{};
        for (const auto &e : exports_)
            if (e.name == spec->name) {
                if (named)
                    throw std::runtime_error(std::string("Ambiguous standard runtime API: ") + spec->name);
                named = e.address;
            }
        if (named) {
            ++standardCount;
            cache_[index] = named;
            return named;
        }
        // Encrypted names and prefixes are hints only. Identify the implementation
        // among current exports, requiring one distinct address. RIP references,
        // relative branches and jump-thunk destinations are normalized.
        FARPROC found{};
        for (const auto &e : exports_) {
            auto it = fingerprints_.find(e.address);
            if (it == fingerprints_.end())
                it = fingerprints_
                         .emplace(e.address, runtimeFingerprint(image_, reinterpret_cast<void *>(e.address)))
                         .first;
            const auto &code = it->second;
            if (code.size() != spec->length || memcmp(code.data(), spec->code, code.size()))
                continue;
            if (found && found != e.address)
                throw std::runtime_error(std::string("Ambiguous runtime API implementation: ") + spec->name);
            found = e.address;
        }
        if (!found)
            throw std::runtime_error(std::string("Unsupported runtime API implementation: ") + spec->name);
        ++fingerprintCount;
        cache_[index] = found;
        return found;
    }
};
} // namespace runtime
