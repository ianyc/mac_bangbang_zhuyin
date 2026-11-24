#include "Globals.h"

#include <objbase.h>

#include <cstdarg>
#include <cwchar>
#include <mutex>

namespace bangbang {

HINSTANCE g_module = nullptr;

namespace {

LONG g_references = 0;

std::once_flag g_logOnce;
HANDLE g_logFile = INVALID_HANDLE_VALUE;  // 程序結束前都開著（只有開了偵錯記錄時才有）

void openLogFile() {
    wchar_t temp[MAX_PATH + 1];
    const DWORD length = GetTempPathW(MAX_PATH + 1, temp);
    if (length == 0 || length > MAX_PATH) return;
    const std::wstring folder(temp, length);
    if (GetFileAttributesW((folder + L"bangbang-debug.flag").c_str()) == INVALID_FILE_ATTRIBUTES) return;
    g_logFile = CreateFileW((folder + L"bangbang-debug.log").c_str(), FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                            nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
}

bool logFileOpen() {
    std::call_once(g_logOnce, openLogFile);
    return g_logFile != INVALID_HANDLE_VALUE;
}

/// 一行一筆：時間、[程序:執行緒]、訊息，UTF-8。
void writeLogFile(const wchar_t* message) {
    if (!logFileOpen()) return;
    SYSTEMTIME now;
    GetLocalTime(&now);
    wchar_t line[1100];
    _snwprintf_s(line, _countof(line), _TRUNCATE, L"%02u:%02u:%02u.%03u [%lu:%lu] %ls\r\n", now.wHour, now.wMinute, now.wSecond,
                 now.wMilliseconds, GetCurrentProcessId(), GetCurrentThreadId(), message);
    char bytes[3400];
    const int size = WideCharToMultiByte(CP_UTF8, 0, line, -1, bytes, sizeof(bytes), nullptr, nullptr);
    DWORD written = 0;
    if (size > 1) WriteFile(g_logFile, bytes, static_cast<DWORD>(size - 1), &written, nullptr);  // FILE_APPEND_DATA：多個執行緒也不會互蓋
}

}  // namespace

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
    writeLogFile(buffer);
}

void traceLog(const wchar_t* format, ...) {
    if (!logFileOpen()) return;  // 沒開偵錯記錄：連格式化都省掉
    wchar_t buffer[1024];
    va_list args;
    va_start(args, format);
    _vsnwprintf_s(buffer, _countof(buffer), _TRUNCATE, format, args);
    va_end(args);
    writeLogFile(buffer);
}

std::wstring guidText(const GUID& guid) {
    wchar_t text[40] = {};
    return StringFromGUID2(guid, text, static_cast<int>(_countof(text))) ? std::wstring(text) : std::wstring(L"{?}");
}

}  // namespace bangbang
