// 棒棒注音 TSF 輸入法 DLL 的進入點（in-process COM 伺服器）。
#include <windows.h>

#include "CandidateWindow.h"
#include "ClassFactory.h"
#include "Globals.h"
#include "Guids.h"
#include "Register.h"

BOOL APIENTRY DllMain(HINSTANCE instance, DWORD reason, LPVOID reserved) {
    if (reason == DLL_PROCESS_ATTACH) {
        bangbang::g_module = instance;
        DisableThreadLibraryCalls(instance);
    } else if (reason == DLL_PROCESS_DETACH && reserved == nullptr) {
        // 被 FreeLibrary 卸載（不是整個程式結束）：候選窗類別的視窗程序在這個 DLL 裡，不能留下來。
        bangbang::CandidateWindow::unregisterClass();
    }
    return TRUE;
}

STDAPI DllGetClassObject(REFCLSID clsid, REFIID riid, LPVOID* ppv) {
    if (!ppv) return E_POINTER;
    *ppv = nullptr;
    if (!IsEqualCLSID(clsid, bangbang::kTextServiceClsid)) return CLASS_E_CLASSNOTAVAILABLE;
    return bangbang::guarded([&] {
        auto* factory = new bangbang::ClassFactory();
        const HRESULT hr = factory->QueryInterface(riid, ppv);
        factory->Release();
        return hr;
    });
}

STDAPI DllCanUnloadNow() {
    return bangbang::dllCanUnload() ? S_OK : S_FALSE;
}

STDAPI DllRegisterServer() {
    return bangbang::guarded([] { return bangbang::registerAll(); });
}

STDAPI DllUnregisterServer() {
    return bangbang::guarded([] {
        bangbang::unregisterAll();
        return S_OK;
    });
}
