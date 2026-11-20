// 棒棒注音 TSF 輸入法 DLL 的進入點。
// 第 0 階段只有骨架：COM 匯出函式都還沒有實作，第 3 階段再補上文字服務與註冊。
#include <windows.h>

#include "Guids.h"

namespace {
HINSTANCE g_module = nullptr;
LONG g_lockCount = 0;  // 第 3 階段由 class factory 與物件計數使用
}  // namespace

BOOL APIENTRY DllMain(HINSTANCE instance, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        g_module = instance;
        DisableThreadLibraryCalls(instance);
    }
    return TRUE;
}

STDAPI DllGetClassObject(REFCLSID, REFIID, LPVOID* ppv) {
    if (ppv) *ppv = nullptr;
    return CLASS_E_CLASSNOTAVAILABLE;
}

STDAPI DllCanUnloadNow() {
    return g_lockCount == 0 ? S_OK : S_FALSE;
}

STDAPI DllRegisterServer() {
    return E_NOTIMPL;
}

STDAPI DllUnregisterServer() {
    return E_NOTIMPL;
}
