#pragma once

// 候選字的 UI 元件，給 UI-less 模式用：程式（例如全螢幕遊戲）在 ITfUIElementSink::BeginUIElement
// 回答「自己畫」時，我們不顯示候選窗，改由程式透過這個介面讀取候選字、反白與分頁，
// 也可以透過 ITfCandidateListUIElementBehavior 移動反白、選字或關閉。
// 程式回答「請輸入法自己畫」時，這個元件照樣存在（ITfUIElementMgr 會列出它）。

#include <windows.h>
#include <msctf.h>

#include <string>
#include <vector>

namespace bangbang {

/// CandidateListUI 透過這個介面向 TextService 查詢與操作候選字。
class CandidateListHost {
public:
    virtual ~CandidateListHost() = default;
    /// 目前的候選字（顯示文字）、反白位置與每頁幾個；候選窗沒開時回傳 false。
    virtual bool candidateSnapshot(std::vector<std::wstring>& items, int& highlighted, int& perPage) = 0;
    virtual HRESULT candidateDocumentMgr(ITfDocumentMgr** documentMgr) = 0;
    /// 程式要求顯示或隱藏我們自己的候選窗（ITfUIElement::Show）。
    virtual void candidateShowRequested(bool show) = 0;
    virtual bool candidateWindowShown() = 0;
    virtual HRESULT candidateHighlight(int index) = 0;
    virtual HRESULT candidateFinalize() = 0;
    virtual HRESULT candidateAbort() = 0;
};

class CandidateListUI final : public ITfCandidateListUIElementBehavior {
public:
    explicit CandidateListUI(CandidateListHost* host);

    /// TextService 停用時呼叫：之後的呼叫都回傳 E_FAIL（程式可能還拿著這個物件）。
    void detach() { host_ = nullptr; }
    /// 下一次 GetUpdatedFlags 要回報的變更（TF_CLUIE_*）。
    void markUpdated(DWORD flags) { updatedFlags_ |= flags; }

    // IUnknown
    STDMETHODIMP QueryInterface(REFIID riid, void** ppv) override;
    STDMETHODIMP_(ULONG) AddRef() override;
    STDMETHODIMP_(ULONG) Release() override;

    // ITfUIElement
    STDMETHODIMP GetDescription(BSTR* description) override;
    STDMETHODIMP GetGUID(GUID* guid) override;
    STDMETHODIMP Show(BOOL show) override;
    STDMETHODIMP IsShown(BOOL* shown) override;

    // ITfCandidateListUIElement
    STDMETHODIMP GetUpdatedFlags(DWORD* flags) override;
    STDMETHODIMP GetDocumentMgr(ITfDocumentMgr** documentMgr) override;
    STDMETHODIMP GetCount(UINT* count) override;
    STDMETHODIMP GetSelection(UINT* index) override;
    STDMETHODIMP GetString(UINT index, BSTR* text) override;
    STDMETHODIMP GetPageIndex(UINT* index, UINT size, UINT* pageCount) override;
    STDMETHODIMP SetPageIndex(UINT* index, UINT pageCount) override;
    STDMETHODIMP GetCurrentPage(UINT* page) override;

    // ITfCandidateListUIElementBehavior
    STDMETHODIMP SetSelection(UINT index) override;
    STDMETHODIMP Finalize() override;
    STDMETHODIMP Abort() override;

private:
    ~CandidateListUI();
    bool snapshot(std::vector<std::wstring>& items, int& highlighted, int& perPage);

    LONG references_ = 1;
    CandidateListHost* host_;
    DWORD updatedFlags_ = 0;
};

}  // namespace bangbang
