#include "TextService.h"

#include <mutex>

#include "BangbangVersion.h"
#include "CandidateWindow.h"
#include "DisplayAttributes.h"
#include "EditSession.h"
#include "Globals.h"
#include "Guids.h"
#include "InputModeButton.h"
#include "KeyMapping.h"
#include "SharedEngine.h"
#include "Utf8.h"

using Microsoft::WRL::ComPtr;

namespace bangbang {

namespace {

/// 每個 UI 執行緒只有一個 TextService；訊息掛鉤與計時器用它找到該執行緒的 TextService。
thread_local TextService* t_threadService = nullptr;

/// 這個執行緒的訊息掛鉤（WH_GETMESSAGE）：程式取出的每一個鍵盤、滑鼠訊息都會經過這裡。
/// 英文模式、或舊式（IMM32）程式在輸入法關閉時不會把按鍵交給 TSF，靠它才看得到 Shift。
LRESULT CALLBACK messageHookProc(int code, WPARAM wParam, LPARAM lParam) {
    if (code == HC_ACTION && wParam == PM_REMOVE && t_threadService) {
        t_threadService->onThreadMessage(*reinterpret_cast<const MSG*>(lParam));
    }
    return CallNextHookEx(nullptr, code, wParam, lParam);
}

void CALLBACK republishTimerProc(HWND, UINT, UINT_PTR id, DWORD) {
    KillTimer(nullptr, id);
    if (t_threadService) t_threadService->onRepublishTimer();
}

/// compartment 的值（VT_EMPTY 表示沒有人設定過）。
struct CompartmentRead {
    VARTYPE type = VT_EMPTY;
    LONG value = 0;
};

CompartmentRead readCompartmentValue(ITfCompartment* compartment) {
    CompartmentRead result;
    if (!compartment) return result;
    VARIANT value;
    VariantInit(&value);
    if (SUCCEEDED(compartment->GetValue(&value))) {
        result.type = value.vt;
        if (value.vt == VT_I4) result.value = value.lVal;
    }
    VariantClear(&value);
    return result;
}

DWORD adviseCompartmentSink(ITfCompartment* compartment, ITfCompartmentEventSink* sink) {
    ComPtr<ITfSource> source;
    DWORD cookie = TF_INVALID_COOKIE;
    if (compartment && SUCCEEDED(compartment->QueryInterface(IID_PPV_ARGS(&source)))) {
        if (FAILED(source->AdviseSink(IID_ITfCompartmentEventSink, sink, &cookie))) cookie = TF_INVALID_COOKIE;
    }
    return cookie;
}

void unadviseCompartmentSink(ITfCompartment* compartment, DWORD& cookie) {
    ComPtr<ITfSource> source;
    if (compartment && cookie != TF_INVALID_COOKIE && SUCCEEDED(compartment->QueryInterface(IID_PPV_ARGS(&source)))) {
        source->UnadviseSink(cookie);
    }
    cookie = TF_INVALID_COOKIE;
}

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
    } else if (IsEqualIID(riid, IID_ITfTextLayoutSink)) {
        *ppv = static_cast<ITfTextLayoutSink*>(this);
    } else if (IsEqualIID(riid, IID_ITfCompartmentEventSink)) {
        *ppv = static_cast<ITfCompartmentEventSink*>(this);
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
        activateFlags_ = flags;
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
        // 中英狀態：所有程式共用的全域狀態（還沒有人設定過就是中文）。
        t_threadService = this;
        chinese_ = readGlobalMode();
        adviseCompartmentSinks();
        ComPtr<ITfLangBarItemMgr> langBar;
        if (SUCCEEDED(threadMgr->QueryInterface(IID_PPV_ARGS(&langBar)))) {
            modeButton_.Attach(new InputModeButton([this](TfLBIClick click, POINT point) { onModeButtonClick(click, point); }));
            modeButton_->setChinese(chinese_);
            if (FAILED(langBar->AddItem(modeButton_.Get()))) {
                modeButton_->detach();
                modeButton_.Reset();
            }
        }
        installMessageHook();
        debugLog(L"棒棒注音：啟用（client %lu，ActivateEx 0x%X、TSF 0x%X，%s，%s）", static_cast<unsigned long>(clientId),
                 static_cast<unsigned>(flags), static_cast<unsigned>(activeFlags), engine_ ? L"模型已載入" : L"沒有模型，按鍵全部交還",
                 chinese_ ? L"中文" : L"英文");
        publishInputMode();
        return S_OK;
    });
}

STDMETHODIMP TextService::Deactivate() {
    return guarded([&]() -> HRESULT {
        debugLog(L"棒棒注音：停用（client %lu）", static_cast<unsigned long>(clientId_));
        // 切換到別的輸入法時，送出整個組字區。
        commitComposition();
        closeCandidateUI();
        candidateWindow_.reset();
        if (candidateUI_) {
            candidateUI_->detach();  // 程式可能還拿著這個 UI 元件
            candidateUI_.Reset();
        }
        removeMessageHook();
        if (republishTimer_) KillTimer(nullptr, republishTimer_);
        republishTimer_ = 0;
        if (t_threadService == this) t_threadService = nullptr;
        shiftTap_.reset();
        unadviseCompartmentSinks();
        if (modeButton_) {
            ComPtr<ITfLangBarItemMgr> langBar;
            if (threadMgr_ && SUCCEEDED(threadMgr_.As(&langBar))) langBar->RemoveItem(modeButton_.Get());
            modeButton_->detach();
            modeButton_.Reset();
        }
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
            closeCandidateUI();
        }
        if (context && layoutContext_.Get() == context) unadviseLayoutSink();
        if (context && activeContext_.Get() == context) activeContext_.Reset();
        return S_OK;
    });
}

// MARK: - ITfThreadFocusSink

STDMETHODIMP TextService::OnSetThreadFocus() {
    return S_OK;
}

STDMETHODIMP TextService::OnKillThreadFocus() {
    // 切換到別的應用程式時，送出整個組字區（同 Mac 版）。Shift 的放開可能送到別的程式，忘掉進行中的單按。
    return guarded([&]() -> HRESULT {
        shiftTap_.reset();
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
    // 英文模式：按鍵全部交給應用程式（Caps Lock、Shift＋字母都照系統原本的行為）。
    if (!chinese_) return decision;
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
    shiftTap_.keyDown(static_cast<unsigned>(wParam));
    return guarded([&]() -> HRESULT {
        *eaten = decide(context, wParam, lParam).action != KeyAction::PassThrough;
        return S_OK;
    });
}

STDMETHODIMP TextService::OnKeyDown(ITfContext* context, WPARAM wParam, LPARAM lParam, BOOL* eaten) {
    if (!eaten) return E_INVALIDARG;
    *eaten = FALSE;
    shiftTap_.keyDown(static_cast<unsigned>(wParam));
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
    // Shift 從來不吃，所以程式一定看得到 Shift 放開。
    *eaten = wParam < eatenKeyDown_.size() && eatenKeyDown_[wParam];
    return guarded([&]() -> HRESULT {
        if (shiftTap_.keyUp(static_cast<unsigned>(wParam))) toggleMode();
        return S_OK;
    });
}

STDMETHODIMP TextService::OnKeyUp(ITfContext*, WPARAM wParam, LPARAM, BOOL* eaten) {
    if (!eaten) return E_INVALIDARG;
    *eaten = wParam < eatenKeyDown_.size() && eatenKeyDown_[wParam];
    if (wParam < eatenKeyDown_.size()) eatenKeyDown_[wParam] = false;
    return guarded([&]() -> HRESULT {
        if (shiftTap_.keyUp(static_cast<unsigned>(wParam))) toggleMode();
        return S_OK;
    });
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
        closeCandidateUI();
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
    // 註冊了 INPUTMODECOMPARTMENT 類別：IMM32 程式看到的開關狀態、轉換模式都讀這兩個值。
    // 沒有模型時按鍵全部交還，算英文。
    const bool chinese = chinese_ && engine_ != nullptr;
    writingCompartments_ = true;
    const HRESULT open = setCompartment(threadMgr_.Get(), clientId_, GUID_COMPARTMENT_KEYBOARD_OPENCLOSE, chinese ? TRUE : FALSE);
    const HRESULT mode = setCompartment(threadMgr_.Get(), clientId_, GUID_COMPARTMENT_KEYBOARD_INPUTMODE_CONVERSION,
                                        chinese ? TF_CONVERSIONMODE_NATIVE : TF_CONVERSIONMODE_ALPHANUMERIC);
    writingCompartments_ = false;
    debugLog(L"棒棒注音：輸入模式 %s（開關 0x%08X、轉換模式 0x%08X）", chinese ? L"中文" : L"英文", static_cast<unsigned>(open),
             static_cast<unsigned>(mode));
}

// MARK: - 中英切換

ComPtr<ITfCompartment> TextService::compartment(REFGUID guid, bool global) const {
    if (!threadMgr_) return nullptr;
    ComPtr<ITfCompartmentMgr> manager;
    if (global ? FAILED(threadMgr_->GetGlobalCompartment(&manager)) : FAILED(threadMgr_.As(&manager))) return nullptr;
    ComPtr<ITfCompartment> result;
    if (FAILED(manager->GetCompartment(guid, &result))) return nullptr;
    return result;
}

bool TextService::readGlobalMode() const {
    const CompartmentRead global = readCompartmentValue(compartment(kChineseModeCompartment, true).Get());
    return global.type != VT_I4 || global.value != 0;  // 還沒有人設定過：中文
}

void TextService::setGlobalMode(bool chinese) {
    if (const auto global = compartment(kChineseModeCompartment, true)) {
        VARIANT value;
        VariantInit(&value);
        value.vt = VT_I4;
        value.lVal = chinese ? 1 : 0;
        global->SetValue(clientId_, &value);
    }
    // 其他程式（和這個程式的其他執行緒）收到 OnChange 後各自套用；這個執行緒馬上套用，不等通知。
    applyMode(chinese);
}

void TextService::toggleMode() {
    // 以這個程式目前的狀態為準：程式自己關掉了輸入法時，單按 Shift 是切回中文。
    setGlobalMode(!chinese_);
}

void TextService::applyMode(bool chinese) {
    if (chinese == chinese_) return;
    // 切到英文前先送出組字區（同 Mac 版切換輸入法）。
    if (!chinese) commitComposition();
    chinese_ = chinese;
    publishInputMode();
    if (modeButton_) modeButton_->setChinese(chinese_);
    debugLog(L"棒棒注音：切換成%s", chinese_ ? L"中文" : L"英文");
}

void TextService::adviseCompartmentSinks() {
    auto* sink = static_cast<ITfCompartmentEventSink*>(this);
    globalModeSinkCookie_ = adviseCompartmentSink(compartment(kChineseModeCompartment, true).Get(), sink);
    openCloseSinkCookie_ = adviseCompartmentSink(compartment(GUID_COMPARTMENT_KEYBOARD_OPENCLOSE, false).Get(), sink);
    conversionSinkCookie_ = adviseCompartmentSink(compartment(GUID_COMPARTMENT_KEYBOARD_INPUTMODE_CONVERSION, false).Get(), sink);
}

void TextService::unadviseCompartmentSinks() {
    unadviseCompartmentSink(compartment(kChineseModeCompartment, true).Get(), globalModeSinkCookie_);
    unadviseCompartmentSink(compartment(GUID_COMPARTMENT_KEYBOARD_OPENCLOSE, false).Get(), openCloseSinkCookie_);
    unadviseCompartmentSink(compartment(GUID_COMPARTMENT_KEYBOARD_INPUTMODE_CONVERSION, false).Get(), conversionSinkCookie_);
}

STDMETHODIMP TextService::OnChange(REFGUID guid) {
    return guarded([&]() -> HRESULT {
        if (IsEqualGUID(guid, kChineseModeCompartment)) {
            // 某個程式切換了中英（單按 Shift、點工作列圖示）：所有程式跟著切換。
            applyMode(readGlobalMode());
            return S_OK;
        }
        if (writingCompartments_ || !threadMgr_) return S_OK;
        const bool chinese = chinese_ && engine_ != nullptr;
        // 注意：在某個 compartment 的變更通知裡改同一個 compartment 會失敗（E_UNEXPECTED），所以重新設定一律延後。
        if (IsEqualGUID(guid, GUID_COMPARTMENT_KEYBOARD_OPENCLOSE)) {
            const CompartmentRead open = readCompartmentValue(compartment(GUID_COMPARTMENT_KEYBOARD_OPENCLOSE, false).Get());
            if (open.type != VT_I4) {
                schedulePublishInputMode();  // 被清掉了：重新設定
            } else if ((open.value != 0) != chinese && engine_) {
                // 程式自己打開或關掉輸入法（例如某些欄位）：只影響這個程式，不改全域的中英狀態。
                debugLog(L"棒棒注音：程式把輸入法%s（只影響這個程式）", open.value != 0 ? L"打開" : L"關閉");
                applyMode(open.value != 0);
            }
        } else if (IsEqualGUID(guid, GUID_COMPARTMENT_KEYBOARD_INPUTMODE_CONVERSION)) {
            const CompartmentRead mode = readCompartmentValue(compartment(GUID_COMPARTMENT_KEYBOARD_INPUTMODE_CONVERSION, false).Get());
            // 第 3 階段的已知問題：啟用後不久，轉換模式會被清成 VT_EMPTY（不是我們清的）。被清掉就重新設定；
            // 程式改成別的值則只記錄（例如某些欄位要求英數），不和程式搶。
            if (mode.type != VT_I4) {
                debugLog(L"棒棒注音：轉換模式被清掉了，稍後重新設定");
                schedulePublishInputMode();
            } else {
                debugLog(L"棒棒注音：轉換模式被改成 0x%X", static_cast<unsigned>(mode.value));
            }
        }
        return S_OK;
    });
}

void TextService::installMessageHook() {
    if (messageHook_) return;
    messageHook_ = SetWindowsHookExW(WH_GETMESSAGE, messageHookProc, nullptr, GetCurrentThreadId());
    if (!messageHook_) debugLog(L"棒棒注音：無法安裝訊息掛鉤（%lu），只能靠 TSF 的按鍵通知偵測 Shift", GetLastError());
}

void TextService::removeMessageHook() {
    if (messageHook_) UnhookWindowsHookEx(messageHook_);
    messageHook_ = nullptr;
}

void TextService::schedulePublishInputMode() {
    if (!republishTimer_) republishTimer_ = SetTimer(nullptr, 0, 0, republishTimerProc);
}

void TextService::onRepublishTimer() {
    republishTimer_ = 0;
    guarded([&]() -> HRESULT {
        publishInputMode();
        return S_OK;
    });
}

void TextService::onThreadMessage(const MSG& message) {
    guarded([&]() -> HRESULT {
        switch (message.message) {
        case WM_KEYDOWN:
        case WM_SYSKEYDOWN:
            shiftTap_.keyDown(static_cast<unsigned>(message.wParam));
            break;
        case WM_KEYUP:
        case WM_SYSKEYUP:
            if (shiftTap_.keyUp(static_cast<unsigned>(message.wParam))) toggleMode();
            break;
        case WM_LBUTTONDOWN:
        case WM_RBUTTONDOWN:
        case WM_MBUTTONDOWN:
        case WM_XBUTTONDOWN:
        case WM_NCLBUTTONDOWN:
        case WM_NCRBUTTONDOWN:
        case WM_NCMBUTTONDOWN:
        case WM_NCXBUTTONDOWN:
            shiftTap_.mouseDown();  // Shift＋點選（選取文字）不算單按
            break;
        default:
            break;
        }
        return S_OK;
    });
}

void TextService::onModeButtonClick(TfLBIClick click, POINT point) {
    if (click == TF_LBI_CLK_LEFT) {
        toggleMode();
    } else {
        showModeMenu(point);
    }
}

void TextService::showModeMenu(POINT point) {
    // 選單要有同一個執行緒的視窗當擁有者。
    HWND owner = GetForegroundWindow();
    if (!owner || GetWindowThreadProcessId(owner, nullptr) != GetCurrentThreadId()) owner = candidateOwner(activeContext_.Get());
    if (!owner) return;
    HMENU menu = CreatePopupMenu();
    if (!menu) return;
    const std::wstring title = L"棒棒注音 " + utf8::toWide(BANGBANG_VERSION_STRING);
    AppendMenuW(menu, MF_STRING | MF_GRAYED, 1, title.c_str());
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, 2, chinese_ ? L"切換成英文（單按 Shift）" : L"切換成中文（單按 Shift）");
    const UINT command = static_cast<UINT>(TrackPopupMenuEx(menu, TPM_RETURNCMD | TPM_NONOTIFY | TPM_RIGHTBUTTON, point.x, point.y, owner, nullptr));
    DestroyMenu(menu);
    if (command == 2) toggleMode();
}

// MARK: - 組字

void TextService::commitComposition() {
    if (!session_) return;
    std::string text;
    text.swap(pendingCommit_);
    if (session_->isComposing()) text += session_->commitAll();
    closeCandidateUI();
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
    updateCandidateUI(ec, context);
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

// MARK: - 候選窗

CandidatePage TextService::currentCandidatePage() const {
    CandidatePage page;
    if (!session_ || !session_->candidateList()) return page;
    const CandidateList& list = *session_->candidateList();
    const auto [first, last] = list.pageRange();
    for (int i = first; i < last; ++i) page.items.push_back(utf8::toWide(list.items[static_cast<std::size_t>(i)].displayText()));
    page.highlighted = list.highlightedInPage();
    page.page = list.page();
    page.pageCount = list.pageCount();
    return page;
}

void TextService::updateCandidateUI(TfEditCookie ec, ITfContext* context) {
    if (!session_ || !session_->candidateList() || !composition_ || compositionContext_.Get() != context) {
        closeCandidateUI();
        return;
    }
    const bool opening = !candidateUIOpen_;
    if (opening) beginCandidateUI();
    candidateAnchor_ = candidateAnchor(ec, context);
    candidateOwner_ = candidateOwner(context);
    if (candidateWindowAllowed_) {
        if (!candidateWindow_) {
            candidateWindow_ = std::make_unique<CandidateWindow>();
            candidateWindow_->setClickHandler([this](int row) { onCandidateClicked(row); });
        }
        candidateWindow_->show(currentCandidatePage(), candidateAnchor_, candidateOwner_);
        adviseLayoutSink(context);
    } else if (candidateWindow_) {
        candidateWindow_->hide();
    }
    publishCandidateUI(opening);
}

void TextService::beginCandidateUI() {
    candidateUIOpen_ = true;
    candidateWindowAllowed_ = true;
    candidateUIId_ = TF_INVALID_UIELEMENTID;
    publishedCandidates_.clear();
    publishedHighlight_ = -1;
    ComPtr<ITfUIElementMgr> uiMgr;
    if (threadMgr_ && SUCCEEDED(threadMgr_.As(&uiMgr))) {
        if (!candidateUI_) candidateUI_.Attach(new CandidateListUI(this));
        BOOL show = TRUE;
        DWORD id = TF_INVALID_UIELEMENTID;
        // 先問程式要不要自己畫候選字（UI-less）：pbShow 回 FALSE 就不顯示我們的候選窗。
        if (SUCCEEDED(uiMgr->BeginUIElement(candidateUI_.Get(), &show, &id))) {
            candidateUIId_ = id;
            candidateWindowAllowed_ = show != FALSE;
        }
    }
    // 程式以「只用 UI-less」啟用 TSF 時，一定由程式自己畫。
    if (activateFlags_ & TF_TMAE_UIELEMENTENABLEDONLY) candidateWindowAllowed_ = false;
}

void TextService::publishCandidateUI(bool opened) {
    if (!candidateUI_ || candidateUIId_ == TF_INVALID_UIELEMENTID) return;
    std::vector<std::wstring> items;
    int highlighted = 0, perPage = 9;
    if (!candidateSnapshot(items, highlighted, perPage)) return;
    DWORD flags = 0;
    if (opened || items != publishedCandidates_) {
        flags = TF_CLUIE_DOCUMENTMGR | TF_CLUIE_COUNT | TF_CLUIE_SELECTION | TF_CLUIE_STRING | TF_CLUIE_PAGEINDEX | TF_CLUIE_CURRENTPAGE;
    } else if (highlighted != publishedHighlight_) {
        flags = TF_CLUIE_SELECTION;
        if (publishedHighlight_ < 0 || highlighted / perPage != publishedHighlight_ / perPage) flags |= TF_CLUIE_CURRENTPAGE;
    }
    publishedCandidates_ = std::move(items);
    publishedHighlight_ = highlighted;
    if (!flags) return;
    candidateUI_->markUpdated(flags);
    ComPtr<ITfUIElementMgr> uiMgr;
    if (threadMgr_ && SUCCEEDED(threadMgr_.As(&uiMgr))) uiMgr->UpdateUIElement(candidateUIId_);
}

void TextService::closeCandidateUI() {
    if (candidateWindow_) candidateWindow_->hide();
    unadviseLayoutSink();
    if (candidateUIOpen_ && candidateUIId_ != TF_INVALID_UIELEMENTID) {
        ComPtr<ITfUIElementMgr> uiMgr;
        if (threadMgr_ && SUCCEEDED(threadMgr_.As(&uiMgr))) uiMgr->EndUIElement(candidateUIId_);
    }
    candidateUIOpen_ = false;
    candidateUIId_ = TF_INVALID_UIELEMENTID;
    candidateWindowAllowed_ = true;
    publishedCandidates_.clear();
    publishedHighlight_ = -1;
}

RECT TextService::candidateAnchor(TfEditCookie ec, ITfContext* context) {
    RECT rect{};
    if (composition_ && session_ && context) {
        if (const auto candidate = session_->candidateRange()) {
            const std::string marked = session_->markedText();
            const auto start = static_cast<LONG>(utf8::utf16Offset(marked, static_cast<std::size_t>(candidate->first)));
            const auto end = static_cast<LONG>(utf8::utf16Offset(marked, static_cast<std::size_t>(candidate->second)));
            ComPtr<ITfRange> range;
            ComPtr<ITfContextView> view;
            if (SUCCEEDED(composition_->GetRange(&range)) && SUCCEEDED(context->GetActiveView(&view))) {
                if (const auto part = subRange(ec, range.Get(), start, end)) {
                    BOOL clipped = FALSE;
                    // 版面還沒排好時會回傳 TS_E_NOLAYOUT；排好後 OnLayoutChange 會再放一次。
                    if (SUCCEEDED(view->GetTextExt(ec, part.Get(), &rect, &clipped)) && (rect.right > rect.left || rect.bottom > rect.top)) {
                        return rect;
                    }
                }
            }
        }
    }
    // 拿不到文字的位置：用系統游標，再不行就用滑鼠位置。
    GUITHREADINFO info{};
    info.cbSize = sizeof(info);
    if (GetGUIThreadInfo(GetCurrentThreadId(), &info) && info.hwndCaret) {
        rect = info.rcCaret;
        MapWindowPoints(info.hwndCaret, nullptr, reinterpret_cast<POINT*>(&rect), 2);
        return rect;
    }
    POINT cursor{};
    GetCursorPos(&cursor);
    return RECT{cursor.x, cursor.y, cursor.x + 1, cursor.y + 20};
}

HWND TextService::candidateOwner(ITfContext* context) const {
    HWND hwnd = nullptr;
    ComPtr<ITfContextView> view;
    if (context && SUCCEEDED(context->GetActiveView(&view))) view->GetWnd(&hwnd);
    if (!hwnd) hwnd = GetFocus();
    if (hwnd) hwnd = GetAncestor(hwnd, GA_ROOT);
    // 別的執行緒的視窗不能當擁有者：會把兩個執行緒的輸入佇列綁在一起。
    if (hwnd && GetWindowThreadProcessId(hwnd, nullptr) != GetCurrentThreadId()) hwnd = nullptr;
    return hwnd;
}

void TextService::adviseLayoutSink(ITfContext* context) {
    if (!context || layoutContext_.Get() == context) return;
    unadviseLayoutSink();
    ComPtr<ITfSource> source;
    DWORD cookie = TF_INVALID_COOKIE;
    if (SUCCEEDED(context->QueryInterface(IID_PPV_ARGS(&source))) &&
        SUCCEEDED(source->AdviseSink(IID_ITfTextLayoutSink, static_cast<ITfTextLayoutSink*>(this), &cookie))) {
        layoutContext_ = context;
        layoutSinkCookie_ = cookie;
    }
}

void TextService::unadviseLayoutSink() {
    if (layoutContext_ && layoutSinkCookie_ != TF_INVALID_COOKIE) {
        ComPtr<ITfSource> source;
        if (SUCCEEDED(layoutContext_.As(&source))) source->UnadviseSink(layoutSinkCookie_);
    }
    layoutContext_.Reset();
    layoutSinkCookie_ = TF_INVALID_COOKIE;
}

STDMETHODIMP TextService::OnLayoutChange(ITfContext* context, TfLayoutCode code, ITfContextView*) {
    return guarded([&]() -> HRESULT {
        if (code == TF_LC_DESTROY || !context || context != compositionContext_.Get() || !candidateWindow_ || !candidateWindow_->isShown()) {
            return S_OK;
        }
        // 版面改變（例如 Chrome、Word 晚一步排好版）：在唯讀的 edit session 裡重新取位置。
        const ComPtr<TextService> self(this);
        const ComPtr<ITfContext> target(context);
        requestEditSession(context, clientId_, TF_ES_ASYNCDONTCARE | TF_ES_READ, [self, target](TfEditCookie ec) {
            self->repositionCandidates(ec, target.Get());
            return S_OK;
        });
        return S_OK;
    });
}

void TextService::repositionCandidates(TfEditCookie ec, ITfContext* context) {
    if (context != compositionContext_.Get() || !candidateWindow_ || !candidateWindow_->isShown()) return;
    const RECT anchor = candidateAnchor(ec, context);
    if (EqualRect(&anchor, &candidateAnchor_)) return;
    candidateAnchor_ = anchor;
    candidateWindow_->move(anchor);
}

void TextService::onCandidateClicked(int row) {
    guarded([&]() -> HRESULT {
        if (!session_ || !session_->candidateList() || !engine_) return S_OK;
        const int index = session_->candidateList()->pageRange().first + row;
        {
            std::lock_guard lock(engine_->mutex);
            if (!session_->chooseCandidate(index)) return S_OK;
        }
        if (activeContext_) requestUpdate(activeContext_.Get());
        return S_OK;
    });
}

// MARK: - CandidateListHost（UI-less）

bool TextService::candidateSnapshot(std::vector<std::wstring>& items, int& highlighted, int& perPage) {
    items.clear();
    if (!session_ || !session_->candidateList()) return false;
    const CandidateList& list = *session_->candidateList();
    for (const auto& candidate : list.items) items.push_back(utf8::toWide(candidate.displayText()));
    highlighted = list.highlighted;
    perPage = list.perPage;
    return true;
}

HRESULT TextService::candidateDocumentMgr(ITfDocumentMgr** documentMgr) {
    *documentMgr = nullptr;
    const ComPtr<ITfContext> context = compositionContext_ ? compositionContext_ : activeContext_;
    return context ? context->GetDocumentMgr(documentMgr) : E_FAIL;
}

void TextService::candidateShowRequested(bool show) {
    candidateWindowAllowed_ = show;
    if (!show) {
        if (candidateWindow_) candidateWindow_->hide();
        return;
    }
    if (!candidateUIOpen_ || !session_ || !session_->candidateList()) return;
    if (!candidateWindow_) {
        candidateWindow_ = std::make_unique<CandidateWindow>();
        candidateWindow_->setClickHandler([this](int row) { onCandidateClicked(row); });
    }
    candidateWindow_->show(currentCandidatePage(), candidateAnchor_, candidateOwner_);
    adviseLayoutSink(compositionContext_.Get());
}

bool TextService::candidateWindowShown() {
    return candidateWindow_ && candidateWindow_->isShown();
}

HRESULT TextService::candidateHighlight(int index) {
    if (!session_ || !engine_) return E_FAIL;
    {
        std::lock_guard lock(engine_->mutex);
        if (!session_->highlightCandidate(index)) return E_INVALIDARG;
    }
    if (activeContext_) requestUpdate(activeContext_.Get());
    return S_OK;
}

HRESULT TextService::candidateFinalize() {
    if (!session_ || !engine_ || !session_->candidateList()) return E_FAIL;
    {
        std::lock_guard lock(engine_->mutex);
        session_->chooseCandidate(session_->candidateList()->highlighted);
    }
    if (activeContext_) requestUpdate(activeContext_.Get());
    return S_OK;
}

HRESULT TextService::candidateAbort() {
    if (!session_ || !engine_ || !session_->candidateList()) return E_FAIL;
    {
        std::lock_guard lock(engine_->mutex);
        session_->handle(KeyEvent::of(KeyEvent::Type::Escape));  // 候選窗開著時，Esc 只關閉候選窗
    }
    if (activeContext_) requestUpdate(activeContext_.Get());
    return S_OK;
}

}  // namespace bangbang
