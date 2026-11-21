#include "Globals.h"

#include <cstdarg>
#include <cwchar>

namespace bangbang {

HINSTANCE g_module = nullptr;

namespace {
LONG g_references = 0;
}

void dllAddRef() {
    InterlockedIncrement(&g_references);
}

void dllRelease() {
    InterlockedDecrement(&g_references);
}

bool dllCanUnload() {
    return InterlockedCompareExchange(&g_references, 0, 0) == 0;
}

std::wstring modulePath() {
    std::wstring path(MAX_PATH, L'\0');
    for (;;) {
        const DWORD n = GetModuleFileNameW(g_module, path.data(), static_cast<DWORD>(path.size()));
        if (n == 0) return {};
        if (n < path.size()) {
            path.resize(n);
            return path;
        }
        path.resize(path.size() * 2);
    }
}

std::wstring moduleDirectory() {
    const std::wstring path = modulePath();
    const auto slash = path.find_last_of(L"\\/");
    return slash == std::wstring::npos ? std::wstring() : path.substr(0, slash);
}

void debugLog(const wchar_t* format, ...) {
    wchar_t buffer[1024];
    va_list args;
    va_start(args, format);
    _vsnwprintf_s(buffer, _countof(buffer), _TRUNCATE, format, args);
    va_end(args);
    OutputDebugStringW(buffer);
    OutputDebugStringW(L"\n");
}

}  // namespace bangbang
