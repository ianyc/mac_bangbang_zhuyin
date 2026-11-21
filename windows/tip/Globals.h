#pragma once

#include <windows.h>

#include <string>

namespace bangbang {

/// 這個 DLL 的模組代號（DllMain 設定）。
extern HINSTANCE g_module;

/// 存活的 COM 物件與 LockServer 的計數，DllCanUnloadNow 用。
void dllAddRef();
void dllRelease();
bool dllCanUnload();

/// 這個 DLL 的完整路徑與所在資料夾。
std::wstring modulePath();
std::wstring moduleDirectory();

/// 除錯訊息：用 DebugView 或 Visual Studio 的「輸出」視窗看。
void debugLog(const wchar_t* format, ...);

/// 執行 COM 進入點時攔下所有例外：例外穿過 COM 邊界會讓宿主程式當掉。
template <class F>
HRESULT guarded(F&& body) noexcept {
    try {
        return body();
    } catch (...) {
        debugLog(L"棒棒注音：攔下未預期的例外");
        return E_UNEXPECTED;
    }
}

}  // namespace bangbang
