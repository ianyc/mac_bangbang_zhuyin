#pragma once

#include <windows.h>
#include <unknwn.h>

namespace bangbang {

/// 建立 TextService 的 COM 類別工廠。
class ClassFactory final : public IClassFactory {
public:
    ClassFactory();

    // IUnknown
    STDMETHODIMP QueryInterface(REFIID riid, void** ppv) override;
    STDMETHODIMP_(ULONG) AddRef() override;
    STDMETHODIMP_(ULONG) Release() override;

    // IClassFactory
    STDMETHODIMP CreateInstance(IUnknown* outer, REFIID riid, void** ppv) override;
    STDMETHODIMP LockServer(BOOL lock) override;

private:
    ~ClassFactory();
    LONG references_ = 1;
};

}  // namespace bangbang
