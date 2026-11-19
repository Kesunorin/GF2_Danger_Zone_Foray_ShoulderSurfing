#pragma once
#include "RuntimeImage.h"
#include "../vendor/minhook/src/hde/hde64.h"
#include <unordered_set>
namespace runtime {
// Supported four-argument, boxed-return HybridCLR Invoker ABI. Validate the
// actual metadata-selected entry, including MethodInfo+80 and all four inputs.
inline bool supportedInvoker(const RuntimeImage &image, const void *entry) {
    static const unsigned char signature[] = {
        0x48, 0x89, 0x5c, 0x24, 0x08, 0x48, 0x89, 0x6c, 0x24, 0x10, 0x48, 0x89, 0x74, 0x24, 0x18, 0x57,
        0x41, 0x54, 0x41, 0x55, 0x41, 0x56, 0x41, 0x57, 0x48, 0x81, 0xec, 0x20, 0x08, 0x00, 0x00, 0x48,
        0x8b, 0x7a, 0x50, 0x4d, 0x8b, 0xe9, 0x49, 0x8b, 0xf0, 0x48, 0x8b, 0xda, 0x4c, 0x8b, 0xf1};
    unsigned char *begin{}, *end{};
    if (!image.functionBounds(entry, begin, end) || begin != entry ||
        !image.executable(entry, sizeof(signature)))
        return false;
    auto p = static_cast<const unsigned char *>(entry);
    unsigned stackBytes{};
    memcpy(&stackBytes, p + 27, 4);
    return stackBytes >= 32 && stackBytes <= 65536 && !(stackBytes & 15) && !memcmp(p, signature, 27) &&
           !memcmp(p + 31, signature + 31, sizeof(signature) - 31);
}
inline bool supportedFrame(const RuntimeImage &image, const void *entry) {
    // RIP displacements and branch addresses vary with the build. Preserve the
    // argument/register assignment, MethodInfo load, and methodData stack-size
    // access that define the ABI used by the optional filters.
    static const unsigned char prefix[] = {0x48, 0x89, 0x5c, 0x24, 0x08, 0x48, 0x89, 0x6c, 0x24,
                                           0x10, 0x48, 0x89, 0x74, 0x24, 0x18, 0x57, 0x41, 0x56,
                                           0x41, 0x57, 0x48, 0x83, 0xec, 0x60, 0x4c, 0x8b, 0x0d};
    auto p = static_cast<const unsigned char *>(entry);
    if (!image.executable(p, 0x8b) || memcmp(p, prefix, 23) || p[23] < 32 || p[23] & 15 ||
        memcmp(p + 24, prefix + 24, sizeof(prefix) - 24) || memcmp(p + 31, "\x49\x8b\xe8\x4c\x8b\x15", 6) ||
        memcmp(p + 41, "\x4c\x8b\xf2\x48\x8b\x3a\x4c\x8b\xf9", 9) ||
        memcmp(p + 0x78, "\x49\x8b\x1f\x8b\x73\x0c\x48\x85\xed", 9) ||
        memcmp(p + 0x87, "\x41\x8b\x7e\x24", 4))
        return false;
    for (unsigned off : {27u, 37u}) {
        int32_t displacement{};
        memcpy(&displacement, p + off, 4);
        auto target = reinterpret_cast<uintptr_t>(p + off + 4) + displacement;
        if (!image.contains(reinterpret_cast<void *>(target), 8) ||
            image.executable(reinterpret_cast<void *>(target)))
            return false;
    }
    return true;
}
inline void *resolveFrameEntry(const RuntimeImage &image, const void *invoker) {
    if (!supportedInvoker(image, invoker))
        throw std::runtime_error("Unsupported HybridCLR Invoker ABI");
    struct Node {
        const void *entry;
        unsigned depth;
    };
    std::vector<Node> queue{{invoker, 0}};
    std::unordered_set<const void *> visited;
    void *found{};
    // Traverse only native direct calls from the metadata-selected Invoker.
    // No global prologue search and no selection of a first generic match.
    for (size_t i = 0; i < queue.size(); ++i) {
        auto node = queue[i];
        if (!visited.insert(node.entry).second)
            continue;
        if (visited.size() > 128)
            throw std::runtime_error("HybridCLR call graph bound exceeded");
        unsigned char *begin{}, *end{};
        if (!image.functionBounds(node.entry, begin, end) || begin != node.entry)
            continue;
        if (supportedFrame(image, begin)) {
            if (found && found != begin)
                throw std::runtime_error("Ambiguous HybridCLR frame entry");
            found = begin;
            continue;
        }
        if (node.depth >= 2)
            continue;
        // Interpreter::Execute includes a large bytecode switch. Its frame
        // construction is in the entry block, so inspect a bounded prefix even
        // when the complete function is much larger than the scan budget.
        auto scanEnd = begin + std::min<ptrdiff_t>(end - begin, 4096);
        for (auto p = begin; p < scanEnd;) {
            if (end - p < 16 && !image.executable(p, 16))
                break;
            hde64s instruction{};
            unsigned length = hde64_disasm(p, &instruction);
            if (!length || instruction.flags & F_ERROR || length > unsigned(end - p))
                break;
            if (instruction.opcode == 0xe8 && length == 5) {
                int32_t relative{};
                memcpy(&relative, p + 1, 4);
                auto address = reinterpret_cast<uintptr_t>(p + 5) + relative;
                auto target = reinterpret_cast<void *>(address);
                unsigned char *childBegin{}, *childEnd{};
                if (image.functionBounds(target, childBegin, childEnd) && childBegin == target) {
                    if (queue.size() >= 512)
                        throw std::runtime_error("HybridCLR call edge bound exceeded");
                    queue.push_back({target, node.depth + 1});
                }
            }
            p += length;
        }
    }
    if (!found)
        throw std::runtime_error("No validated frame entry reachable from current Invoker");
    return found;
}
} // namespace runtime
