#include "InputModeButton.h"

#include <olectl.h>

#include <cwchar>

#include "Globals.h"
#include "Guids.h"
#include "resource.h"

namespace bangbang {

namespace {

/// 系統用來接收語言列通知的 sink 只有一個，cookie 固定。
constexpr DWORD kSinkCookie = 0x4D4F4445;  // 'MODE'

/// 工作列是淺色（Windows 的「預設 Windows 模式」）時用黑字圖示，深色時用白字。
bool taskbarIsLight() {
    DWORD light = 0;
    DWORD size = sizeof(light);
    if (RegGetValueW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize", L"SystemUsesLightTheme",
                     RRF_RT_REG_DWORD, nullptr, &light, &size) != ERROR_SUCCESS) {
        return false;  // Windows 11 預設是深色工作列
    }
    return light != 0;
}

}  // namespace

InputModeButton::InputModeButton(ClickHandler onClick) : onClick_(std::move(onClick)) {
    dllAddRef();
}

InputModeButton::~InputModeButton() {
    dllRelease();
}

void InputModeButton::detach() {
    onClick_ = nullptr;
    sink_.Reset();
}

void InputModeButton::setChinese(bool chinese) {
    if (chinese == chinese_) return;
    chinese_ = chinese;
    if (sink_) sink_->OnUpdate(TF_LBI_ICON | TF_LBI_TEXT | TF_LBI_TOOLTIP);
}

// MARK: - IUnknown

STDMETHODIMP InputModeButton::QueryInterface(REFIID riid, void** ppv) {
    if (!ppv) return E_POINTER;
    *ppv = nullptr;
    if (IsEqualIID(riid, IID_IUnknown) || IsEqualIID(riid, IID_ITfLangBarItem) || IsEqualIID(riid, IID_ITfLangBarItemButton)) {
        *ppv = static_cast<ITfLangBarItemButton*>(this);
    } else if (IsEqualIID(riid, IID_ITfSource)) {
        *ppv = static_cast<ITfSource*>(this);
    }
    if (!*ppv) return E_NOINTERFACE;
    AddRef();
    return S_OK;
}

STDMETHODIMP_(ULONG) InputModeButton::AddRef() {
    return static_cast<ULONG>(InterlockedIncrement(&references_));
}

STDMETHODIMP_(ULONG) InputModeButton::Release() {
    const LONG remaining = InterlockedDecrement(&references_);
    if (remaining == 0) delete this;
    return static_cast<ULONG>(remaining);
}

// MARK: - ITfLangBarItem

STDMETHODIMP InputModeButton::GetInfo(TF_LANGBARITEMINFO* info) {
    if (!info) return E_INVALIDARG;
    info->clsidService = kTextServiceClsid;
    info->guidItem = GUID_LBI_INPUTMODE;
    info->dwStyle = TF_LBI_STYLE_BTN_BUTTON | TF_LBI_STYLE_SHOWNINTRAY;
    info->ulSort = 0;
    wcsncpy_s(info->szDescription, L"棒棒注音輸入模式", _TRUNCATE);
    return S_OK;
}

STDMETHODIMP InputModeButton::GetStatus(DWORD* status) {
    if (!status) return E_INVALIDARG;
    *status = 0;  // 可以點、沒有停用
    return S_OK;
}

STDMETHODIMP InputModeButton::Show(BOOL) {
    return E_NOTIMPL;
}

STDMETHODIMP InputModeButton::GetTooltipString(BSTR* tooltip) {
    if (!tooltip) return E_INVALIDARG;
    *tooltip = SysAllocString(chinese_ ? L"棒棒注音：中文（按 Shift 切換成英文）" : L"棒棒注音：英文（按 Shift 切換成中文）");
    return *tooltip ? S_OK : E_OUTOFMEMORY;
}

// MARK: - ITfLangBarItemButton

STDMETHODIMP InputModeButton::OnClick(TfLBIClick click, POINT point, const RECT*) {
    if (!onClick_) return S_OK;
    const ClickHandler handler = onClick_;  // 處理途中可能被停用
    return guarded([&] {
        handler(click, point);
        return S_OK;
    });
}

STDMETHODIMP InputModeButton::InitMenu(ITfMenu*) {
    return E_NOTIMPL;  // 一般按鈕，不是選單按鈕
}

STDMETHODIMP InputModeButton::OnMenuSelect(UINT) {
    return E_NOTIMPL;
}

STDMETHODIMP InputModeButton::GetIcon(HICON* icon) {
    if (!icon) return E_INVALIDARG;
    const bool light = taskbarIsLight();
    const int id = chinese_ ? (light ? IDI_MODE_ZH_LIGHT : IDI_MODE_ZH_DARK) : (light ? IDI_MODE_EN_LIGHT : IDI_MODE_EN_DARK);
    const int pixels = GetSystemMetricsForDpi(SM_CXSMICON, GetDpiForSystem());
    // 系統用完會自己 DestroyIcon，所以每次都載入一份新的。
    *icon = static_cast<HICON>(LoadImageW(g_module, MAKEINTRESOURCEW(id), IMAGE_ICON, pixels, pixels, LR_DEFAULTCOLOR));
    return *icon ? S_OK : E_FAIL;
}

STDMETHODIMP InputModeButton::GetText(BSTR* text) {
    if (!text) return E_INVALIDARG;
    *text = SysAllocString(chinese_ ? L"中" : L"英");
    return *text ? S_OK : E_OUTOFMEMORY;
}

// MARK: - ITfSource

STDMETHODIMP InputModeButton::AdviseSink(REFIID riid, IUnknown* sink, DWORD* cookie) {
    if (!sink || !cookie) return E_INVALIDARG;
    if (!IsEqualIID(riid, IID_ITfLangBarItemSink)) return CONNECT_E_CANNOTCONNECT;
    if (sink_) return CONNECT_E_ADVISELIMIT;
    if (FAILED(sink->QueryInterface(IID_PPV_ARGS(&sink_)))) return E_NOINTERFACE;
    *cookie = kSinkCookie;
    return S_OK;
}

STDMETHODIMP InputModeButton::UnadviseSink(DWORD cookie) {
    if (cookie != kSinkCookie || !sink_) return CONNECT_E_NOCONNECTION;
    sink_.Reset();
    return S_OK;
}

}  // namespace bangbang
