#pragma once

// 對應 Sources/BangbangZhuyin/InputController.swift：把按鍵交給 InputSession，再把結果顯示到應用程式裡。
// TSF 會為每個 UI 執行緒建立一個 TextService；選字引擎與記憶由同一個程式裡的所有 TextService 共用（SharedEngine）。

#include <windows.h>
#include <msctf.h>
#include <wrl/client.h>

#include <array>
#include <memory>
#include <string>
#include <vector>

#include "CandidateListUI.h"
#include "CandidateRenderer.h"
#include "InputSession.h"
#include "ShiftTapDetector.h"

namespace bangbang {

struct SharedEngine;
class CandidateWindow;
class InputModeButton;

class TextService final : public ITfTextInputProcessorEx,
                          public ITfThreadMgrEventSink,
                          public ITfThreadFocusSink,
                          public ITfActiveLanguageProfileNotifySink,
                          public ITfKeyEventSink,
                          public ITfCompositionSink,
                          public ITfDisplayAttributeProvider,
                          public ITfTextLayoutSink,
                          public ITfCompartmentEventSink,
                          private CandidateListHost {
public:
    /// 組字區最多幾個字，超過就自動送出最前面的詞（Mac 版是 20；2026-11-23 使用者要求 Windows 版改成 30）。
    static constexpr int kMaxCompositionLength = 30;

    TextService();

    /// 這個執行緒的訊息掛鉤收到的訊息（偵測單按 Shift、Shift＋滑鼠）。內部使用。
    void onThreadMessage(const MSG& message);
    /// 延後重新設定輸入模式的計時器到了。內部使用。
    void onRepublishTimer();

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

    // ITfTextLayoutSink：程式的版面改變（例如 Chrome 晚一步排好版）時，重新放候選窗。
    STDMETHODIMP OnLayoutChange(ITfContext* context, TfLayoutCode code, ITfContextView* view) override;

    // ITfCompartmentEventSink：中英狀態（全域）、這個執行緒的輸入法開關與轉換模式被改變時。
    STDMETHODIMP OnChange(REFGUID guid) override;

private:
    ~TextService();

    // CandidateListHost（UI-less 模式的候選字介面向這裡查詢與操作）
    bool candidateSnapshot(std::vector<std::wstring>& items, int& highlighted, int& perPage) override;
    HRESULT candidateDocumentMgr(ITfDocumentMgr** documentMgr) override;
    void candidateShowRequested(bool show) override;
    bool candidateWindowShown() override;
    HRESULT candidateHighlight(int index) override;
    HRESULT candidateFinalize() override;
    HRESULT candidateAbort() override;

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

    /// 把這個執行緒目前的中英狀態寫到 GUID_COMPARTMENT_KEYBOARD_OPENCLOSE 與 INPUTMODE_CONVERSION。
    void publishInputMode();
    /// 等程式的訊息迴圈下一輪再 publishInputMode：在某個 compartment 的變更通知裡不能改它（E_UNEXPECTED）。
    void schedulePublishInputMode();

    // 中英切換（第 5 階段）。全域 compartment kChineseModeCompartment 是所有程式共用的狀態。
    Microsoft::WRL::ComPtr<ITfCompartment> compartment(REFGUID guid, bool global) const;
    bool readGlobalMode() const;
    /// 改變所有程式共用的中英狀態（單按 Shift、點工作列圖示）。這個執行緒馬上套用，其他程式收到通知後套用。
    void setGlobalMode(bool chinese);
    void toggleMode();
    /// 只改這個執行緒的中英狀態，並更新 compartment 與工作列圖示。byUser：使用者自己切換（單按 Shift、點圖示、
    /// 別的程式同步過來）時組字區留著；程式自己關掉輸入法時先送出組字區。
    void applyMode(bool chinese, bool byUser);
    void adviseCompartmentSinks();
    void unadviseCompartmentSinks();
    void installMessageHook();
    void removeMessageHook();
    void onModeButtonClick(TfLBIClick click, POINT point);
    void showModeMenu(POINT point);
    /// 送出整個組字區（切換應用程式、切換輸入法、按 Caps Lock 時）。
    void commitComposition();
    /// 要求一個 edit session，把引擎目前的狀態畫到文件上。
    void requestUpdate(ITfContext* context);
    HRESULT updateDocument(TfEditCookie ec, ITfContext* context);
    bool startComposition(TfEditCookie ec, ITfContext* context);
    void showComposition(TfEditCookie ec, ITfContext* context);
    void finishComposition(TfEditCookie ec, ITfContext* context, const std::wstring& text);
    void forgetComposition();

    // 候選窗（第 4 階段）
    /// 在 edit session 裡依引擎的候選狀態開啟、更新或關閉候選窗與 UI 元件。
    void updateCandidateUI(TfEditCookie ec, ITfContext* context);
    /// 關閉候選窗、結束 UI 元件（送出組字、換焦點、停用時）。
    void closeCandidateUI();
    void beginCandidateUI();
    /// 通知 UI-less 的程式候選內容變了（ITfUIElementMgr::UpdateUIElement）。
    void publishCandidateUI(bool opened);
    CandidatePage currentCandidatePage() const;
    /// 正在選字那一段文字的螢幕座標；拿不到時退回系統游標，再退回滑鼠位置。
    RECT candidateAnchor(TfEditCookie ec, ITfContext* context);
    /// 候選窗的擁有者：文件所在的頂層視窗（必須是同一個執行緒的視窗）。
    HWND candidateOwner(ITfContext* context) const;
    void repositionCandidates(TfEditCookie ec, ITfContext* context);
    void adviseLayoutSink(ITfContext* context);
    void unadviseLayoutSink();
    /// 使用者用滑鼠點了候選窗本頁第 row 列。
    void onCandidateClicked(int row);

    LONG references_ = 1;
    Microsoft::WRL::ComPtr<ITfThreadMgr> threadMgr_;
    TfClientId clientId_ = TF_CLIENTID_NULL;
    DWORD activateFlags_ = 0;
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

    // 候選窗
    std::unique_ptr<CandidateWindow> candidateWindow_;
    Microsoft::WRL::ComPtr<CandidateListUI> candidateUI_;
    /// 候選字的 UI 元件已經 BeginUIElement（候選窗開著）。
    bool candidateUIOpen_ = false;
    DWORD candidateUIId_ = TF_INVALID_UIELEMENTID;
    /// 程式是否讓我們自己畫候選窗（BeginUIElement 的 pbShow，之後可由 ITfUIElement::Show 改變）。
    bool candidateWindowAllowed_ = true;
    RECT candidateAnchor_{};
    HWND candidateOwner_ = nullptr;
    /// 上一次告訴 UI-less 程式的內容，用來判斷 GetUpdatedFlags 要回報哪些變更。
    std::vector<std::wstring> publishedCandidates_;
    int publishedHighlight_ = -1;
    /// 註冊了 ITfTextLayoutSink 的文件。
    Microsoft::WRL::ComPtr<ITfContext> layoutContext_;
    DWORD layoutSinkCookie_ = TF_INVALID_COOKIE;

    // 中英切換
    /// 這個執行緒目前是不是中文模式（通常等於全域狀態；程式自己關掉輸入法時只有這個執行緒改變）。
    bool chinese_ = true;
    /// 正在寫自己的 compartment：忽略它們觸發的通知。
    bool writingCompartments_ = false;
    ShiftTapDetector shiftTap_;
    HHOOK messageHook_ = nullptr;
    UINT_PTR republishTimer_ = 0;
    DWORD globalModeSinkCookie_ = TF_INVALID_COOKIE;
    DWORD openCloseSinkCookie_ = TF_INVALID_COOKIE;
    DWORD conversionSinkCookie_ = TF_INVALID_COOKIE;
    Microsoft::WRL::ComPtr<InputModeButton> modeButton_;
};

}  // namespace bangbang
