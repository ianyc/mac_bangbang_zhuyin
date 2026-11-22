#include "CandidateListUI.h"

#include <algorithm>

#include "Globals.h"
#include "Guids.h"

namespace bangbang {

CandidateListUI::CandidateListUI(CandidateListHost* host) : host_(host) {
    dllAddRef();
}

CandidateListUI::~CandidateListUI() {
    dllRelease();
}

bool CandidateListUI::snapshot(std::vector<std::wstring>& items, int& highlighted, int& perPage) {
    highlighted = 0;
    perPage = 9;
    return host_ && host_->candidateSnapshot(items, highlighted, perPage);
}

// MARK: - IUnknown

STDMETHODIMP CandidateListUI::QueryInterface(REFIID riid, void** ppv) {
    if (!ppv) return E_POINTER;
    *ppv = nullptr;
    if (IsEqualIID(riid, IID_IUnknown) || IsEqualIID(riid, IID_ITfUIElement) || IsEqualIID(riid, IID_ITfCandidateListUIElement) ||
        IsEqualIID(riid, IID_ITfCandidateListUIElementBehavior)) {
        *ppv = static_cast<ITfCandidateListUIElementBehavior*>(this);
        AddRef();
        return S_OK;
    }
    return E_NOINTERFACE;
}

STDMETHODIMP_(ULONG) CandidateListUI::AddRef() {
    return static_cast<ULONG>(InterlockedIncrement(&references_));
}

STDMETHODIMP_(ULONG) CandidateListUI::Release() {
    const LONG remaining = InterlockedDecrement(&references_);
    if (remaining == 0) delete this;
    return static_cast<ULONG>(remaining);
}

// MARK: - ITfUIElement

STDMETHODIMP CandidateListUI::GetDescription(BSTR* description) {
    if (!description) return E_INVALIDARG;
    *description = SysAllocString(L"棒棒注音候選字");
    return *description ? S_OK : E_OUTOFMEMORY;
}

STDMETHODIMP CandidateListUI::GetGUID(GUID* guid) {
    if (!guid) return E_INVALIDARG;
    *guid = kCandidateUIElementGuid;
    return S_OK;
}

STDMETHODIMP CandidateListUI::Show(BOOL show) {
    if (!host_) return E_FAIL;
    return guarded([&] {
        host_->candidateShowRequested(show != FALSE);
        return S_OK;
    });
}

STDMETHODIMP CandidateListUI::IsShown(BOOL* shown) {
    if (!shown) return E_INVALIDARG;
    *shown = host_ && host_->candidateWindowShown() ? TRUE : FALSE;
    return S_OK;
}

// MARK: - ITfCandidateListUIElement

STDMETHODIMP CandidateListUI::GetUpdatedFlags(DWORD* flags) {
    if (!flags) return E_INVALIDARG;
    *flags = updatedFlags_;
    updatedFlags_ = 0;
    return S_OK;
}

STDMETHODIMP CandidateListUI::GetDocumentMgr(ITfDocumentMgr** documentMgr) {
    if (!documentMgr) return E_INVALIDARG;
    *documentMgr = nullptr;
    if (!host_) return E_FAIL;
    return guarded([&] { return host_->candidateDocumentMgr(documentMgr); });
}

STDMETHODIMP CandidateListUI::GetCount(UINT* count) {
    if (!count) return E_INVALIDARG;
    *count = 0;
    return guarded([&] {
        std::vector<std::wstring> items;
        int highlighted = 0, perPage = 9;
        if (!snapshot(items, highlighted, perPage)) return E_FAIL;
        *count = static_cast<UINT>(items.size());
        return S_OK;
    });
}

STDMETHODIMP CandidateListUI::GetSelection(UINT* index) {
    if (!index) return E_INVALIDARG;
    *index = 0;
    return guarded([&] {
        std::vector<std::wstring> items;
        int highlighted = 0, perPage = 9;
        if (!snapshot(items, highlighted, perPage)) return E_FAIL;
        *index = static_cast<UINT>(highlighted);
        return S_OK;
    });
}

STDMETHODIMP CandidateListUI::GetString(UINT index, BSTR* text) {
    if (!text) return E_INVALIDARG;
    *text = nullptr;
    return guarded([&] {
        std::vector<std::wstring> items;
        int highlighted = 0, perPage = 9;
        if (!snapshot(items, highlighted, perPage)) return E_FAIL;
        if (index >= items.size()) return E_INVALIDARG;
        *text = SysAllocStringLen(items[index].c_str(), static_cast<UINT>(items[index].size()));
        return *text ? S_OK : E_OUTOFMEMORY;
    });
}

STDMETHODIMP CandidateListUI::GetPageIndex(UINT* index, UINT size, UINT* pageCount) {
    if (!pageCount) return E_INVALIDARG;
    *pageCount = 0;
    return guarded([&] {
        std::vector<std::wstring> items;
        int highlighted = 0, perPage = 9;
        if (!snapshot(items, highlighted, perPage)) return E_FAIL;
        const UINT per = static_cast<UINT>(std::max(1, perPage));
        const UINT pages = std::max<UINT>(1, (static_cast<UINT>(items.size()) + per - 1) / per);
        *pageCount = pages;
        if (index) {
            for (UINT i = 0; i < std::min(size, pages); ++i) index[i] = i * per;  // 每頁第一個候選的位置
        }
        return S_OK;
    });
}

STDMETHODIMP CandidateListUI::SetPageIndex(UINT*, UINT) {
    return E_NOTIMPL;  // 分頁固定每頁 9 個（同 Mac 版）
}

STDMETHODIMP CandidateListUI::GetCurrentPage(UINT* page) {
    if (!page) return E_INVALIDARG;
    *page = 0;
    return guarded([&] {
        std::vector<std::wstring> items;
        int highlighted = 0, perPage = 9;
        if (!snapshot(items, highlighted, perPage)) return E_FAIL;
        *page = static_cast<UINT>(highlighted / std::max(1, perPage));
        return S_OK;
    });
}

// MARK: - ITfCandidateListUIElementBehavior

STDMETHODIMP CandidateListUI::SetSelection(UINT index) {
    if (!host_) return E_FAIL;
    return guarded([&] { return host_->candidateHighlight(static_cast<int>(index)); });
}

STDMETHODIMP CandidateListUI::Finalize() {
    if (!host_) return E_FAIL;
    return guarded([&] { return host_->candidateFinalize(); });
}

STDMETHODIMP CandidateListUI::Abort() {
    if (!host_) return E_FAIL;
    return guarded([&] { return host_->candidateAbort(); });
}

}  // namespace bangbang
