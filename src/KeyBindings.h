#pragma once
#include <string>
#include <cwctype>
#include <cstdlib>
#include <cerrno>
#include <initializer_list>

namespace shoulder {
// Win32 virtual-key values. Keeping parsing independent of Windows input lets
// malformed user configuration be checked without running the game.
inline int parseKey(std::wstring text) {
    const auto first = text.find_first_not_of(L" \t\r\n");
    if (first == std::wstring::npos)
        return -1;
    text = text.substr(first, text.find_last_not_of(L" \t\r\n") - first + 1);
    for (auto &c : text)
        c = static_cast<wchar_t>(std::towupper(c));
    if (text == L"NONE" || text == L"OFF")
        return 0;
    if (text.size() == 1 && ((text[0] >= L'A' && text[0] <= L'Z') || (text[0] >= L'0' && text[0] <= L'9')))
        return text[0];
    if (text[0] == L'F' && text.size() >= 2 && text.size() <= 3) {
        if (text[1] < L'1' || text[1] > L'9' || (text.size() == 3 && (text[2] < L'0' || text[2] > L'9')))
            return -1;
        wchar_t *end{};
        auto number = std::wcstol(text.c_str() + 1, &end, 10);
        if (*end == 0 && number >= 1 && number <= 24)
            return 0x70 + static_cast<int>(number) - 1;
    }
    struct Name {
        const wchar_t *text;
        int value;
    };
    for (const auto &n :
         {Name{L"ESC", 0x1b},    Name{L"ESCAPE", 0x1b},    Name{L"TAB", 0x09},      Name{L"SPACE", 0x20},
          Name{L"ENTER", 0x0d},  Name{L"BACKSPACE", 0x08}, Name{L"SHIFT", 0x10},    Name{L"CTRL", 0x11},
          Name{L"ALT", 0x12},    Name{L"LSHIFT", 0xa0},    Name{L"RSHIFT", 0xa1},   Name{L"LCTRL", 0xa2},
          Name{L"RCTRL", 0xa3},  Name{L"LALT", 0xa4},      Name{L"RALT", 0xa5},     Name{L"LEFT", 0x25},
          Name{L"UP", 0x26},     Name{L"RIGHT", 0x27},     Name{L"DOWN", 0x28},     Name{L"HOME", 0x24},
          Name{L"END", 0x23},    Name{L"PAGEUP", 0x21},    Name{L"PAGEDOWN", 0x22}, Name{L"INSERT", 0x2d},
          Name{L"DELETE", 0x2e}, Name{L"MOUSE4", 0x05},    Name{L"MOUSE5", 0x06}})
        if (text == n.text)
            return n.value;
    if (text.size() > 3 && text.substr(0, 3) == L"VK:") {
        wchar_t *end{};
        errno = 0;
        auto number = std::wcstol(text.c_str() + 3, &end, 0);
        if (end != text.c_str() + 3 && *end == 0 && errno != ERANGE && number >= 0 && number <= 255)
            return static_cast<int>(number);
    }
    return -1;
}
} // namespace shoulder
