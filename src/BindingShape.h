#pragma once
#include <string>
#include <vector>
#include <algorithm>
namespace runtime {
inline bool bindingIdentifier(char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_';
}
inline bool obfuscatedBinding(const std::string &token) {
    if (token.size() != 11)
        return false;
    for (char c : token)
        if (c < 'A' || c > 'Z')
            return false;
    return true;
}
inline std::string coarseBinding(const std::string &text) {
    std::string result;
    for (size_t i = 0; i < text.size();) {
        if (!bindingIdentifier(text[i])) {
            result += text[i++];
            continue;
        }
        auto end = i + 1;
        while (end < text.size() && bindingIdentifier(text[end]))
            ++end;
        auto token = text.substr(i, end - i);
        result += obfuscatedBinding(token) ? "?" : token;
        i = end;
    }
    return result;
}
inline std::string stableBinding(const std::string &name) {
    return coarseBinding(name) == name ? name : "";
}
inline std::string joinBindingShape(std::vector<std::string> rows) {
    std::sort(rows.begin(), rows.end());
    std::string result;
    for (const auto &row : rows) {
        if (!result.empty())
            result += '\n';
        result += row;
    }
    return result;
}
inline std::vector<std::string> bindingParameters(const char *parameters) {
    std::string text(parameters);
    std::vector<std::string> result;
    size_t start = 0;
    while (start < text.size()) {
        auto end = text.find('|', start);
        result.push_back(text.substr(start, end == std::string::npos ? end : end - start));
        if (end == std::string::npos)
            break;
        start = end + 1;
    }
    return result;
}
} // namespace runtime
