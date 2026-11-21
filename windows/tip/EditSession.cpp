#include "EditSession.h"

#include "Globals.h"

namespace bangbang {

EditSession::EditSession(std::function<HRESULT(TfEditCookie)> body) : body_(std::move(body)) {
    dllAddRef();
}

EditSession::~EditSession() {
    dllRelease();
}

STDMETHODIMP EditSession::QueryInterface(REFIID riid, void** ppv) {
    if (!ppv) return E_POINTER;
    if (IsEqualIID(riid, IID_IUnknown) || IsEqualIID(riid, IID_ITfEditSession)) {
        *ppv = static_cast<ITfEditSession*>(this);
        AddRef();
        return S_OK;
    }
    *ppv = nullptr;
    return E_NOINTERFACE;
}

STDMETHODIMP_(ULONG) EditSession::AddRef() {
    return static_cast<ULONG>(InterlockedIncrement(&references_));
}

STDMETHODIMP_(ULONG) EditSession::Release() {
    const LONG remaining = InterlockedDecrement(&references_);
    if (remaining == 0) delete this;
    return static_cast<ULONG>(remaining);
}

STDMETHODIMP EditSession::DoEditSession(TfEditCookie ec) {
    return guarded([&] { return body_(ec); });
}

HRESULT requestEditSession(ITfContext* context, TfClientId clientId, DWORD flags, std::function<HRESULT(TfEditCookie)> body) {
    if (!context) return E_INVALIDARG;
    return guarded([&] {
        auto* session = new EditSession(std::move(body));
        HRESULT sessionResult = S_OK;
        const HRESULT hr = context->RequestEditSession(clientId, session, flags, &sessionResult);
        session->Release();
        return FAILED(hr) ? hr : sessionResult;
    });
}

}  // namespace bangbang
