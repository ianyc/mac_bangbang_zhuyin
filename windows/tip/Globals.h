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

/// 除錯訊息：用 DebugView 或 Visual Studio 的「輸出」視窗看；開了偵錯記錄檔（見 traceLog）時也寫進記錄檔。
void debugLog(const wchar_t* format, ...);

/// 偵錯記錄檔：程序的暫存資料夾（GetTempPath）裡有 bangbang-debug.flag 時，寫進同一個資料夾的
/// bangbang-debug.log；沒有這個檔案時什麼都不做（程序啟動後第一次記錄時檢查一次）。
/// 沙盒程式（例如工作列搜尋）的暫存資料夾在它自己的套件資料夾（%LOCALAPPDATA%\Packages\<套件>\AC\Temp），
/// 在那裡看不到 OutputDebugString，就用這個查問題。
void traceLog(const wchar_t* format, ...);

/// GUID 的文字（{XXXXXXXX-…}），記錄用。
std::wstring guidText(const GUID& guid);

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
