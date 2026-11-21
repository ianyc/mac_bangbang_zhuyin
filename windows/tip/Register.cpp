#include "Register.h"

#include <msctf.h>
#include <wrl/client.h>

#include <string>

#include "Globals.h"
#include "Guids.h"
#include "resource.h"

using Microsoft::WRL::ComPtr;

namespace bangbang {

namespace {

/// 一定會註冊的 TSF 類別（windows\scripts\register.ps1 的 $expectedCategories 要保持一致）。
/// - IMMERSIVESUPPORT：Store 程式使用輸入法的條件。
/// - UIELEMENTENABLED：支援 UI-less 模式。宣告後，顯示候選窗等 UI 前要先問 ITfUIElementMgr（第 4 階段）。
/// - INPUTMODECOMPARTMENT：用 GUID_COMPARTMENT_KEYBOARD_INPUTMODE_CONVERSION 回報中／英模式。
const GUID* const kCategories[] = {
    &GUID_TFCAT_TIP_KEYBOARD,
    &GUID_TFCAT_DISPLAYATTRIBUTEPROVIDER,
    &GUID_TFCAT_TIPCAP_SYSTRAYSUPPORT,
    &GUID_TFCAT_TIPCAP_COMLESS,
    &GUID_TFCAT_TIPCAP_IMMERSIVESUPPORT,
    &GUID_TFCAT_TIPCAP_UIELEMENTENABLED,
    &GUID_TFCAT_TIPCAP_INPUTMODECOMPARTMENT,
};

/// 取消註冊時，連同之後階段可能加上的類別一起清掉。
const GUID* const kAllCategories[] = {
    &GUID_TFCAT_TIP_KEYBOARD,
    &GUID_TFCAT_DISPLAYATTRIBUTEPROVIDER,
    &GUID_TFCAT_TIPCAP_SYSTRAYSUPPORT,
    &GUID_TFCAT_TIPCAP_COMLESS,
    &GUID_TFCAT_TIPCAP_IMMERSIVESUPPORT,
    &GUID_TFCAT_TIPCAP_INPUTMODECOMPARTMENT,
    &GUID_TFCAT_TIPCAP_UIELEMENTENABLED,
    &GUID_TFCAT_TIPCAP_SECUREMODE,
};

/// 環境變數展開；沒有這個變數時回傳空字串。
std::wstring expandEnvironment(const wchar_t* text) {
    wchar_t buffer[MAX_PATH] = {};
    const DWORD n = ExpandEnvironmentStringsW(text, buffer, MAX_PATH);
    if (n == 0 || n > MAX_PATH) return {};
    const std::wstring expanded = buffer;
    return expanded.find(L'%') == std::wstring::npos ? expanded : std::wstring();
}

/// DLL 是否放在一般使用者改不了的位置（Program Files、Windows 資料夾）。
/// 32 位元程式的 %ProgramFiles% 是 Program Files (x86)，所以另外查 %ProgramW6432%（64 位元的 Program Files）。
bool isProtectedLocation(const std::wstring& path) {
    for (const wchar_t* root : {L"%ProgramW6432%", L"%ProgramFiles%", L"%ProgramFiles(x86)%", L"%SystemRoot%"}) {
        std::wstring prefix = expandEnvironment(root);
        if (prefix.empty()) continue;
        if (prefix.back() != L'\\') prefix += L'\\';
        if (path.size() > prefix.size() &&
            CompareStringOrdinal(path.c_str(), static_cast<int>(prefix.size()), prefix.c_str(), static_cast<int>(prefix.size()), TRUE) ==
                CSTR_EQUAL) {
            return true;
        }
    }
    return false;
}

std::wstring guidString(const GUID& guid) {
    wchar_t buffer[64] = {};
    StringFromGUID2(guid, buffer, static_cast<int>(_countof(buffer)));
    return buffer;
}

std::wstring clsidKey() {
    return L"Software\\Classes\\CLSID\\" + guidString(kTextServiceClsid);
}

HRESULT setString(HKEY key, const wchar_t* name, const std::wstring& value) {
    const LSTATUS status = RegSetValueExW(key, name, 0, REG_SZ, reinterpret_cast<const BYTE*>(value.c_str()),
                                          static_cast<DWORD>((value.size() + 1) * sizeof(wchar_t)));
    return HRESULT_FROM_WIN32(status);
}

/// HKLM\Software\Classes\CLSID\{CLSID}\InprocServer32 = 這個 DLL（ThreadingModel = Apartment）。
/// 32 位元的 regsvr32 會自動寫到 WOW6432Node 底下。
HRESULT registerServer() {
    HKEY key = nullptr;
    LSTATUS status = RegCreateKeyExW(HKEY_LOCAL_MACHINE, clsidKey().c_str(), 0, nullptr, REG_OPTION_NON_VOLATILE, KEY_WRITE,
                                     nullptr, &key, nullptr);
    if (status != ERROR_SUCCESS) return HRESULT_FROM_WIN32(status);
    HRESULT hr = setString(key, nullptr, kDisplayName);
    HKEY inproc = nullptr;
    if (SUCCEEDED(hr)) {
        status = RegCreateKeyExW(key, L"InprocServer32", 0, nullptr, REG_OPTION_NON_VOLATILE, KEY_WRITE, nullptr, &inproc, nullptr);
        hr = HRESULT_FROM_WIN32(status);
    }
    if (SUCCEEDED(hr)) hr = setString(inproc, nullptr, modulePath());
    if (SUCCEEDED(hr)) hr = setString(inproc, L"ThreadingModel", L"Apartment");
    if (inproc) RegCloseKey(inproc);
    RegCloseKey(key);
    return hr;
}

void unregisterServer() {
    RegDeleteTreeW(HKEY_LOCAL_MACHINE, clsidKey().c_str());
}

HRESULT registerProfile() {
    ComPtr<ITfInputProcessorProfileMgr> profiles;
    HRESULT hr = CoCreateInstance(CLSID_TF_InputProcessorProfiles, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&profiles));
    if (FAILED(hr)) return hr;
    const std::wstring path = modulePath();
    const std::wstring name = kDisplayName;
    return profiles->RegisterProfile(kTextServiceClsid, kLangId, kProfileGuid, name.c_str(), static_cast<ULONG>(name.size()),
                                     path.c_str(), static_cast<ULONG>(path.size()), static_cast<ULONG>(-IDI_BANGBANG), nullptr, 0,
                                     TRUE, 0);
}

void unregisterProfile() {
    ComPtr<ITfInputProcessorProfileMgr> profiles;
    if (SUCCEEDED(CoCreateInstance(CLSID_TF_InputProcessorProfiles, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&profiles)))) {
        profiles->UnregisterProfile(kTextServiceClsid, kLangId, kProfileGuid, 0);
    }
}

HRESULT registerCategories() {
    ComPtr<ITfCategoryMgr> categories;
    HRESULT hr = CoCreateInstance(CLSID_TF_CategoryMgr, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&categories));
    if (FAILED(hr)) return hr;
    for (const GUID* category : kCategories) {
        hr = categories->RegisterCategory(kTextServiceClsid, *category, kTextServiceClsid);
        if (FAILED(hr)) return hr;
    }
    // SECUREMODE：允許在安全模式（登入畫面這類以系統身分執行的地方）載入。DLL 放在使用者自己改得動的資料夾
    // （開發版）時不能註冊，否則任何以使用者身分執行的程式都能換掉 DLL，讓自己的程式碼以系統身分執行。
    if (isProtectedLocation(modulePath())) {
        hr = categories->RegisterCategory(kTextServiceClsid, GUID_TFCAT_TIPCAP_SECUREMODE, kTextServiceClsid);
        if (FAILED(hr)) return hr;
    } else {
        categories->UnregisterCategory(kTextServiceClsid, GUID_TFCAT_TIPCAP_SECUREMODE, kTextServiceClsid);
    }
    return S_OK;
}

void unregisterCategories() {
    ComPtr<ITfCategoryMgr> categories;
    if (FAILED(CoCreateInstance(CLSID_TF_CategoryMgr, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&categories)))) return;
    for (const GUID* category : kAllCategories) categories->UnregisterCategory(kTextServiceClsid, *category, kTextServiceClsid);
}

/// regsvr32 會先初始化 COM；其他呼叫者可能沒有，所以需要時自己初始化。
class ComScope {
public:
    ComScope() : hr_(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED)) {}
    ~ComScope() {
        if (SUCCEEDED(hr_)) CoUninitialize();
    }
    ComScope(const ComScope&) = delete;
    ComScope& operator=(const ComScope&) = delete;

private:
    HRESULT hr_;
};

}  // namespace

HRESULT registerAll() {
    const ComScope com;
    HRESULT hr = registerServer();
    if (SUCCEEDED(hr)) hr = registerProfile();
    if (SUCCEEDED(hr)) hr = registerCategories();
    if (FAILED(hr)) {
        debugLog(L"棒棒注音：註冊失敗 0x%08X", static_cast<unsigned>(hr));
        unregisterAll();
    }
    return hr;
}

void unregisterAll() {
    const ComScope com;
    unregisterCategories();
    unregisterProfile();
    unregisterServer();
}

}  // namespace bangbang
