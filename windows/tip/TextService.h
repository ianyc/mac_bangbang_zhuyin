#pragma once

// 對應 Sources/BangbangZhuyin/InputController.swift：把按鍵交給 InputSession，再把結果顯示到應用程式裡。
// TSF 會為每個 UI 執行緒建立一個 TextService；選字引擎與記憶由同一個程式裡的所有 TextService 共用（SharedEngine）。

#include <windows.h>
#include <msctf.h>
#include <wrl/client.h>

#include <array>
#include <memory>
#include <string>

#include "InputSession.h"

namespace bangbang {

struct SharedEngine;

class TextService final : public ITfTextInputProcessorEx,
                          public ITfThreadMgrEventSink,
                          public ITfThreadFocusSink,
                          public ITfActiveLanguageProfileNotifySink,
                          public ITfKeyEventSink,
                          public ITfCompositionSink,
                          public ITfDisplayAttributeProvider {
public:
    TextService();

    // IUnknown
    STDMETHODIMP QueryInterface(REFIID riid, void** ppv) override;
    STDMETHODIMP_(ULONG) AddRef() override;
    STDMETHODIMP_(ULONG) Release() override;

    // ITfTextInputProcessor / ITfTextInputProcessorEx
    STDMETHODIMP Activate(ITfThreadMgr* threadMgr, TfClientId clientId) override;
    STDMETHODIMP ActivateEx(ITfThreadMgr* threadMgr, TfClientId clientId, DWORD flags) override;
    STDMETHODIMP Deactivate() override;

    // ITfThreadMgrEventSink
    STDMETHODIMP OnInitDocumentMgr(ITfDocumentMgr* documentMgr) override;
    STDMETHODIMP OnUninitDocumentMgr(ITfDocumentMgr* documentMgr) override;
    STDMETHODIMP OnSetFocus(ITfDocumentMgr* focus, ITfDocumentMgr* previous) override;
    STDMETHODIMP OnPushContext(ITfContext* context) override;
    STDMETHODIMP OnPopContext(ITfContext* context) override;

    // ITfThreadFocusSink
    STDMETHODIMP OnSetThreadFocus() override;
    STDMETHODIMP OnKillThreadFocus() override;

    // ITfActiveLanguageProfileNotifySink
    STDMETHODIMP OnActivated(REFCLSID clsid, REFGUID profile, BOOL activated) override;

    // ITfKeyEventSink
    STDMETHODIMP OnSetFocus(BOOL foreground) override;
    STDMETHODIMP OnTestKeyDown(ITfContext* context, WPARAM wParam, LPARAM lParam, BOOL* eaten) override;
    STDMETHODIMP OnTestKeyUp(ITfContext* context, WPARAM wParam, LPARAM lParam, BOOL* eaten) override;
    STDMETHODIMP OnKeyDown(ITfContext* context, WPARAM wParam, LPARAM lParam, BOOL* eaten) override;
    STDMETHODIMP OnKeyUp(ITfContext* context, WPARAM wParam, LPARAM lParam, BOOL* eaten) override;
    STDMETHODIMP OnPreservedKey(ITfContext* context, REFGUID guid, BOOL* eaten) override;

    // ITfCompositionSink
    STDMETHODIMP OnCompositionTerminated(TfEditCookie ec, ITfComposition* composition) override;

    // ITfDisplayAttributeProvider
    STDMETHODIMP EnumDisplayAttributeInfo(IEnumTfDisplayAttributeInfo** enumerator) override;
    STDMETHODIMP GetDisplayAttributeInfo(REFGUID guid, ITfDisplayAttributeInfo** info) override;

private:
    ~TextService();

    /// 按鍵的處理方式。
    enum class KeyAction {
        PassThrough,     // 交給應用程式
        Handle,          // 交給 InputSession
        Swallow,         // 組字中按到沒有對應的鍵：吃掉不處理
        CommitThenPass,  // 先送出組字區，再把按鍵交給應用程式（Caps Lock）
    };
    struct KeyDecision {
        KeyAction action = KeyAction::PassThrough;
        KeyEvent key;
    };
    KeyDecision decide(ITfContext* context, WPARAM wParam, LPARAM lParam) const;

    /// 告訴系統輸入法目前開著、是中文模式（GUID_COMPARTMENT_KEYBOARD_OPENCLOSE 與 INPUTMODE_CONVERSION）。
    void publishInputMode();
    /// 送出整個組字區（切換應用程式、切換輸入法、按 Caps Lock 時）。
    void commitComposition();
    /// 要求一個 edit session，把引擎目前的狀態畫到文件上。
    void requestUpdate(ITfContext* context);
    HRESULT updateDocument(TfEditCookie ec, ITfContext* context);
    bool startComposition(TfEditCookie ec, ITfContext* context);
    void showComposition(TfEditCookie ec, ITfContext* context);
    void finishComposition(TfEditCookie ec, ITfContext* context, const std::wstring& text);
    void forgetComposition();

    LONG references_ = 1;
    Microsoft::WRL::ComPtr<ITfThreadMgr> threadMgr_;
    TfClientId clientId_ = TF_CLIENTID_NULL;
    DWORD threadMgrSinkCookie_ = TF_INVALID_COOKIE;
    DWORD threadFocusSinkCookie_ = TF_INVALID_COOKIE;
    DWORD profileSinkCookie_ = TF_INVALID_COOKIE;
    bool keySinkAdvised_ = false;
    TfGuidAtom inputAtom_ = TF_INVALID_GUIDATOM;
    TfGuidAtom convertedAtom_ = TF_INVALID_GUIDATOM;

    SharedEngine* engine_ = nullptr;
    std::unique_ptr<InputSession> session_;
    /// 目前的組字（底線那一段）與它所在的文件。
    Microsoft::WRL::ComPtr<ITfComposition> composition_;
    Microsoft::WRL::ComPtr<ITfContext> compositionContext_;
    /// 最近一次處理按鍵的文件：非同步的 edit session 只更新這個文件。
    Microsoft::WRL::ComPtr<ITfContext> activeContext_;
    /// 引擎已經送出、但還沒畫到文件上的文字（edit session 可能是非同步的）。
    std::string pendingCommit_;
    /// 哪些鍵按下時被吃掉了：放開時也要吃掉，其餘的放開事件都交給應用程式。
    std::array<bool, 256> eatenKeyDown_{};
};

}  // namespace bangbang
