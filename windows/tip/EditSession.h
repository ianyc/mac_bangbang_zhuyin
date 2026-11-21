#pragma once

#include <windows.h>
#include <msctf.h>

#include <functional>

namespace bangbang {

/// TSF 只允許在 edit session 裡讀寫文件；這個類別把一段程式包成 ITfEditSession。
class EditSession final : public ITfEditSession {
public:
    explicit EditSession(std::function<HRESULT(TfEditCookie)> body);

    // IUnknown
    STDMETHODIMP QueryInterface(REFIID riid, void** ppv) override;
    STDMETHODIMP_(ULONG) AddRef() override;
    STDMETHODIMP_(ULONG) Release() override;

    // ITfEditSession
    STDMETHODIMP DoEditSession(TfEditCookie ec) override;

private:
    ~EditSession();
    LONG references_ = 1;
    std::function<HRESULT(TfEditCookie)> body_;
};

/// 要求在 context 上執行 body。flags 例如 TF_ES_ASYNCDONTCARE | TF_ES_READWRITE。
HRESULT requestEditSession(ITfContext* context, TfClientId clientId, DWORD flags, std::function<HRESULT(TfEditCookie)> body);

}  // namespace bangbang
