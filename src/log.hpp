// ArthurContracts - logging. File + OutputDebugString, always flushed:
// when the game dies, the log is the evidence of where it died.
#pragma once
#include <windows.h>
#include <cstdarg>
#include <cstdio>
#include <string>

namespace ac {
inline std::string LogPath() {
    char base[MAX_PATH];
    DWORD n = GetEnvironmentVariableA("LOCALAPPDATA", base, MAX_PATH);
    std::string dir = std::string(base, n) + "\\ArthurContracts";
    CreateDirectoryA(dir.c_str(), nullptr);
    return dir + "\\log.txt";
}

inline FILE* LogFile() {
    static FILE* f = [] {
        FILE* out = nullptr;
        fopen_s(&out, LogPath().c_str(), "a");
        if (out) setvbuf(out, nullptr, _IONBF, 0);
        return out;
    }();
    return f;
}

inline void Log(const char* fmt, ...) {
    char buf[2048];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    SYSTEMTIME t;
    GetLocalTime(&t);
    char line[2300];
    int n = snprintf(line, sizeof(line), "[%02d:%02d:%02d.%03d] %s\n",
                     t.wHour, t.wMinute, t.wSecond, t.wMilliseconds, buf);
    if (n <= 0) return;
    // CreateFile with full sharing: readers (the agent checking progress)
    // must not be locked out while the game runs. fopen's default sharing
    // made the log unreadable live.
    HANDLE h = CreateFileA(LogPath().c_str(), FILE_APPEND_DATA,
                           FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                           nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h != INVALID_HANDLE_VALUE) {
        DWORD written = 0;
        WriteFile(h, line, (DWORD)n, &written, nullptr);
        CloseHandle(h);
    }
    OutputDebugStringA(line);
}
}  // namespace ac

#define AC_LOG(...) ::ac::Log(__VA_ARGS__)
