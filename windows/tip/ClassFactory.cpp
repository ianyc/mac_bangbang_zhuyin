#include "ClassFactory.h"

#include "Globals.h"
#include "TextService.h"

namespace bangbang {

ClassFactory::ClassFactory() {
    dllAddRef();
}

ClassFactory::~ClassFactory() {
    dllRelease();
}

STDMETHODIMP ClassFactory::QueryInterface(REFIID riid, void** ppv) {
    if (!ppv) return E_POINTER;
    if (IsEqualIID(riid, IID_IUnknown) || IsEqualIID(riid, IID_IClassFactory)) {
        *ppv = static_cast<IClassFactory*>(this);
        AddRef();
        return S_OK;
    }
    *ppv = nullptr;
    return E_NOINTERFACE;
}

STDMETHODIMP_(ULONG) ClassFactory::AddRef() {
    return static_cast<ULONG>(InterlockedIncrement(&references_));
}

STDMETHODIMP_(ULONG) ClassFactory::Release() {
    const LONG remaining = InterlockedDecrement(&references_);
    if (remaining == 0) delete this;
    return static_cast<ULONG>(remaining);
}

STDMETHODIMP ClassFactory::CreateInstance(IUnknown* outer, REFIID riid, void** ppv) {
    if (!ppv) return E_POINTER;
    *ppv = nullptr;
    if (outer) return CLASS_E_NOAGGREGATION;
    return guarded([&] {
        auto* service = new TextService();
        const HRESULT hr = service->QueryInterface(riid, ppv);
        service->Release();
        return hr;
    });
}

STDMETHODIMP ClassFactory::LockServer(BOOL lock) {
    if (lock) {
        dllAddRef();
    } else {
        dllRelease();
    }
    return S_OK;
}

}  // namespace bangbang
