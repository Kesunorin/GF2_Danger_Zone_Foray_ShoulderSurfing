#pragma once
#include <windows.h>
#include <cstdio>
#include <cstdarg>
#include <mutex>
#include <string>
#include <atomic>

// Two bounded files beside the DLL. Logging failure never interrupts gameplay.
class RuntimeLog {
    std::mutex mutex;
    std::wstring path;
    std::atomic<bool> debugEnabled{};
    static constexpr long long limit = 4 * 1024 * 1024;
    void rotateIfNeeded() {
        FILE *file{};
        if (_wfopen_s(&file, path.c_str(), L"rb") || !file)
            return;
        _fseeki64(file, 0, SEEK_END);
        auto bytes = _ftelli64(file);
        fclose(file);
        if (bytes < limit)
            return;
        auto previous = path.substr(0, path.find_last_of(L'.')) + L".previous.log";
        MoveFileExW(path.c_str(), previous.c_str(), MOVEFILE_REPLACE_EXISTING);
    }

  public:
    enum class Level { Info, Error, Debug };
    void setDebug(bool enabled) { debugEnabled.store(enabled, std::memory_order_relaxed); }
    bool debug() const { return debugEnabled.load(std::memory_order_relaxed); }
    void configure(const std::wstring &filename) {
        std::lock_guard<std::mutex> guard(mutex);
        path = filename;
        rotateIfNeeded();
    }
    void write(const char *format, va_list args, Level level = Level::Info) {
        // Disabled diagnostics return before locking, formatting or accessing the file.
        if (level == Level::Debug && !debug())
            return;
        std::lock_guard<std::mutex> guard(mutex);
        if (path.empty())
            return;
        // Stat only at a write, with no additional worker or timer.
        rotateIfNeeded();
        FILE *file{};
        if (_wfopen_s(&file, path.c_str(), L"a") || !file)
            return;
        SYSTEMTIME time{};
        GetLocalTime(&time);
        fprintf(file, "%04u-%02u-%02u %02u:%02u:%02u.%03u PID=%lu ", time.wYear, time.wMonth, time.wDay,
                time.wHour, time.wMinute, time.wSecond, time.wMilliseconds, GetCurrentProcessId());
        fprintf(file, "[%s] ", level == Level::Debug ? "DEBUG" : level == Level::Error ? "ERROR" : "INFO");
        vfprintf(file, format, args);
        fputc('\n', file);
        fclose(file);
    }
};
