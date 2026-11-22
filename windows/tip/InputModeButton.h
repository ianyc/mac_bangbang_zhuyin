#pragma once

// 工作列的「中／英」輸入模式圖示。Windows 8 以後，系統的輸入指示器會顯示文字服務註冊的
// GUID_LBI_INPUTMODE 語言列按鈕：左鍵點一下切換中英，右鍵顯示選單（版本號，同 Mac 版的輸入法選單）。
// 圖示分成淺色工作列用（黑字）與深色工作列用（白字），由 windows/scripts/make-icon.ps1 產生。

#include <windows.h>
#include <ctffunc.h>
#include <msctf.h>
#include <wrl/client.h>

#include <functional>

namespace bangbang {

class InputModeButton final : public ITfLangBarItemButton, public ITfSource {
public:
    using ClickHandler = std::function<void(TfLBIClick click, POINT point)>;

    explicit InputModeButton(ClickHandler onClick);

    /// TextService 停用時呼叫：之後點擊不再有作用，也不再通知系統。
    void detach();
    /// 更新中／英，並通知系統重畫圖示與說明文字。
    void setChinese(bool chinese);
    bool chinese() const { return chinese_; }

    // IUnknown
    STDMETHODIMP QueryInterface(REFIID riid, void** ppv) override;
    STDMETHODIMP_(ULONG) AddRef() override;
    STDMETHODIMP_(ULONG) Release() override;

    // ITfLangBarItem
    STDMETHODIMP GetInfo(TF_LANGBARITEMINFO* info) override;
    STDMETHODIMP GetStatus(DWORD* status) override;
    STDMETHODIMP Show(BOOL show) override;
    STDMETHODIMP GetTooltipString(BSTR* tooltip) override;

    // ITfLangBarItemButton
    STDMETHODIMP OnClick(TfLBIClick click, POINT point, const RECT* area) override;
    STDMETHODIMP InitMenu(ITfMenu* menu) override;
    STDMETHODIMP OnMenuSelect(UINT id) override;
    STDMETHODIMP GetIcon(HICON* icon) override;
    STDMETHODIMP GetText(BSTR* text) override;

    // ITfSource（系統用來接收 ITfLangBarItemSink）
    STDMETHODIMP AdviseSink(REFIID riid, IUnknown* sink, DWORD* cookie) override;
    STDMETHODIMP UnadviseSink(DWORD cookie) override;

private:
    ~InputModeButton();

    LONG references_ = 1;
    bool chinese_ = true;
    ClickHandler onClick_;
    Microsoft::WRL::ComPtr<ITfLangBarItemSink> sink_;
};

}  // namespace bangbang
