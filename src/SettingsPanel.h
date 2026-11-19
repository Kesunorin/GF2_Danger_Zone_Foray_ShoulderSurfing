#pragma once
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <atomic>
#include <mutex>
#include <string>
#include <vector>
#include <cmath>
#include <iterator>
#include <cwchar>
#include <algorithm>
#include <cwctype>
#include "KeyBindings.h"

namespace settings {
inline std::mutex iniMutex;
inline std::atomic<HWND> panel{};
inline std::atomic<bool> started{};
inline std::atomic<bool> opening{};
inline std::atomic<int> toggleKey{VK_F10};
inline std::wstring iniPath;
inline HMODULE module{};
inline HWND owner{};
inline void (*reloadRequest)(){};
inline std::atomic<int> streamingState{};
inline std::atomic<float> streamingNear{}, streamingPreload{};
enum class Kind { Number, Check, Key };
struct Field {
    const wchar_t *section;
    const wchar_t *key;
    const wchar_t *label;
    const wchar_t *fallback;
    Kind kind;
    float lo{}, hi{};
};
inline const Field fields[] = {
    {L"Camera", L"Distance", L"相机后退距离", L"3.2", Kind::Number, .5f, 10},
    {L"Camera", L"Height", L"相机锚点高度", L"1.55", Kind::Number, .2f, 4},
    {L"Camera", L"ShoulderOffset", L"肩位偏移", L"0.65", Kind::Number, 0, 2},
    {L"Camera", L"FOV", L"普通视野角", L"70", Kind::Number, 25, 110},
    {L"Camera", L"ADSFOV", L"固定开镜视野（回退值）", L"50", Kind::Number, 15, 100},
    {L"Camera", L"Sensitivity", L"普通鼠标灵敏度", L"0.16", Kind::Number, .01f, 2},
    {L"Camera", L"ADSSensitivity", L"开镜基础灵敏度", L"0.16", Kind::Number, .01f, 2},
    {L"Camera", L"RenderDistance", L"远裁剪距离（0 沿用）", L"0", Kind::Number, 0, 5000},
    {L"Camera", L"StreamingDistanceMultiplier", L"建筑加载范围倍率（0 沿用）", L"0", Kind::Number, 0, 8},
    {L"Camera", L"MinPitch", L"抬头角度下限", L"-45", Kind::Number, -80, 0},
    {L"Camera", L"MaxPitch", L"低头角度上限", L"65", Kind::Number, 1, 80},
    {L"Camera", L"Smoothing", L"相机位置平滑速度", L"16", Kind::Number, 0, 50},
    {L"Camera", L"CollisionRadius", L"相机碰撞半径", L"0.18", Kind::Number, .05f, .5f},
    {L"Camera", L"InvertY", L"反转鼠标 Y 轴", L"0", Kind::Check},
    {L"Camera", L"Collision", L"相机碰撞", L"1", Kind::Check},
    {L"Gameplay", L"CenterAim", L"屏幕中心瞄准", L"1", Kind::Check},
    {L"Gameplay", L"TurnCharacter", L"角色水平转向", L"1", Kind::Check},
    {L"Input", L"AutoReleaseUI", L"游戏界面自动释放鼠标", L"1", Kind::Check},
    {L"Input", L"AutoResumeUI", L"菜单 / 切回游戏自动捕获", L"1", Kind::Check},
    {L"Keys", L"ToggleCamera", L"视角开关", L"F7", Kind::Key},
    {L"Keys", L"SwapShoulder", L"左右肩切换", L"F6", Kind::Key},
    {L"Keys", L"CaptureMouse", L"鼠标捕获 / 释放", L"F8", Kind::Key},
    {L"Keys", L"ReloadConfig", L"重新读取配置", L"F9", Kind::Key},
    {L"Keys", L"SettingsPanel", L"打开设置面板", L"F10", Kind::Key},
    {L"Keys", L"LeftShoulder", L"直接切换左肩", L"NONE", Kind::Key},
    {L"Keys", L"RightShoulder", L"直接切换右肩", L"NONE", Kind::Key},
    {L"Camera", L"AutoOpticMagnification", L"自动倍率（已核对瞄具）", L"1", Kind::Check},
    {L"Camera", L"LinearADSZoom", L"开镜放大随瞄准进度", L"1", Kind::Check},
    {L"Camera", L"RenderGeometryCollision", L"静态墙面补充碰撞", L"1", Kind::Check},
    {L"Gameplay", L"UseGameADS", L"跟随游戏开镜状态", L"1", Kind::Check},
    {L"Camera", L"UseGameSensitivity", L"联动原生准星灵敏度", L"1", Kind::Check},
    {L"Camera", L"SensitivityMultiplier", L"普通灵敏度系数", L"1", Kind::Number, .1f, 5},
    {L"Camera", L"ADSSensitivityMultiplier", L"开镜灵敏度系数", L"1", Kind::Number, .1f, 5},
    {L"Diagnostics", L"DebugLogging", L"详细诊断日志（Debug）", L"0", Kind::Check},
};
enum class Page { Camera, Aim, Input };
inline Page fieldPage(const Field &field) {
    if (!wcscmp(field.section, L"Input") || !wcscmp(field.section, L"Keys") ||
        !wcscmp(field.section, L"Diagnostics"))
        return Page::Input;
    if (!wcscmp(field.section, L"Gameplay") || !wcscmp(field.key, L"ADSFOV") ||
        !wcscmp(field.key, L"ADSSensitivity") || !wcscmp(field.key, L"AutoOpticMagnification") ||
        !wcscmp(field.key, L"LinearADSZoom") || !wcscmp(field.key, L"UseGameSensitivity") ||
        !wcscmp(field.key, L"SensitivityMultiplier") || !wcscmp(field.key, L"ADSSensitivityMultiplier"))
        return Page::Aim;
    return Page::Camera;
}
inline Page currentPage = Page::Camera;
inline std::vector<HWND> labels, recordButtons;
inline HWND pageButtons[3]{};
inline std::vector<HWND> editors;
inline HWND status{};
inline HWND streamingStatus{};
inline HFONT font{};
inline int recording = -1;
inline int dpi = 96;
inline int scale(int n) {
    return MulDiv(n, dpi, 96);
}
inline HWND control(HWND parent, const wchar_t *cls, const wchar_t *text, DWORD style, int x, int y,
                    int width, int height, int id = 0) {
    auto window =
        CreateWindowExW(cls == std::wstring(L"EDIT") ? WS_EX_CLIENTEDGE : 0, cls, text,
                        WS_CHILD | WS_VISIBLE | style, scale(x), scale(y), scale(width), scale(height),
                        parent, reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), module, nullptr);
    SendMessageW(window, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
    return window;
}
inline void showPage(Page selected) {
    currentPage = selected;
    recording = -1;
    for (size_t i = 0; i < editors.size(); i++) {
        int show = fieldPage(fields[i]) == selected ? SW_SHOW : SW_HIDE;
        ShowWindow(editors[i], show);
        ShowWindow(labels[i], show);
        if (recordButtons[i])
            ShowWindow(recordButtons[i], show);
    }
    for (int i = 0; i < 3; i++)
        SendMessageW(pageButtons[i], BM_SETCHECK,
                     static_cast<int>(selected) == i ? BST_CHECKED : BST_UNCHECKED, 0);
}
inline std::wstring readValue(const Field &field) {
    wchar_t text[128];
    const wchar_t *fallback = field.fallback;
    wchar_t sensitivity[128]{};
    if (std::wstring(field.key) == L"ADSSensitivity") {
        GetPrivateProfileStringW(L"Camera", L"Sensitivity", L"0.16", sensitivity, 128, iniPath.c_str());
        fallback = sensitivity;
    }
    GetPrivateProfileStringW(field.section, field.key, fallback, text, 128, iniPath.c_str());
    return text;
}
inline void fill(bool defaults) {
    std::lock_guard<std::mutex> lock(iniMutex);
    for (size_t i = 0; i < editors.size(); i++) {
        const auto &field = fields[i];
        auto value = defaults ? std::wstring(field.fallback) : readValue(field);
        if (field.kind == Kind::Check)
            SendMessageW(editors[i], BM_SETCHECK, value != L"0" ? BST_CHECKED : BST_UNCHECKED, 0);
        else
            SetWindowTextW(editors[i], value.c_str());
    }
    recording = -1;
    SetWindowTextW(status, defaults ? L"默认值已填入；点击“保存并应用”才会写入。"
                                    : L"单键可录入或输入名称；NONE 禁用。Esc 始终释放鼠标。");
}
inline bool save() {
    std::vector<std::wstring> values;
    bool used[256]{};
    for (size_t i = 0; i < editors.size(); i++) {
        const auto &field = fields[i];
        wchar_t text[128]{};
        if (field.kind == Kind::Check)
            wcscpy_s(text, SendMessageW(editors[i], BM_GETCHECK, 0, 0) == BST_CHECKED ? L"1" : L"0");
        else
            GetWindowTextW(editors[i], text, 128);
        bool valid = true;
        if (field.kind == Kind::Number) {
            wchar_t *end{};
            float value = wcstof(text, &end);
            while (end && iswspace(*end))
                end++;
            valid = end != text && !*end && std::isfinite(value) && value >= field.lo && value <= field.hi;
            if (std::wstring(field.key) == L"RenderDistance" && value > 0 && value < 25)
                valid = false;
            if (std::wstring(field.key) == L"StreamingDistanceMultiplier" && value > 0 && value < 1)
                valid = false;
        }
        if (field.kind == Kind::Key) {
            int key = shoulder::parseKey(text);
            valid = key >= 0 && key != VK_ESCAPE;
            if (valid && key) {
                if (used[key])
                    valid = false;
                used[key] = true;
            }
        }
        if (!valid) {
            wchar_t message[256];
            if (field.kind == Kind::Number) {
                if (!wcscmp(field.key, L"RenderDistance"))
                    swprintf_s(message, L"请检查 %ls：须为 0 或 25 到 5000。", field.label);
                else if (!wcscmp(field.key, L"StreamingDistanceMultiplier"))
                    swprintf_s(message, L"请检查 %ls：须为 0 或 1 到 8。", field.label);
                else
                    swprintf_s(message, L"请检查 %ls：范围 %g 到 %g。", field.label,
                               static_cast<double>(field.lo), static_cast<double>(field.hi));
            } else
                swprintf_s(message, L"请检查 %ls：须为有效单键或 NONE，不能使用 Esc 或与其他快捷键冲突。",
                           field.label);
            showPage(fieldPage(field));
            SetWindowTextW(status, message);
            SetFocus(editors[i]);
            return false;
        }
        values.emplace_back(text);
    }
    bool success = true;
    DWORD error{};
    {
        std::lock_guard<std::mutex> lock(iniMutex);
        auto temporary = iniPath + L".panel.tmp";
        if (GetFileAttributesW(iniPath.c_str()) != INVALID_FILE_ATTRIBUTES &&
            !CopyFileW(iniPath.c_str(), temporary.c_str(), FALSE)) {
            success = false;
            error = GetLastError();
        }
        for (size_t i = 0; success && i < values.size(); i++)
            if (!WritePrivateProfileStringW(fields[i].section, fields[i].key, values[i].c_str(),
                                            temporary.c_str())) {
                success = false;
                error = GetLastError();
            }
        if (success) {
            WritePrivateProfileStringW(nullptr, nullptr, nullptr, temporary.c_str());
            if (!MoveFileExW(temporary.c_str(), iniPath.c_str(),
                             MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
                success = false;
                error = GetLastError();
            } else
                WritePrivateProfileStringW(nullptr, nullptr, nullptr, iniPath.c_str());
        }
    }
    if (!success) {
        wchar_t message[128];
        swprintf_s(message, L"保存失败（Windows 错误 %lu）。原配置尚未替换。", error);
        SetWindowTextW(status, message);
        return false;
    }
    if (reloadRequest)
        reloadRequest();
    SetWindowTextW(status, L"已保存并应用；开启自动捕获时，关闭面板返回肩视角会自动恢复。");
    return true;
}
inline void hide() {
    recording = -1;
    ShowWindow(panel.load(), SW_HIDE);
    opening = false;
    if (IsWindow(owner))
        SetForegroundWindow(owner);
}
inline LRESULT CALLBACK windowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    switch (message) {
    case WM_CREATE: {
        editors.clear();
        labels.clear();
        recordButtons.clear();
        dpi = static_cast<int>(GetDpiForWindow(window));
        MONITORINFO monitor{sizeof(monitor)};
        if (GetMonitorInfoW(MonitorFromWindow(window, MONITOR_DEFAULTTONEAREST), &monitor)) {
            int available =
                static_cast<int>(std::min((monitor.rcWork.right - monitor.rcWork.left - 40) * 96 / 800,
                                          (monitor.rcWork.bottom - monitor.rcWork.top - 80) * 96 / 480));
            dpi = std::max(60, std::min(dpi, available));
        }
        RECT bounds{0, 0, scale(800), scale(480)};
        AdjustWindowRectExForDpi(&bounds, WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX, FALSE,
                                 WS_EX_CONTROLPARENT | WS_EX_TOOLWINDOW, GetDpiForWindow(window));
        SetWindowPos(window, nullptr, 0, 0, bounds.right - bounds.left, bounds.bottom - bounds.top,
                     SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
        font = CreateFontW(-scale(13), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                           OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH,
                           L"Microsoft YaHei UI");
        const wchar_t *pageNames[] = {L"相机", L"瞄准", L"操作"};
        size_t counts[3]{}, rows[3]{};
        for (const auto &field : fields)
            ++counts[static_cast<int>(fieldPage(field))];
        for (int i = 0; i < 3; i++)
            pageButtons[i] =
                control(window, L"BUTTON", pageNames[i], BS_RADIOBUTTON | BS_PUSHLIKE | WS_TABSTOP,
                        20 + i * 132, 16, 120, 32, 10 + i);
        control(window, L"STATIC", L"切换页面保留未保存修改；保存会应用所有页面。", 0, 20, 58, 752, 24);
        for (size_t i = 0; i < std::size(fields); i++) {
            const auto &field = fields[i];
            int page = static_cast<int>(fieldPage(field));
            size_t half = (counts[page] + 1) / 2, index = rows[page]++;
            bool left = index < half;
            int row = static_cast<int>(left ? index : index - half);
            int x = left ? 20 : 420, y = 96 + row * 32;
            labels.push_back(control(window, L"STATIC", field.label, 0, x, y + 3, 198, 24));
            auto editor = control(
                window, field.kind == Kind::Check ? L"BUTTON" : L"EDIT", L"",
                field.kind == Kind::Check ? BS_AUTOCHECKBOX | WS_TABSTOP : ES_AUTOHSCROLL | WS_TABSTOP,
                x + 200, y, field.kind == Kind::Key ? 100 : 152, 25, 100 + static_cast<int>(i));
            editors.push_back(editor);
            recordButtons.push_back(field.kind == Kind::Key
                                        ? control(window, L"BUTTON", L"录入", WS_TABSTOP, x + 306, y, 46, 25,
                                                  200 + static_cast<int>(i))
                                        : nullptr);
        }
        status = control(window, L"STATIC", L"", 0, 20, 366, 752, 24);
        streamingStatus = control(window, L"STATIC", L"", 0, 20, 396, 752, 24);
        SetTimer(window, 1, 250, nullptr);
        control(window, L"BUTTON", L"保存并应用", WS_TABSTOP | BS_DEFPUSHBUTTON, 20, 426, 140, 32, 1);
        control(window, L"BUTTON", L"恢复默认（待保存）", WS_TABSTOP, 176, 426, 180, 32, 2);
        control(window, L"BUTTON", L"重新读取文件", WS_TABSTOP, 372, 426, 150, 32, 3);
        control(window, L"BUTTON", L"关闭", WS_TABSTOP, 648, 426, 124, 32, 4);
        showPage(Page::Camera);
        return 0;
    }
    case WM_APP + 1:
        if (IsWindowVisible(window))
            hide();
        else {
            fill(false);
            ShowWindow(window, SW_SHOW);
            SetForegroundWindow(window);
            opening = false;
        }
        return 0;
    case WM_TIMER: {
        wchar_t text[256];
        switch (streamingState.load()) {
        case 1:
            wcscpy_s(text, L"建筑范围：等待肩视角和当前场景加载器。");
            break;
        case 2:
            swprintf_s(text, L"建筑范围已应用：近圈 %.0f，预载 %.0f；已请求原生刷新，按原生预算分批加载。",
                       static_cast<double>(streamingNear.load()),
                       static_cast<double>(streamingPreload.load()));
            break;
        case 3:
            wcscpy_s(text, L"建筑范围未应用：绑定或配置检查失败；详情见本版本日志。");
            break;
        default:
            wcscpy_s(text, L"建筑范围：沿用游戏原值。");
            break;
        }
        SetWindowTextW(streamingStatus, text);
        return 0;
    }
    case WM_COMMAND: {
        int id = LOWORD(wParam);
        if (HIWORD(wParam) != BN_CLICKED)
            return 0;
        if (id == 1)
            save();
        else if (id == 2)
            fill(true);
        else if (id == 3)
            fill(false);
        else if (id == 4)
            hide();
        else if (id >= 10 && id <= 12) {
            showPage(static_cast<Page>(id - 10));
            SetWindowTextW(status, L"切换页面保留修改；点击保存并应用写入所有页面。");
        } else if (id >= 200 && id < 200 + static_cast<int>(std::size(fields))) {
            recording = id - 200;
            SetWindowTextW(status, L"请按一个键进行录入；Esc 取消录入。组合键暂不支持。");
            SetFocus(window);
        }
        return 0;
    }
    case WM_KEYDOWN:
    case WM_SYSKEYDOWN:
        if (recording >= 0) {
            if (wParam != VK_ESCAPE) {
                wchar_t text[32];
                swprintf_s(text, L"VK:0x%02X", static_cast<unsigned>(wParam));
                SetWindowTextW(editors[static_cast<size_t>(recording)], text);
            }
            recording = -1;
            SetWindowTextW(status, L"录入结束；点击“保存并应用”生效。");
            return 0;
        }
        if (wParam == VK_ESCAPE || (toggleKey.load() && wParam == static_cast<WPARAM>(toggleKey.load()))) {
            hide();
            return 0;
        }
        break;
    case WM_SETCURSOR:
        if (LOWORD(lParam) == HTCLIENT) {
            SetCursor(LoadCursorW(nullptr, MAKEINTRESOURCEW(32512)));
            return TRUE;
        }
        break;
    case WM_CLOSE:
        hide();
        return 0;
    case WM_DESTROY:
        KillTimer(window, 1);
        if (font) {
            DeleteObject(font);
            font = nullptr;
        }
        panel.store(nullptr);
        opening = false;
        started = false;
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(window, message, wParam, lParam);
}
inline DWORD WINAPI uiThread(void *) {
    WNDCLASSW wc{};
    wc.lpfnWndProc = windowProc;
    wc.hInstance = module;
    wc.hCursor = LoadCursorW(nullptr, MAKEINTRESOURCEW(32512));
    wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
    wc.lpszClassName = L"GF2ShoulderSettingsV23";
    if (!RegisterClassW(&wc) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
        opening = false;
        started = false;
        return 0;
    }
    auto window = CreateWindowExW(WS_EX_CONTROLPARENT | WS_EX_TOOLWINDOW, wc.lpszClassName,
                                  L"危区探遗 — 越肩模组设置", WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX,
                                  CW_USEDEFAULT, CW_USEDEFAULT, 820, 670, owner, nullptr, module, nullptr);
    if (!window) {
        opening = false;
        started = false;
        return 0;
    }
    panel.store(window);
    if (!PostMessageW(window, WM_APP + 1, 0, 0)) {
        DestroyWindow(window);
        return 0;
    }
    MSG message{};
    while (GetMessageW(&message, nullptr, 0, 0) > 0) {
        bool keyMessage = message.message == WM_KEYDOWN || message.message == WM_SYSKEYDOWN;
        bool closeKey =
            message.wParam == VK_ESCAPE ||
            (recording < 0 && toggleKey.load() && message.wParam == static_cast<WPARAM>(toggleKey.load()));
        if (keyMessage && closeKey) {
            SendMessageW(window, message.message, message.wParam, message.lParam);
            continue;
        }
        if (recording >= 0 || !IsDialogMessageW(window, &message)) {
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }
    }
    return 0;
}
inline bool ownsWindow(HWND window) {
    auto current = panel.load();
    return current && (window == current || GetAncestor(window, GA_ROOTOWNER) == current);
}
inline void toggle(HMODULE instance, const std::wstring &path, HWND gameWindow, void (*reload)()) {
    if (auto window = panel.load()) {
        if (!IsWindowVisible(window))
            opening = true;
        if (!PostMessageW(window, WM_APP + 1, 0, 0))
            opening = false;
        return;
    }
    if (started.exchange(true))
        return;
    opening = true;
    module = instance;
    iniPath = path;
    owner = gameWindow;
    reloadRequest = reload;
    auto thread = CreateThread(nullptr, 0, uiThread, nullptr, 0, nullptr);
    if (thread)
        CloseHandle(thread);
    else {
        opening = false;
        started = false;
    }
}
} // namespace settings
