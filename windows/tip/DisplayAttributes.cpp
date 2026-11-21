#include "DisplayAttributes.h"

#include <oleauto.h>

#include "Globals.h"
#include "Guids.h"

namespace bangbang {

namespace {

struct AttributeDefinition {
    const GUID* guid;
    const wchar_t* description;
    BOOL boldLine;
    TF_DA_ATTR_INFO attr;
};

const AttributeDefinition kDefinitions[] = {
    {&kDisplayAttributeInput, L"棒棒注音：組字中", FALSE, TF_ATTR_INPUT},
    {&kDisplayAttributeConverted, L"棒棒注音：選字中", TRUE, TF_ATTR_TARGET_CONVERTED},
};

TF_DISPLAYATTRIBUTE attributeOf(const AttributeDefinition& d) {
    TF_DISPLAYATTRIBUTE a{};
    a.crText.type = TF_CT_NONE;
    a.crBk.type = TF_CT_NONE;
    a.lsStyle = TF_LS_SOLID;
    a.fBoldLine = d.boldLine;
    a.crLine.type = TF_CT_NONE;
    a.bAttr = d.attr;
    return a;
}

class DisplayAttributeInfo final : public ITfDisplayAttributeInfo {
public:
    explicit DisplayAttributeInfo(const AttributeDefinition& definition) : definition_(definition) { dllAddRef(); }

    STDMETHODIMP QueryInterface(REFIID riid, void** ppv) override {
        if (!ppv) return E_POINTER;
        if (IsEqualIID(riid, IID_IUnknown) || IsEqualIID(riid, IID_ITfDisplayAttributeInfo)) {
            *ppv = static_cast<ITfDisplayAttributeInfo*>(this);
            AddRef();
            return S_OK;
        }
        *ppv = nullptr;
        return E_NOINTERFACE;
    }
    STDMETHODIMP_(ULONG) AddRef() override { return static_cast<ULONG>(InterlockedIncrement(&references_)); }
    STDMETHODIMP_(ULONG) Release() override {
        const LONG remaining = InterlockedDecrement(&references_);
        if (remaining == 0) delete this;
        return static_cast<ULONG>(remaining);
    }

    STDMETHODIMP GetGUID(GUID* guid) override {
        if (!guid) return E_INVALIDARG;
        *guid = *definition_.guid;
        return S_OK;
    }
    STDMETHODIMP GetDescription(BSTR* description) override {
        if (!description) return E_INVALIDARG;
        *description = SysAllocString(definition_.description);
        return *description ? S_OK : E_OUTOFMEMORY;
    }
    STDMETHODIMP GetAttributeInfo(TF_DISPLAYATTRIBUTE* attribute) override {
        if (!attribute) return E_INVALIDARG;
        *attribute = attributeOf(definition_);
        return S_OK;
    }
    STDMETHODIMP SetAttributeInfo(const TF_DISPLAYATTRIBUTE*) override { return E_NOTIMPL; }
    STDMETHODIMP Reset() override { return S_OK; }

private:
    ~DisplayAttributeInfo() { dllRelease(); }
    LONG references_ = 1;
    const AttributeDefinition& definition_;
};

class DisplayAttributeEnum final : public IEnumTfDisplayAttributeInfo {
public:
    explicit DisplayAttributeEnum(ULONG index = 0) : index_(index) { dllAddRef(); }

    STDMETHODIMP QueryInterface(REFIID riid, void** ppv) override {
        if (!ppv) return E_POINTER;
        if (IsEqualIID(riid, IID_IUnknown) || IsEqualIID(riid, IID_IEnumTfDisplayAttributeInfo)) {
            *ppv = static_cast<IEnumTfDisplayAttributeInfo*>(this);
            AddRef();
            return S_OK;
        }
        *ppv = nullptr;
        return E_NOINTERFACE;
    }
    STDMETHODIMP_(ULONG) AddRef() override { return static_cast<ULONG>(InterlockedIncrement(&references_)); }
    STDMETHODIMP_(ULONG) Release() override {
        const LONG remaining = InterlockedDecrement(&references_);
        if (remaining == 0) delete this;
        return static_cast<ULONG>(remaining);
    }

    STDMETHODIMP Clone(IEnumTfDisplayAttributeInfo** enumerator) override {
        if (!enumerator) return E_INVALIDARG;
        return guarded([&] {
            *enumerator = new DisplayAttributeEnum(index_);
            return S_OK;
        });
    }
    STDMETHODIMP Next(ULONG count, ITfDisplayAttributeInfo** infos, ULONG* fetched) override {
        if (!infos) return E_INVALIDARG;
        if (count != 1 && !fetched) return E_INVALIDARG;
        return guarded([&] {
            ULONG n = 0;
            while (n < count && index_ < _countof(kDefinitions)) {
                infos[n] = new DisplayAttributeInfo(kDefinitions[index_]);
                ++n;
                ++index_;
            }
            if (fetched) *fetched = n;
            return n == count ? S_OK : S_FALSE;
        });
    }
    STDMETHODIMP Reset() override {
        index_ = 0;
        return S_OK;
    }
    STDMETHODIMP Skip(ULONG count) override {
        const ULONG remaining = static_cast<ULONG>(_countof(kDefinitions)) - index_;
        const ULONG skipped = count < remaining ? count : remaining;
        index_ += skipped;
        return skipped == count ? S_OK : S_FALSE;
    }

private:
    ~DisplayAttributeEnum() { dllRelease(); }
    LONG references_ = 1;
    ULONG index_ = 0;
};

}  // namespace

HRESULT createDisplayAttributeInfo(REFGUID guid, ITfDisplayAttributeInfo** info) {
    if (!info) return E_INVALIDARG;
    *info = nullptr;
    for (const auto& d : kDefinitions) {
        if (IsEqualGUID(guid, *d.guid)) {
            return guarded([&] {
                *info = new DisplayAttributeInfo(d);
                return S_OK;
            });
        }
    }
    return E_INVALIDARG;
}

HRESULT createDisplayAttributeEnum(IEnumTfDisplayAttributeInfo** enumerator) {
    if (!enumerator) return E_INVALIDARG;
    return guarded([&] {
        *enumerator = new DisplayAttributeEnum();
        return S_OK;
    });
}

}  // namespace bangbang
