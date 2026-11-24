// MSI 安裝檔的自訂動作（由 windows/installer/BangbangZhuyin.wxs 呼叫）。
// 鍵盤清單是每個使用者自己的設定（HKCU），所以要以使用者的身分執行：安裝完成後（InstallFinalize 之後）
// 與開始解除安裝前（InstallInitialize 之前）的立即自訂動作。註冊 DLL 則由 regsvr32 以系統身分處理。
#include <windows.h>
#include <msi.h>
#include <msiquery.h>

namespace {

// 與 windows/tip/Guids.h 相同：語言 0x0404（中文，台灣）＋文字服務的 CLSID＋語言設定檔的 GUID。
constexpr wchar_t kTip[] = L"0x0404:{37DA0F04-7924-465F-8DA3-C8EEE7D3BFD5}{0318F0A4-F04F-4C79-B66B-4CF72A7666ED}";
constexpr DWORD kUninstall = 0x00000001;  // ILOT_UNINSTALL

/// 寫進 MSI 的記錄檔（msiexec /l*v）。
void log(MSIHANDLE install, const wchar_t* text) {
    PMSIHANDLE record = MsiCreateRecord(0);
    MsiRecordSetStringW(record, 0, text);
    MsiProcessMessage(install, INSTALLMESSAGE_INFO, record);
}

/// input.dll 的 InstallLayoutOrTip：把輸入法加進（或移出）目前使用者的鍵盤清單。
bool installLayoutOrTip(DWORD flags) {
    using Function = BOOL(WINAPI*)(LPCWSTR, DWORD);
    const HMODULE input = LoadLibraryExW(L"input.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
    if (!input) return false;
    const auto function = reinterpret_cast<Function>(GetProcAddress(input, "InstallLayoutOrTip"));
    const bool ok = function && function(kTip, flags);
    FreeLibrary(input);
    return ok;
}

}  // namespace

// 失敗也不讓安裝失敗：使用者可以到「設定 → 時間與語言 → 語言與地區」手動加入。
extern "C" UINT __stdcall AddToKeyboardList(MSIHANDLE install) {
    log(install, installLayoutOrTip(0) ? L"棒棒注音：已加進鍵盤清單" : L"棒棒注音：無法自動加進鍵盤清單");
    return ERROR_SUCCESS;
}

extern "C" UINT __stdcall RemoveFromKeyboardList(MSIHANDLE install) {
    log(install, installLayoutOrTip(kUninstall) ? L"棒棒注音：已從鍵盤清單移除" : L"棒棒注音：無法從鍵盤清單移除");
    return ERROR_SUCCESS;
}
