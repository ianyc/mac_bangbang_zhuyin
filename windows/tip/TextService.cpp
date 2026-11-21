#include "TextService.h"

#include <mutex>

#include "DisplayAttributes.h"
#include "EditSession.h"
#include "Globals.h"
#include "Guids.h"
#include "KeyMapping.h"
#include "SharedEngine.h"
#include "Utf8.h"

using Microsoft::WRL::ComPtr;

namespace bangbang {

namespace {

bool isKeyDown(int vk) {
    return (GetKeyState(vk) & 0x8000) != 0;
}

bool isToggled(int vk) {
    return (GetKeyState(vk) & 0x0001) != 0;
}

/// 不需要 COM 初始化的建立方式優先（TSF 可能在沒有初始化 COM 的情況下載入輸入法）。
ComPtr<ITfCategoryMgr> createCategoryMgr() {
    ComPtr<ITfCategoryMgr> categories;
    using Create = HRESULT(WINAPI*)(ITfCategoryMgr**);
    if (const HMODULE msctf = GetModuleHandleW(L"msctf.dll")) {
        if (const auto create = reinterpret_cast<Create>(reinterpret_cast<void*>(GetProcAddress(msctf, "TF_CreateCategoryMgr")))) {
            if (SUCCEEDED(create(&categories))) return categories;
        }
    }
    CoCreateInstance(CLSID_TF_CategoryMgr, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&categories));
    return categories;
}

/// compartment（例如「鍵盤停用」）是否設成非 0。
bool compartmentIsSet(IUnknown* owner, REFGUID guid) {
    ComPtr<ITfCompartmentMgr> compartments;
    if (!owner || FAILED(owner->QueryInterface(IID_PPV_ARGS(&compartments)))) return false;
    ComPtr<ITfCompartment> compartment;
    if (FAILED(compartments->GetCompartment(guid, &compartment))) return false;
    VARIANT value;
    VariantInit(&value);
    const bool set = SUCCEEDED(compartment->GetValue(&value)) && value.vt == VT_I4 && value.lVal != 0;
    VariantClear(&value);
    return set;
}

/// 設定 compartment 的值（例如鍵盤開關、輸入模式）。
HRESULT setCompartment(IUnknown* owner, TfClientId clientId, REFGUID guid, LONG value) {
    ComPtr<ITfCompartmentMgr> compartments;
    if (!owner) return E_INVALIDARG;
    HRESULT hr = owner->QueryInterface(IID_PPV_ARGS(&compartments));
    if (FAILED(hr)) return hr;
    ComPtr<ITfCompartment> compartment;
    hr = compartments->GetCompartment(guid, &compartment);
    if (FAILED(hr)) return hr;
    VARIANT variant;
    VariantInit(&variant);
    variant.vt = VT_I4;
    variant.lVal = value;
    return compartment->SetValue(clientId, &variant);
}

void setSelection(TfEditCookie ec, ITfContext* context, ITfRange* range) {
    TF_SELECTION selection{};
    selection.range = range;
    selection.style.ase = TF_AE_NONE;
    selection.style.fInterimChar = FALSE;
    context->SetSelection(ec, 1, &selection);
}

void setAttribute(TfEditCookie ec, ITfContext* context, ITfRange* range, TfGuidAtom atom) {
    if (atom == TF_INVALID_GUIDATOM) return;
    ComPtr<ITfProperty> property;
    if (FAILED(context->GetProperty(GUID_PROP_ATTRIBUTE, &property))) return;
    VARIANT value;
    VariantInit(&value);
    value.vt = VT_I4;
    value.lVal = static_cast<LONG>(atom);
    property->SetValue(ec, range, &value);
}

void clearAttributes(TfEditCookie ec, ITfContext* context, ITfRange* range) {
    ComPtr<ITfProperty> property;
    if (SUCCEEDED(context->GetProperty(GUID_PROP_ATTRIBUTE, &property))) property->Clear(ec, range);
}

/// range 裡從 start 到 end（UTF-16 單位）的那一段。
ComPtr<ITfRange> subRange(TfEditCookie ec, ITfRange* range, LONG start, LONG end) {
    ComPtr<ITfRange> part;
    if (FAILED(range->Clone(&part))) return nullptr;
    part->Collapse(ec, TF_ANCHOR_START);
    LONG moved = 0;
    part->ShiftEnd(ec, end, &moved, nullptr);
    part->ShiftStart(ec, start, &moved, nullptr);
    return part;
}

}  // namespace

TextService::TextService() {
    dllAddRef();
}

TextService::~TextService() {
    dllRelease();
}

// MARK: - IUnknown

STDMETHODIMP TextService::QueryInterface(REFIID riid, void** ppv) {
    if (!ppv) return E_POINTER;
    *ppv = nullptr;
    if (IsEqualIID(riid, IID_IUnknown) || IsEqualIID(riid, IID_ITfTextInputProcessor) ||
        IsEqualIID(riid, IID_ITfTextInputProcessorEx)) {
        *ppv = static_cast<ITfTextInputProcessorEx*>(this);
    } else if (IsEqualIID(riid, IID_ITfThreadMgrEventSink)) {
        *ppv = static_cast<ITfThreadMgrEventSink*>(this);
    } else if (IsEqualIID(riid, IID_ITfThreadFocusSink)) {
        *ppv = static_cast<ITfThreadFocusSink*>(this);
    } else if (IsEqualIID(riid, IID_ITfActiveLanguageProfileNotifySink)) {
        *ppv = static_cast<ITfActiveLanguageProfileNotifySink*>(this);
    } else if (IsEqualIID(riid, IID_ITfKeyEventSink)) {
        *ppv = static_cast<ITfKeyEventSink*>(this);
    } else if (IsEqualIID(riid, IID_ITfCompositionSink)) {
        *ppv = static_cast<ITfCompositionSink*>(this);
    } else if (IsEqualIID(riid, IID_ITfDisplayAttributeProvider)) {
        *ppv = static_cast<ITfDisplayAttributeProvider*>(this);
    }
    if (!*ppv) return E_NOINTERFACE;
    AddRef();
    return S_OK;
}

STDMETHODIMP_(ULONG) TextService::AddRef() {
    return static_cast<ULONG>(InterlockedIncrement(&references_));
}

STDMETHODIMP_(ULONG) TextService::Release() {
    const LONG remaining = InterlockedDecrement(&references_);
    if (remaining == 0) delete this;
    return static_cast<ULONG>(remaining);
}

// MARK: - 啟用／停用

STDMETHODIMP TextService::Activate(ITfThreadMgr* threadMgr, TfClientId clientId) {
    return ActivateEx(threadMgr, clientId, 0);
}

STDMETHODIMP TextService::ActivateEx(ITfThreadMgr* threadMgr, TfClientId clientId, DWORD flags) {
    if (!threadMgr) return E_INVALIDARG;
    return guarded([&]() -> HRESULT {
        threadMgr_ = threadMgr;
        clientId_ = clientId;
        engine_ = sharedEngine((flags & TF_TMAE_SECUREMODE) != 0);
        if (engine_ && !session_) session_ = std::make_unique<InputSession>(engine_->lm, engine_->userModel.get());

        ComPtr<ITfSource> source;
        if (SUCCEEDED(threadMgr->QueryInterface(IID_PPV_ARGS(&source)))) {
            DWORD cookie = TF_INVALID_COOKIE;
            if (SUCCEEDED(source->AdviseSink(IID_ITfThreadMgrEventSink, static_cast<ITfThreadMgrEventSink*>(this), &cookie))) {
                threadMgrSinkCookie_ = cookie;
            }
            cookie = TF_INVALID_COOKIE;
            if (SUCCEEDED(source->AdviseSink(IID_ITfThreadFocusSink, static_cast<ITfThreadFocusSink*>(this), &cookie))) {
                threadFocusSinkCookie_ = cookie;
            }
            cookie = TF_INVALID_COOKIE;
            if (SUCCEEDED(source->AdviseSink(IID_ITfActiveLanguageProfileNotifySink, static_cast<ITfActiveLanguageProfileNotifySink*>(this),
                                             &cookie))) {
                profileSinkCookie_ = cookie;
            }
        }
        ComPtr<ITfKeystrokeMgr> keystrokes;
        if (SUCCEEDED(threadMgr->QueryInterface(IID_PPV_ARGS(&keystrokes)))) {
            keySinkAdvised_ = SUCCEEDED(keystrokes->AdviseKeyEventSink(clientId, static_cast<ITfKeyEventSink*>(this), TRUE));
        }
        if (const auto categories = createCategoryMgr()) {
            categories->RegisterGUID(kDisplayAttributeInput, &inputAtom_);
            categories->RegisterGUID(kDisplayAttributeConverted, &convertedAtom_);
        }
        DWORD activeFlags = 0;
        ComPtr<ITfThreadMgrEx> threadMgrEx;
        if (SUCCEEDED(threadMgr->QueryInterface(IID_PPV_ARGS(&threadMgrEx)))) threadMgrEx->GetActiveFlags(&activeFlags);
        debugLog(L"棒棒注音：啟用（ActivateEx 0x%X、TSF 0x%X，%s）", static_cast<unsigned>(flags), static_cast<unsigned>(activeFlags),
                 engine_ ? L"模型已載入" : L"沒有模型，按鍵全部交還");
        publishInputMode();
        return S_OK;
    });
}

STDMETHODIMP TextService::Deactivate() {
    return guarded([&]() -> HRESULT {
        // 切換到別的輸入法時，送出整個組字區。
        commitComposition();
        if (threadMgr_) {
            ComPtr<ITfSource> source;
            if (SUCCEEDED(threadMgr_.As(&source))) {
                if (threadMgrSinkCookie_ != TF_INVALID_COOKIE) source->UnadviseSink(threadMgrSinkCookie_);
                if (threadFocusSinkCookie_ != TF_INVALID_COOKIE) source->UnadviseSink(threadFocusSinkCookie_);
                if (profileSinkCookie_ != TF_INVALID_COOKIE) source->UnadviseSink(profileSinkCookie_);
            }
            if (keySinkAdvised_) {
                ComPtr<ITfKeystrokeMgr> keystrokes;
                if (SUCCEEDED(threadMgr_.As(&keystrokes))) keystrokes->UnadviseKeyEventSink(clientId_);
            }
        }
        threadMgrSinkCookie_ = TF_INVALID_COOKIE;
        threadFocusSinkCookie_ = TF_INVALID_COOKIE;
        profileSinkCookie_ = TF_INVALID_COOKIE;
        keySinkAdvised_ = false;
        activeContext_.Reset();
        threadMgr_.Reset();
        clientId_ = TF_CLIENTID_NULL;
        return S_OK;
    });
}

// MARK: - ITfThreadMgrEventSink

STDMETHODIMP TextService::OnInitDocumentMgr(ITfDocumentMgr*) {
    return S_OK;
}

STDMETHODIMP TextService::OnUninitDocumentMgr(ITfDocumentMgr*) {
    return S_OK;
}

STDMETHODIMP TextService::OnSetFocus(ITfDocumentMgr* focus, ITfDocumentMgr*) {
    return guarded([&]() -> HRESULT {
        // 焦點移到別的文件（另一個輸入框）時，送出原本文件裡的組字。
        if (compositionContext_) {
            ComPtr<ITfDocumentMgr> owner;
            if (FAILED(compositionContext_->GetDocumentMgr(&owner)) || owner.Get() != focus) commitComposition();
        }
        return S_OK;
    });
}

STDMETHODIMP TextService::OnPushContext(ITfContext*) {
    return S_OK;
}

STDMETHODIMP TextService::OnPopContext(ITfContext* context) {
    return guarded([&]() -> HRESULT {
        // 文件要消失了：組字不必也沒辦法送出。
        if (context && compositionContext_.Get() == context) {
            forgetComposition();
            pendingCommit_.clear();
            if (session_) session_->reset();
        }
        if (context && activeContext_.Get() == context) activeContext_.Reset();
        return S_OK;
    });
}

// MARK: - ITfThreadFocusSink

STDMETHODIMP TextService::OnSetThreadFocus() {
    return S_OK;
}

STDMETHODIMP TextService::OnKillThreadFocus() {
    // 切換到別的應用程式時，送出整個組字區（同 Mac 版）。
    return guarded([&]() -> HRESULT {
        commitComposition();
        return S_OK;
    });
}

// MARK: - ITfActiveLanguageProfileNotifySink

STDMETHODIMP TextService::OnActivated(REFCLSID clsid, REFGUID, BOOL activated) {
    // 只在 ActivateEx 裡設定時，testpad 事後讀到的轉換模式是 VT_EMPTY（沒有生效），所以等語言設定檔啟用完成後
    // 再設定一次（微軟範例 SampleIME 也會在這裡更新輸入模式）。
    return guarded([&]() -> HRESULT {
        if (activated && IsEqualCLSID(clsid, kTextServiceClsid)) publishInputMode();
        return S_OK;
    });
}

// MARK: - ITfKeyEventSink

STDMETHODIMP TextService::OnSetFocus(BOOL) {
    return S_OK;
}

TextService::KeyDecision TextService::decide(ITfContext* context, WPARAM wParam, LPARAM lParam) const {
    KeyDecision decision;
    if (!session_ || !context) return decision;
    // 密碼欄、唯讀欄位等不能輸入的地方：按鍵全部交還。
    if (compartmentIsSet(context, GUID_COMPARTMENT_KEYBOARD_DISABLED) || compartmentIsSet(context, GUID_COMPARTMENT_EMPTYCONTEXT)) {
        return decision;
    }
    const bool composing = session_->isComposing();
    if (isModifierKey(wParam)) {
        // 組字中按 Caps Lock：先送出組字區，之後的大寫英文直接交給應用程式。
        if (wParam == VK_CAPITAL && composing) decision.action = KeyAction::CommitThenPass;
        return decision;
    }
    // Ctrl／Alt 組合鍵一律交給應用程式，不動組字區（同 Mac 版的 Command／Control／Option）。
    if (isKeyDown(VK_CONTROL) || isKeyDown(VK_MENU)) return decision;
    // Caps Lock 鎖定大寫時，英文直接交給應用程式；組字中的話先送出組字區（同 Mac 版）。
    if (isToggled(VK_CAPITAL)) {
        if (composing) decision.action = KeyAction::CommitThenPass;
        return decision;
    }
    const auto key = keyEventFromKey(wParam, lParam, isKeyDown(VK_SHIFT));
    if (!key) {
        // 組字中按到沒有對應的鍵：吃掉不處理，以免干擾。
        if (composing) decision.action = KeyAction::Swallow;
        return decision;
    }
    if (session_->wouldHandle(*key)) {
        decision.action = KeyAction::Handle;
        decision.key = *key;
    }
    return decision;
}

STDMETHODIMP TextService::OnTestKeyDown(ITfContext* context, WPARAM wParam, LPARAM lParam, BOOL* eaten) {
    if (!eaten) return E_INVALIDARG;
    *eaten = FALSE;
    return guarded([&]() -> HRESULT {
        *eaten = decide(context, wParam, lParam).action != KeyAction::PassThrough;
        return S_OK;
    });
}

STDMETHODIMP TextService::OnKeyDown(ITfContext* context, WPARAM wParam, LPARAM lParam, BOOL* eaten) {
    if (!eaten) return E_INVALIDARG;
    *eaten = FALSE;
    const HRESULT hr = guarded([&]() -> HRESULT {
        // 組字還留在別的文件裡（焦點換了卻沒收到通知）：先把那邊送出。
        if (compositionContext_ && context && compositionContext_.Get() != context) commitComposition();
        const KeyDecision decision = decide(context, wParam, lParam);
        switch (decision.action) {
        case KeyAction::PassThrough:
            return S_OK;
        case KeyAction::Swallow:
            *eaten = TRUE;
            return S_OK;
        case KeyAction::CommitThenPass:
            commitComposition();
            return S_OK;
        case KeyAction::Handle:
            break;
        }
        SessionOutput output;
        {
            std::lock_guard lock(engine_->mutex);
            output = session_->handle(decision.key);
        }
        *eaten = output.handled ? TRUE : FALSE;
        if (output.beep) MessageBeep(MB_OK);
        pendingCommit_ += output.commit;
        activeContext_ = context;
        if (!pendingCommit_.empty() || session_->isComposing() || composition_) requestUpdate(context);
        return S_OK;
    });
    if (wParam < eatenKeyDown_.size()) eatenKeyDown_[wParam] = *eaten != FALSE;
    return hr;
}

STDMETHODIMP TextService::OnTestKeyUp(ITfContext*, WPARAM wParam, LPARAM, BOOL* eaten) {
    if (!eaten) return E_INVALIDARG;
    // 只吃掉「按下時被吃掉」的鍵的放開事件：應用程式看到按下的鍵，就一定看得到放開。
    *eaten = wParam < eatenKeyDown_.size() && eatenKeyDown_[wParam];
    return S_OK;
}

STDMETHODIMP TextService::OnKeyUp(ITfContext*, WPARAM wParam, LPARAM, BOOL* eaten) {
    if (!eaten) return E_INVALIDARG;
    *eaten = wParam < eatenKeyDown_.size() && eatenKeyDown_[wParam];
    if (wParam < eatenKeyDown_.size()) eatenKeyDown_[wParam] = false;
    return S_OK;
}

STDMETHODIMP TextService::OnPreservedKey(ITfContext*, REFGUID, BOOL* eaten) {
    if (!eaten) return E_INVALIDARG;
    *eaten = FALSE;
    return S_OK;
}

// MARK: - ITfCompositionSink

STDMETHODIMP TextService::OnCompositionTerminated(TfEditCookie ec, ITfComposition* composition) {
    return guarded([&]() -> HRESULT {
        // 應用程式結束了組字（例如在別處點一下）：文件裡留下的是目前的組字文字。
        // 換成選好的字（去掉還沒拼完的注音），再把引擎清空。
        if (composition && session_ && compositionContext_) {
            ComPtr<ITfRange> range;
            if (SUCCEEDED(composition->GetRange(&range))) {
                const std::wstring text = utf8::toWide(pendingCommit_ + session_->composition.text());
                range->SetText(ec, 0, text.c_str(), static_cast<LONG>(text.size()));
                clearAttributes(ec, compositionContext_.Get(), range.Get());
            }
        }
        pendingCommit_.clear();
        if (session_) session_->reset();
        forgetComposition();
        return S_OK;
    });
}

// MARK: - ITfDisplayAttributeProvider

STDMETHODIMP TextService::EnumDisplayAttributeInfo(IEnumTfDisplayAttributeInfo** enumerator) {
    return createDisplayAttributeEnum(enumerator);
}

STDMETHODIMP TextService::GetDisplayAttributeInfo(REFGUID guid, ITfDisplayAttributeInfo** info) {
    return createDisplayAttributeInfo(guid, info);
}

// MARK: - 輸入模式

void TextService::publishInputMode() {
    if (!threadMgr_) return;
    // 註冊了 INPUTMODECOMPARTMENT 類別：工作列的輸入模式圖示、IMM32 程式看到的開關狀態都讀這兩個值。
    // 中英切換（第 5 階段）會再更新它們。沒有模型時按鍵全部交還，算英文。
    const bool chinese = engine_ != nullptr;
    const HRESULT open = setCompartment(threadMgr_.Get(), clientId_, GUID_COMPARTMENT_KEYBOARD_OPENCLOSE, chinese ? TRUE : FALSE);
    const HRESULT mode = setCompartment(threadMgr_.Get(), clientId_, GUID_COMPARTMENT_KEYBOARD_INPUTMODE_CONVERSION,
                                        chinese ? TF_CONVERSIONMODE_NATIVE : TF_CONVERSIONMODE_ALPHANUMERIC);
    debugLog(L"棒棒注音：輸入模式（開關 0x%08X、轉換模式 0x%08X）", static_cast<unsigned>(open), static_cast<unsigned>(mode));
}

// MARK: - 組字

void TextService::commitComposition() {
    if (!session_) return;
    std::string text;
    text.swap(pendingCommit_);
    if (session_->isComposing()) text += session_->commitAll();
    const ComPtr<ITfComposition> composition = composition_;
    const ComPtr<ITfContext> context = compositionContext_;
    // 之後的按鍵從新的組字開始；這一段的收尾只處理這一個組字物件，和之後的狀態無關。
    forgetComposition();
    if (!composition || !context) return;
    const std::wstring wide = utf8::toWide(text);
    const auto body = [composition, context, wide](TfEditCookie ec) -> HRESULT {
        ComPtr<ITfRange> range;
        if (SUCCEEDED(composition->GetRange(&range))) {
            range->SetText(ec, 0, wide.c_str(), static_cast<LONG>(wide.size()));
            clearAttributes(ec, context.Get(), range.Get());
            range->Collapse(ec, TF_ANCHOR_END);
            setSelection(ec, context.Get(), range.Get());
        }
        composition->EndComposition(ec);
        return S_OK;
    };
    // 盡量同步完成，免得和接下來的按鍵（可能在別的文件裡）交錯。
    if (FAILED(requestEditSession(context.Get(), clientId_, TF_ES_SYNC | TF_ES_READWRITE, body))) {
        requestEditSession(context.Get(), clientId_, TF_ES_ASYNC | TF_ES_READWRITE, body);
    }
}

void TextService::requestUpdate(ITfContext* context) {
    if (!context) return;
    const ComPtr<TextService> self(this);
    const ComPtr<ITfContext> target(context);
    // SampleIME 註明：Word 不能用同步的 edit session 處理按鍵，所以讓 TSF 決定同步或非同步。
    const HRESULT hr = requestEditSession(context, clientId_, TF_ES_ASYNCDONTCARE | TF_ES_READWRITE,
                                          [self, target](TfEditCookie ec) { return self->updateDocument(ec, target.Get()); });
    if (FAILED(hr)) debugLog(L"棒棒注音：RequestEditSession 失敗 0x%08X", static_cast<unsigned>(hr));
}

HRESULT TextService::updateDocument(TfEditCookie ec, ITfContext* context) {
    // 非同步的 edit session 執行時，焦點可能已經換到別的文件：過時的就不處理。
    if (context != activeContext_.Get()) return S_OK;
    if (composition_ && compositionContext_.Get() != context) return S_OK;
    // 先送出累積的文字，再把引擎「目前的」組字狀態整個畫上去，所以非同步執行也不會亂。
    std::string commit;
    commit.swap(pendingCommit_);
    if (!commit.empty()) finishComposition(ec, context, utf8::toWide(commit));
    if (session_ && session_->isComposing()) {
        showComposition(ec, context);
    } else if (composition_) {
        finishComposition(ec, context, std::wstring());  // 例如按 Esc 清掉了
    }
    return S_OK;
}

bool TextService::startComposition(TfEditCookie ec, ITfContext* context) {
    ComPtr<ITfInsertAtSelection> insert;
    ComPtr<ITfContextComposition> contextComposition;
    if (FAILED(context->QueryInterface(IID_PPV_ARGS(&insert))) || FAILED(context->QueryInterface(IID_PPV_ARGS(&contextComposition)))) {
        return false;
    }
    ComPtr<ITfRange> range;
    if (FAILED(insert->InsertTextAtSelection(ec, TF_IAS_QUERYONLY, nullptr, 0, &range)) || !range) return false;
    ComPtr<ITfComposition> composition;
    if (FAILED(contextComposition->StartComposition(ec, range.Get(), static_cast<ITfCompositionSink*>(this), &composition)) ||
        !composition) {
        return false;
    }
    composition_ = composition;
    compositionContext_ = context;
    setSelection(ec, context, range.Get());
    return true;
}

void TextService::showComposition(TfEditCookie ec, ITfContext* context) {
    if (!composition_ && !startComposition(ec, context)) {
        // 這個位置不能組字（例如唯讀）：放棄，避免留下看不見的組字。
        session_->reset();
        return;
    }
    const std::string marked = session_->markedText();
    const std::wstring text = utf8::toWide(marked);
    ComPtr<ITfRange> range;
    if (FAILED(composition_->GetRange(&range))) return;
    if (FAILED(range->SetText(ec, TF_ST_CORRECTION, text.c_str(), static_cast<LONG>(text.size())))) return;
    range.Reset();
    if (FAILED(composition_->GetRange(&range))) return;
    // 整段單底線；正在選字的那一段粗底線。位置都要換算成 UTF-16（BMP 以外的字佔 2 個單位）。
    setAttribute(ec, context, range.Get(), inputAtom_);
    if (const auto candidate = session_->candidateRange()) {
        const auto start = static_cast<LONG>(utf8::utf16Offset(marked, static_cast<std::size_t>(candidate->first)));
        const auto end = static_cast<LONG>(utf8::utf16Offset(marked, static_cast<std::size_t>(candidate->second)));
        if (const auto part = subRange(ec, range.Get(), start, end)) setAttribute(ec, context, part.Get(), convertedAtom_);
    }
    const auto caret = static_cast<LONG>(utf8::utf16Offset(marked, static_cast<std::size_t>(session_->markedCursor())));
    if (const auto caretRange = subRange(ec, range.Get(), caret, caret)) setSelection(ec, context, caretRange.Get());
}

void TextService::finishComposition(TfEditCookie ec, ITfContext* context, const std::wstring& text) {
    // 沒有組字時要直接送出文字：開一個組字、放文字、馬上結束（和大多數輸入法一樣，相容性最好）。
    if (!composition_ && (text.empty() || !startComposition(ec, context))) return;
    ComPtr<ITfRange> range;
    if (SUCCEEDED(composition_->GetRange(&range))) {
        range->SetText(ec, 0, text.c_str(), static_cast<LONG>(text.size()));
        clearAttributes(ec, context, range.Get());
        range->Collapse(ec, TF_ANCHOR_END);
        setSelection(ec, context, range.Get());
    }
    composition_->EndComposition(ec);
    forgetComposition();
}

void TextService::forgetComposition() {
    composition_.Reset();
    compositionContext_.Reset();
}

}  // namespace bangbang
