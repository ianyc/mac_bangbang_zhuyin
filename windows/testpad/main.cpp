// testpad：測試棒棒注音用的小程式（x64、x86 各一，x86 版用來測 32 位元程式）。
//
//   testpad.exe              打開測試視窗：上面是 RichEdit（直接使用 TSF），下面是一般的 EDIT（透過 IMM32 相容層）。
//   testpad.exe --selftest   自動測試：在 RichEdit 上啟用棒棒注音，用 ITfKeystrokeMgr 模擬按鍵，
//                            逐鍵比對「輸入法有沒有吃掉這個鍵」，最後比對文件裡的文字和選字引擎算出的結果。
//                            需要先用 windows\scripts\register.ps1 註冊輸入法。測試時不要點別的視窗：
//                            測試視窗不在前景時，按鍵不會交給輸入法，那幾項會標成「略過」。
//                            結束代碼：0 全部通過；1 有失敗；2 無法開始；3 沒有失敗但有略過。
//                            也檢查候選窗（顯示位置、不搶焦點、UI 元件的內容）與 UI-less 模式。
//   testpad.exe --render-candidates <資料夾>
//                            把幾種候選窗內容畫成 PNG（淺色／深色、100%／150%），檢查外觀用，不需要輸入法。
#include <windows.h>
#include <ctffunc.h>
#include <msctf.h>
#include <richedit.h>
#include <dwmapi.h>
#include <wincodec.h>
#include <wrl/client.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cwchar>
#include <filesystem>
#include <map>
#include <optional>
#include <string>
#include <vector>

#include "BangbangVersion.h"
#include "CandidateRenderer.h"
#include "CandidateWindow.h"
#include "Guids.h"
#include "Globals.h"
#include "InputSession.h"
#include "LanguageModel.h"
#include "TextService.h"
#include "UserModel.h"
#include "Utf8.h"

// 和輸入法 DLL 共用的 CandidateWindow.cpp 需要這兩個（DLL 裡在 Globals.cpp）。
namespace bangbang {
HINSTANCE g_module = GetModuleHandleW(nullptr);
void debugLog(const wchar_t* format, ...) {
    va_list args;
    va_start(args, format);
    std::vfwprintf(stderr, format, args);
    va_end(args);
    std::fputwc(L'\n', stderr);
}
}  // namespace bangbang

using Microsoft::WRL::ComPtr;
using namespace bangbang;

namespace {

#if defined(_WIN64)
constexpr const wchar_t* kArch = L"x64";
#else
constexpr const wchar_t* kArch = L"x86";
#endif

HWND g_richEdit = nullptr;
HWND g_plainEdit = nullptr;
/// --probe-compartments：自動測試前先印出 compartment 的診斷資訊。
bool g_probeCompartments = false;

LRESULT CALLBACK windowProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
    switch (message) {
    case WM_SIZE: {
        const int w = LOWORD(lParam), h = HIWORD(lParam);
        const int split = h * 2 / 3;
        MoveWindow(g_richEdit, 8, 8, w - 16, split - 12, TRUE);
        MoveWindow(g_plainEdit, 8, split + 4, w - 16, h - split - 12, TRUE);
        return 0;
    }
    case WM_SETFOCUS:
        SetFocus(g_richEdit);
        return 0;
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    default:
        return DefWindowProcW(hwnd, message, wParam, lParam);
    }
}

HWND createTestWindow() {
    LoadLibraryW(L"Msftedit.dll");
    const HINSTANCE instance = GetModuleHandleW(nullptr);
    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = windowProc;
    wc.hInstance = instance;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
    wc.lpszClassName = L"BangbangTestpad";
    RegisterClassExW(&wc);
    const std::wstring title = std::wstring(L"棒棒注音測試板（") + kArch + L"）— 上：RichEdit（TSF）／下：EDIT（IMM32 相容層）";
    HWND window = CreateWindowExW(0, wc.lpszClassName, title.c_str(), WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT, 960, 640,
                                  nullptr, nullptr, instance, nullptr);
    const DWORD editStyle = WS_CHILD | WS_VISIBLE | WS_VSCROLL | ES_MULTILINE | ES_AUTOVSCROLL | ES_WANTRETURN;
    g_richEdit = CreateWindowExW(WS_EX_CLIENTEDGE, MSFTEDIT_CLASS, L"", editStyle, 0, 0, 0, 0, window, nullptr, instance, nullptr);
    SendMessageW(g_richEdit, EM_SETEDITSTYLE, SES_USECTF, SES_USECTF);
    g_plainEdit = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"", editStyle, 0, 0, 0, 0, window, nullptr, instance, nullptr);
    const HFONT font = CreateFontW(-26, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                                   CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Microsoft JhengHei UI");
    SendMessageW(g_richEdit, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
    SendMessageW(g_plainEdit, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
    RECT rc{};
    GetClientRect(window, &rc);
    SendMessageW(window, WM_SIZE, 0, MAKELPARAM(rc.right, rc.bottom));
    return window;
}

void pumpMessages(int rounds = 3) {
    for (int i = 0; i < rounds; ++i) {
        MSG msg;
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
        Sleep(2);
    }
}

std::string windowText(HWND hwnd) {
    const int length = GetWindowTextLengthW(hwnd);
    std::wstring text(static_cast<std::size_t>(length) + 1, L'\0');
    GetWindowTextW(hwnd, text.data(), length + 1);
    text.resize(static_cast<std::size_t>(length));
    return utf8::fromWide(text);
}

int runInteractive() {
    FreeConsole();
    HWND window = createTestWindow();
    ShowWindow(window, SW_SHOWNORMAL);
    SetFocus(g_richEdit);
    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    return 0;
}

// MARK: - 自動測試

/// 一個模擬按鍵：要送給 TSF 的虛擬鍵碼與 Shift 狀態，以及引擎應該收到的 KeyEvent（標準答案）。
/// check 不是空字串時，這一步不是按鍵，而是檢查或操作（見 parseKeys 的 <?…>）。
struct ScriptKey {
    UINT vk = 0;
    bool shift = false;
    KeyEvent event;
    std::string label;
    std::string check;
    bool ctrl = false;
};


std::optional<UINT> virtualKeyFor(char c) {
    if (c >= 'a' && c <= 'z') return static_cast<UINT>('A' + (c - 'a'));
    if (c >= '0' && c <= '9') return static_cast<UINT>(c);
    switch (c) {
    case '-': return VK_OEM_MINUS;
    case '=': return VK_OEM_PLUS;
    case '[': return VK_OEM_4;
    case ']': return VK_OEM_6;
    case '\\': return VK_OEM_5;
    case ';': return VK_OEM_1;
    case '\'': return VK_OEM_7;
    case ',': return VK_OEM_COMMA;
    case '.': return VK_OEM_PERIOD;
    case '/': return VK_OEM_2;
    case '`': return VK_OEM_3;
    default: return std::nullopt;
    }
}

/// 按鍵腳本，格式同 bangbang-tool keys：小寫字母、數字、標點是一般按鍵，大寫字母是 Shift+字母，空白是空白鍵；
/// <enter> <esc> <bs> <del> <left> <right> <up> <down> <home> <end> <tab> <space>、
/// <num0>～<num9>（數字鍵盤）、<S-x>（Shift+x）、<C-x>（Ctrl+x）。
/// 另外有檢查與操作（不送按鍵）：
///   <?cand>   候選窗顯示中、不在游標那一行上、沒有搶走焦點　　<?nocand>  候選窗沒有顯示
///   <?list>   候選字的 UI 元件內容（數量、每一個字、反白、頁碼）和引擎相同
///   <?uiless> 這一題扮演自己畫候選字的程式（BeginUIElement 回答不要輸入法畫）
///   <?ui-select:N>、<?ui-finalize>、<?ui-abort>  透過 ITfCandidateListUIElementBehavior 操作
/// 中英切換（第 5 階段）：
///   <shift>   單按 Shift　　<shift-down>、<shift-up>  分開按下、放開　　<click>  按一下滑鼠（Shift＋點選）
///   <?mode:zh>、<?mode:en>  這個程式與全域都是中文／英文　　<?mode:en-local>  只有這個程式是英文
///   <?set-open:0|1>  扮演程式關掉／打開輸入法　　<?lbi-click>  點工作列的「中／英」圖示
std::vector<ScriptKey> parseKeys(std::string_view script) {
    using T = KeyEvent::Type;
    static const std::map<std::string, std::pair<UINT, T>> specials = {
        {"space", {VK_SPACE, T::Space}}, {"enter", {VK_RETURN, T::Enter}}, {"esc", {VK_ESCAPE, T::Escape}},
        {"bs", {VK_BACK, T::Backspace}}, {"del", {VK_DELETE, T::ForwardDelete}}, {"left", {VK_LEFT, T::Left}},
        {"right", {VK_RIGHT, T::Right}}, {"up", {VK_UP, T::Up}}, {"down", {VK_DOWN, T::Down}},
        {"home", {VK_HOME, T::Home}}, {"end", {VK_END, T::End}}, {"tab", {VK_TAB, T::Tab}},
    };
    std::vector<ScriptKey> keys;
    for (std::size_t i = 0; i < script.size();) {
        if (script[i] == '<') {
            const auto close = script.find('>', i);
            const std::string name(script.substr(i + 1, close - i - 1));
            const std::string label(script.substr(i, close - i + 1));
            i = close + 1;
            if (!name.empty() && name[0] == '?') {
                keys.push_back({0, false, KeyEvent{}, label, name.substr(1)});
            } else if (name == "shift" || name == "shift-down" || name == "shift-up" || name == "click") {
                keys.push_back({0, false, KeyEvent{}, label, "!" + name});  // 動作：見 runSelfTest
            } else if (const auto it = specials.find(name); it != specials.end()) {
                keys.push_back({it->second.first, false, KeyEvent::of(it->second.second), label});
            } else if (name.size() == 4 && name.rfind("num", 0) == 0) {
                const char digit = name[3];
                keys.push_back({static_cast<UINT>(VK_NUMPAD0 + (digit - '0')), false, KeyEvent::numpad(digit), label});
            } else if (name.size() == 3 && name.rfind("S-", 0) == 0) {
                keys.push_back({*virtualKeyFor(name[2]), true, KeyEvent::character(name[2], true), label});
            } else if (name.size() == 3 && name.rfind("C-", 0) == 0) {
                ScriptKey key{*virtualKeyFor(name[2]), false, KeyEvent::control(name[2]), label};
                key.ctrl = true;
                keys.push_back(key);
            }
            continue;
        }
        const char c = script[i++];
        if (c == ' ') {
            keys.push_back({VK_SPACE, false, KeyEvent::of(T::Space), " "});
        } else if (c >= 'A' && c <= 'Z') {
            const char lower = static_cast<char>(c - 'A' + 'a');
            keys.push_back({*virtualKeyFor(lower), true, KeyEvent::character(lower, true), std::string(1, c)});
        } else {
            keys.push_back({*virtualKeyFor(c), false, KeyEvent::character(c), std::string(1, c)});
        }
    }
    return keys;
}

bool isExtendedKey(UINT vk) {
    switch (vk) {
    case VK_LEFT: case VK_RIGHT: case VK_UP: case VK_DOWN:
    case VK_HOME: case VK_END: case VK_PRIOR: case VK_NEXT: case VK_INSERT: case VK_DELETE:
        return true;
    default:
        return false;
    }
}

/// 透過 TSF 的按鍵管理員送出一個按鍵（按下、放開），回傳按下時有沒有被輸入法吃掉。
bool sendKey(ITfKeystrokeMgr* keystrokes, const ScriptKey& key) {
    BYTE state[256] = {};
    GetKeyboardState(state);
    const BYTE shift = key.shift ? 0x80 : 0;
    const BYTE ctrl = key.ctrl ? 0x80 : 0;
    state[VK_SHIFT] = shift;
    state[VK_LSHIFT] = shift;
    state[VK_RSHIFT] = 0;
    state[VK_CONTROL] = state[VK_LCONTROL] = ctrl;
    state[VK_RCONTROL] = 0;
    state[VK_MENU] = state[VK_LMENU] = state[VK_RMENU] = 0;
    state[VK_CAPITAL] = 0;  // Caps Lock 關閉
    SetKeyboardState(state);

    const UINT scan = MapVirtualKeyW(key.vk, MAPVK_VK_TO_VSC);
    const LPARAM down = 1 | (static_cast<LPARAM>(scan) << 16) | (isExtendedKey(key.vk) ? (static_cast<LPARAM>(1) << 24) : 0);
    const LPARAM up = down | (static_cast<LPARAM>(1) << 30) | static_cast<LPARAM>(static_cast<ULONG_PTR>(1) << 31);
    BOOL eaten = FALSE;
    keystrokes->TestKeyDown(key.vk, down, &eaten);
    if (eaten) keystrokes->KeyDown(key.vk, down, &eaten);
    pumpMessages();
    BOOL eatenUp = FALSE;
    keystrokes->TestKeyUp(key.vk, up, &eatenUp);
    if (eatenUp) keystrokes->KeyUp(key.vk, up, &eatenUp);
    pumpMessages();

    state[VK_SHIFT] = state[VK_LSHIFT] = 0;
    state[VK_CONTROL] = state[VK_LCONTROL] = 0;
    SetKeyboardState(state);
    return eaten != FALSE;
}

/// 送出 Shift 的按下或放開（中英切換用）。回傳輸入法有沒有吃掉（Shift 應該永遠交給程式）。
bool sendShift(ITfKeystrokeMgr* keystrokes, bool down) {
    BYTE state[256] = {};
    GetKeyboardState(state);
    state[VK_SHIFT] = state[VK_LSHIFT] = down ? 0x80 : 0;
    state[VK_RSHIFT] = 0;
    SetKeyboardState(state);
    const UINT scan = MapVirtualKeyW(VK_LSHIFT, MAPVK_VK_TO_VSC);
    LPARAM lParam = 1 | (static_cast<LPARAM>(scan) << 16);
    BOOL eaten = FALSE;
    if (down) {
        keystrokes->TestKeyDown(VK_SHIFT, lParam, &eaten);
        if (eaten) keystrokes->KeyDown(VK_SHIFT, lParam, &eaten);
    } else {
        lParam |= (static_cast<LPARAM>(1) << 30) | static_cast<LPARAM>(static_cast<ULONG_PTR>(1) << 31);
        keystrokes->TestKeyUp(VK_SHIFT, lParam, &eaten);
        if (eaten) keystrokes->KeyUp(VK_SHIFT, lParam, &eaten);
    }
    pumpMessages();
    return eaten != FALSE;
}

/// 測試視窗是否在前景：不在前景時 RichEdit 沒有 TSF 焦點，按鍵不會交給輸入法。
/// 不在的話試著拉回來（Windows 可能不允許），拉不回來就回傳 false。
bool ensureForeground(HWND window, HWND target) {
    if (GetForegroundWindow() != window) {
        SetForegroundWindow(window);
        pumpMessages(10);
        if (GetForegroundWindow() != window) return false;
    }
    SetFocus(target);
    pumpMessages();
    return true;
}

/// compartment 的值與型別（VT_EMPTY 表示沒有人設定過）。
struct CompartmentValue {
    VARTYPE type = VT_EMPTY;
    LONG value = 0;
};

/// owner：執行緒管理員，或全域的 compartment 管理員。
CompartmentValue readCompartment(IUnknown* owner, REFGUID guid) {
    CompartmentValue result;
    ComPtr<ITfCompartmentMgr> compartments;
    ComPtr<ITfCompartment> compartment;
    VARIANT value;
    VariantInit(&value);
    if (SUCCEEDED(owner->QueryInterface(IID_PPV_ARGS(&compartments))) && SUCCEEDED(compartments->GetCompartment(guid, &compartment)) &&
        SUCCEEDED(compartment->GetValue(&value))) {
        result.type = value.vt;
        if (value.vt == VT_I4) result.value = value.lVal;
    }
    VariantClear(&value);
    return result;
}

/// 診斷用（--probe-compartments）：列出執行緒與全域 compartment 裡的開關、轉換模式，
/// 再以 testpad 自己的身分寫入轉換模式，看寫不寫得進去、之後會不會被清掉。
void probeCompartments(ITfThreadMgr* threadMgr) {
    ComPtr<ITfCompartmentMgr> global;
    threadMgr->GetGlobalCompartment(&global);
    const auto show = [&](const char* label) {
        const CompartmentValue open = readCompartment(threadMgr, GUID_COMPARTMENT_KEYBOARD_OPENCLOSE);
        const CompartmentValue conversion = readCompartment(threadMgr, GUID_COMPARTMENT_KEYBOARD_INPUTMODE_CONVERSION);
        CompartmentValue globalOpen, globalConversion;
        if (global) {
            globalOpen = readCompartment(global.Get(), GUID_COMPARTMENT_KEYBOARD_OPENCLOSE);
            globalConversion = readCompartment(global.Get(), GUID_COMPARTMENT_KEYBOARD_INPUTMODE_CONVERSION);
        }
        std::printf("  （診斷）%s：執行緒 開關 %ld/型別 %u、轉換 0x%lX/型別 %u；全域 開關 %ld/型別 %u、轉換 0x%lX/型別 %u\n", label,
                    static_cast<long>(open.value), static_cast<unsigned>(open.type), static_cast<unsigned long>(conversion.value),
                    static_cast<unsigned>(conversion.type), static_cast<long>(globalOpen.value), static_cast<unsigned>(globalOpen.type),
                    static_cast<unsigned long>(globalConversion.value), static_cast<unsigned>(globalConversion.type));
    };
    show("目前");
    TfClientId client = TF_CLIENTID_NULL;
    const HRESULT activated = threadMgr->Activate(&client);
    HRESULT written = E_FAIL;
    ComPtr<ITfCompartmentMgr> compartments;
    ComPtr<ITfCompartment> compartment;
    if (SUCCEEDED(activated) && SUCCEEDED(threadMgr->QueryInterface(IID_PPV_ARGS(&compartments))) &&
        SUCCEEDED(compartments->GetCompartment(GUID_COMPARTMENT_KEYBOARD_INPUTMODE_CONVERSION, &compartment))) {
        VARIANT value;
        VariantInit(&value);
        value.vt = VT_I4;
        value.lVal = TF_CONVERSIONMODE_NATIVE;
        written = compartment->SetValue(client, &value);
    }
    std::printf("  （診斷）testpad 寫入轉換模式 0x1：Activate 0x%08lX、SetValue 0x%08lX\n", static_cast<unsigned long>(activated),
                static_cast<unsigned long>(written));
    show("剛寫完");
    pumpMessages(30);
    show("處理訊息後");
    if (SUCCEEDED(activated)) threadMgr->Deactivate();
}

// MARK: - 候選窗的檢查

/// 扮演「程式」決定要不要讓輸入法自己畫候選窗（ITfUIElementSink）；drawItself 為 true 時就是 UI-less 模式。
class UIElementSink final : public ITfUIElementSink {
public:
    bool drawItself = false;
    DWORD current = TF_INVALID_UIELEMENTID;

    STDMETHODIMP QueryInterface(REFIID riid, void** ppv) override {
        if (!ppv) return E_POINTER;
        *ppv = nullptr;
        if (!IsEqualIID(riid, IID_IUnknown) && !IsEqualIID(riid, IID_ITfUIElementSink)) return E_NOINTERFACE;
        *ppv = static_cast<ITfUIElementSink*>(this);
        AddRef();
        return S_OK;
    }
    STDMETHODIMP_(ULONG) AddRef() override { return ++references_; }
    STDMETHODIMP_(ULONG) Release() override {
        const ULONG remaining = --references_;
        if (remaining == 0) delete this;
        return remaining;
    }
    STDMETHODIMP BeginUIElement(DWORD id, BOOL* show) override {
        current = id;
        if (show) *show = drawItself ? FALSE : TRUE;
        return S_OK;
    }
    STDMETHODIMP UpdateUIElement(DWORD) override { return S_OK; }
    STDMETHODIMP EndUIElement(DWORD id) override {
        if (id == current) current = TF_INVALID_UIELEMENTID;
        return S_OK;
    }

private:
    ULONG references_ = 1;
};

HWND findCandidateWindow() {
    HWND found = nullptr;
    EnumThreadWindows(
        GetCurrentThreadId(),
        [](HWND hwnd, LPARAM param) -> BOOL {
            wchar_t name[64] = {};
            GetClassNameW(hwnd, name, static_cast<int>(std::size(name)));
            if (wcscmp(name, CandidateWindow::kClassName) != 0) return TRUE;
            *reinterpret_cast<HWND*>(param) = hwnd;
            return FALSE;
        },
        reinterpret_cast<LPARAM>(&found));
    return found;
}

/// 候選窗顯示中：不能搶走焦點，也不能蓋住游標那一行（放在下方或上方）。
void checkCandidateShown(HWND window, HWND target, std::string& problems) {
    const HWND candidate = findCandidateWindow();
    if (!candidate || !IsWindowVisible(candidate)) {
        problems += "  候選窗沒有出現\n";
        return;
    }
    if (!(GetWindowLongW(candidate, GWL_EXSTYLE) & WS_EX_NOACTIVATE)) problems += "  候選窗少了 WS_EX_NOACTIVATE\n";
    if (GetForegroundWindow() != window || GetFocus() != target) problems += "  候選窗搶走了焦點\n";
    RECT box{};
    GetWindowRect(candidate, &box);
    GUITHREADINFO info{};
    info.cbSize = sizeof(info);
    if (GetGUIThreadInfo(GetCurrentThreadId(), &info) && info.hwndCaret) {
        RECT caret = info.rcCaret;
        MapWindowPoints(info.hwndCaret, nullptr, reinterpret_cast<POINT*>(&caret), 2);
        const LONG middle = (caret.top + caret.bottom) / 2;
        if (box.top < middle && box.bottom > middle) problems += "  候選窗蓋住了游標那一行\n";
        if (std::abs(box.top - caret.bottom) > 300 && std::abs(caret.top - box.bottom) > 300) problems += "  候選窗離游標太遠\n";
    }
}

void checkCandidateHidden(std::string& problems) {
    const HWND candidate = findCandidateWindow();
    if (candidate && IsWindowVisible(candidate)) problems += "  候選窗應該已經關閉\n";
}

ComPtr<ITfCandidateListUIElementBehavior> candidateElement(ITfUIElementMgr* uiMgr, const UIElementSink& sink) {
    ComPtr<ITfUIElement> element;
    ComPtr<ITfCandidateListUIElementBehavior> behavior;
    if (uiMgr && sink.current != TF_INVALID_UIELEMENTID && SUCCEEDED(uiMgr->GetUIElement(sink.current, &element))) element.As(&behavior);
    return behavior;
}

/// UI 元件的內容（數量、每一個字、反白、頁碼）要和引擎的候選窗完全相同。
void checkCandidateList(ITfUIElementMgr* uiMgr, const UIElementSink& sink, const InputSession& oracle, std::string& problems) {
    const auto& list = oracle.candidateList();
    if (!list) {
        problems += "  （腳本錯誤）引擎沒有開候選窗\n";
        return;
    }
    const auto element = candidateElement(uiMgr, sink);
    if (!element) {
        problems += "  找不到候選字的 UI 元件（BeginUIElement 沒有被呼叫）\n";
        return;
    }
    GUID guid{};
    element->GetGUID(&guid);
    if (!IsEqualGUID(guid, kCandidateUIElementGuid)) problems += "  UI 元件的 GUID 不對\n";
    UINT count = 0, selection = 0, page = 0, pages = 0;
    element->GetCount(&count);
    element->GetSelection(&selection);
    element->GetCurrentPage(&page);
    element->GetPageIndex(nullptr, 0, &pages);
    const auto expect = [&](const char* what, UINT actual, int expected) {
        if (actual != static_cast<UINT>(expected)) {
            problems += std::string("  UI 元件的") + what + " " + std::to_string(actual) + "，應為 " + std::to_string(expected) + "\n";
        }
    };
    expect("候選數", count, static_cast<int>(list->items.size()));
    expect("反白位置", selection, list->highlighted);
    expect("目前頁", page, list->page());
    expect("頁數", pages, list->pageCount());
    for (UINT i = 0; i < std::min<UINT>(count, static_cast<UINT>(list->items.size())); ++i) {
        BSTR text = nullptr;
        if (FAILED(element->GetString(i, &text)) || !text) {
            problems += "  UI 元件讀不到第 " + std::to_string(i + 1) + " 個候選\n";
            break;
        }
        const std::string actual = utf8::fromWide(std::wstring(text, SysStringLen(text)));
        SysFreeString(text);
        const std::string expected = list->items[i].displayText();
        if (actual != expected) {
            problems += "  UI 元件第 " + std::to_string(i + 1) + " 個候選「" + actual + "」，應為「" + expected + "」\n";
            break;
        }
    }
}

// MARK: - 中英切換的檢查

/// 一題測試的狀態：測試用的 TSF 物件，以及輸入法以外算出來的標準答案（引擎、中英模式）。
struct TestRun {
    HWND window = nullptr;
    HWND target = nullptr;
    ITfThreadMgr* threadMgr = nullptr;
    TfClientId client = TF_CLIENTID_NULL;
    ITfKeystrokeMgr* keystrokes = nullptr;
    ITfUIElementMgr* uiMgr = nullptr;
    UIElementSink* sink = nullptr;
    InputSession* oracle = nullptr;
    std::string expectedCommits;
    /// 標準答案：這個程式目前是不是中文、全域是不是中文、Shift 按著而且還可能是一次單按。
    bool chinese = true;
    bool globalChinese = true;
    bool shiftPending = false;
    std::string problems;

    /// 標準答案切換這個程式的中英。使用者自己切到英文（單按 Shift、點圖示）時組字區留著，只關掉候選窗；
    /// 程式自己關掉輸入法時先送出組字區（同 TextService::applyMode）。
    void setChinese(bool value, bool byUser = true) {
        if (chinese && !value && oracle->isComposing()) {
            if (!byUser) {
                expectedCommits += oracle->commitAll();
            } else if (oracle->candidateList()) {
                oracle->handle(KeyEvent::of(KeyEvent::Type::Escape));
            }
        }
        chinese = value;
    }
};

HRESULT writeCompartment(IUnknown* owner, TfClientId client, REFGUID guid, LONG value) {
    ComPtr<ITfCompartmentMgr> compartments;
    ComPtr<ITfCompartment> compartment;
    if (FAILED(owner->QueryInterface(IID_PPV_ARGS(&compartments))) || FAILED(compartments->GetCompartment(guid, &compartment))) return E_FAIL;
    VARIANT variant;
    VariantInit(&variant);
    variant.vt = VT_I4;
    variant.lVal = value;
    return compartment->SetValue(client, &variant);
}

ComPtr<ITfCompartmentMgr> globalCompartments(ITfThreadMgr* threadMgr) {
    ComPtr<ITfCompartmentMgr> global;
    threadMgr->GetGlobalCompartment(&global);
    return global;
}

ComPtr<ITfLangBarItemButton> modeButton(ITfThreadMgr* threadMgr) {
    ComPtr<ITfLangBarItemMgr> langBar;
    ComPtr<ITfLangBarItem> item;
    ComPtr<ITfLangBarItemButton> button;
    if (SUCCEEDED(threadMgr->QueryInterface(IID_PPV_ARGS(&langBar))) && SUCCEEDED(langBar->GetItem(GUID_LBI_INPUTMODE, &item))) item.As(&button);
    return button;
}

/// 每一題開始前切回中文：扮演程式把全域狀態與這個程式的輸入法開關都設成中文。
void forceChinese(TestRun& run) {
    if (const auto global = globalCompartments(run.threadMgr)) writeCompartment(global.Get(), run.client, kChineseModeCompartment, 1);
    writeCompartment(run.threadMgr, run.client, GUID_COMPARTMENT_KEYBOARD_OPENCLOSE, 1);
    pumpMessages();
    run.chinese = run.globalChinese = true;
    run.shiftPending = false;
}

/// <?mode:zh|en|en-local>：輸入法開關、轉換模式、全域狀態、工作列圖示都要符合。
void checkMode(const std::string& expect, TestRun& run) {
    const bool threadChinese = expect == "zh";
    const bool globalChinese = expect != "en";
    if (run.chinese != threadChinese || run.globalChinese != globalChinese) run.problems += "  （腳本錯誤）標準答案的中英狀態和 <?mode:" + expect + "> 不同\n";
    const CompartmentValue open = readCompartment(run.threadMgr, GUID_COMPARTMENT_KEYBOARD_OPENCLOSE);
    const CompartmentValue conversion = readCompartment(run.threadMgr, GUID_COMPARTMENT_KEYBOARD_INPUTMODE_CONVERSION);
    if (open.type != VT_I4 || (open.value != 0) != threadChinese) {
        run.problems += "  輸入法開關是 " + std::to_string(open.value) + "（型別 " + std::to_string(open.type) + "），應為 " + (threadChinese ? "1" : "0") + "\n";
    }
    if (conversion.type != VT_I4 || ((conversion.value & TF_CONVERSIONMODE_NATIVE) != 0) != threadChinese) {
        run.problems += "  轉換模式是 " + std::to_string(conversion.value) + "（型別 " + std::to_string(conversion.type) + "），應為" +
                        (threadChinese ? "中文" : "英數") + "\n";
    }
    CompartmentValue global;
    if (const auto compartments = globalCompartments(run.threadMgr)) global = readCompartment(compartments.Get(), kChineseModeCompartment);
    if ((global.type != VT_I4 || global.value != 0) != globalChinese) run.problems += std::string("  全域狀態應為") + (globalChinese ? "中文" : "英文") + "\n";
    const auto button = modeButton(run.threadMgr);
    BSTR text = nullptr;
    if (!button || FAILED(button->GetText(&text)) || !text) {
        run.problems += "  找不到工作列的「中／英」圖示（GUID_LBI_INPUTMODE）\n";
    } else {
        const std::string shown = utf8::fromWide(std::wstring(text, SysStringLen(text)));
        SysFreeString(text);
        if (shown != (threadChinese ? "中" : "英")) run.problems += "  工作列圖示是「" + shown + "」\n";
    }
}

/// 執行腳本裡的一個動作（<shift>…）或檢查（<?…>）；操作會同時套用到輸入法與標準答案。
void runStep(const ScriptKey& key, TestRun& run) {
    const std::string& check = key.check;
    if (check == "!shift" || check == "!shift-down") {
        if (sendShift(run.keystrokes, true)) run.problems += "  Shift 按下被輸入法吃掉了\n";
        run.shiftPending = true;
        if (check == "!shift-down") return;
    }
    if (check == "!shift" || check == "!shift-up") {
        if (sendShift(run.keystrokes, false)) run.problems += "  Shift 放開被輸入法吃掉了\n";
        if (run.shiftPending) {
            run.setChinese(!run.chinese);  // 單按 Shift：以這個程式目前的狀態切換，並改變全域狀態
            run.globalChinese = run.chinese;
        }
        run.shiftPending = false;
        return;
    }
    if (check == "!click") {
        // 按住 Shift 點一下（選取文字）：滑鼠訊息經過輸入法的訊息掛鉤。
        PostMessageW(run.target, WM_LBUTTONDOWN, MK_LBUTTON | MK_SHIFT, MAKELPARAM(2, 2));
        PostMessageW(run.target, WM_LBUTTONUP, MK_SHIFT, MAKELPARAM(2, 2));
        pumpMessages();
        run.shiftPending = false;
        return;
    }
    if (check == "cand") {
        checkCandidateShown(run.window, run.target, run.problems);
    } else if (check == "nocand") {
        checkCandidateHidden(run.problems);
    } else if (check == "list") {
        checkCandidateList(run.uiMgr, *run.sink, *run.oracle, run.problems);
    } else if (check == "uiless") {
        run.sink->drawItself = true;
    } else if (check.rfind("mode:", 0) == 0) {
        checkMode(check.substr(5), run);
    } else if (check.rfind("set-open:", 0) == 0) {
        const bool open = check.substr(9) != "0";
        writeCompartment(run.threadMgr, run.client, GUID_COMPARTMENT_KEYBOARD_OPENCLOSE, open ? 1 : 0);
        pumpMessages();
        run.setChinese(open, false);  // 程式自己打開／關掉輸入法：只影響這個程式
    } else if (check == "lbi-click") {
        const auto button = modeButton(run.threadMgr);
        RECT area{};
        if (!button || FAILED(button->OnClick(TF_LBI_CLK_LEFT, POINT{0, 0}, &area))) run.problems += "  無法點工作列的「中／英」圖示\n";
        pumpMessages();
        run.setChinese(!run.chinese);
        run.globalChinese = run.chinese;
    } else if (check.rfind("ui-", 0) == 0) {
        const auto element = candidateElement(run.uiMgr, *run.sink);
        if (!element) {
            run.problems += "  找不到候選字的 UI 元件，無法執行 " + check + "\n";
            return;
        }
        HRESULT hr = E_FAIL;
        if (check.rfind("ui-select:", 0) == 0) {
            const int index = std::atoi(check.c_str() + 10);
            hr = element->SetSelection(static_cast<UINT>(index));
            run.oracle->highlightCandidate(index);
        } else if (check == "ui-finalize") {
            hr = element->Finalize();
            if (run.oracle->candidateList()) run.oracle->chooseCandidate(run.oracle->candidateList()->highlighted);
        } else if (check == "ui-abort") {
            hr = element->Abort();
            run.oracle->handle(KeyEvent::of(KeyEvent::Type::Escape));
        }
        if (FAILED(hr)) {
            char code[16];
            std::snprintf(code, sizeof(code), "%08lX", static_cast<unsigned long>(hr));
            run.problems += "  " + check + " 失敗（0x" + code + "）\n";
        }
        pumpMessages();
    } else {
        run.problems += "  （腳本錯誤）不認得的檢查 <?" + check + ">\n";
    }
}

struct TestCase {
    const char* name;
    std::string keys;
};

std::vector<TestCase> testCases() {
    std::string longSentence;
    for (int i = 0; i < TextService::kMaxCompositionLength + 2; ++i) longSentence += "su3";
    return {
        {"打字後 Enter 送出", "su3cl3<enter>"},
        {"組字中空白鍵進組字區，Enter 才送出", "5j/ jp6<space><enter>"},
        {"拼注音時空白鍵是一聲", "e/ vup <enter>"},
        {"整句選字：越更新越難用", "m,4e/ vup m,4s06m/4<enter>"},
        {"沒有組字時 Shift+字母直接送出半形大寫", "A"},
        {"組字中 Shift+字母進組字區", "su3B<enter>"},
        {"全形標點（Ctrl＋,）進組字區，Enter 才送出", "<C-,><enter>"},
        {"標點夾在句子中間", "su3<C-,>cl3<S-,><enter>"},
        // 標點（Windows 配置，比照微軟注音；對照表見 README）
        {"Shift＋標點是半形，沒有組字時直接送出", "<S-,><S-.><S-1><S-9><S-0><S-;><S-'><S-/><S-`>`[]'"},
        {"Ctrl＋標點是全形：，。；、【】", "<C-,><C-.><C-;><C-'><C-[><C-]><enter>"},
        {"Ctrl+, 的第二個候選是「、」", "<C-,><down><?cand><?list><down><enter><?nocand><enter>"},
        {"Ctrl+[ 的候選依序是【「『", "<C-[><down><?cand><?list>3<?nocand>5j/ <C-]><enter>"},
        {"英文模式下 Ctrl+, 也打出「，」，打英文時先送出", "<shift><?mode:en><C-,>a<shift><?mode:zh>"},
        {"英文模式下 Ctrl+. 按 ↓ 換成半形", "<shift><?mode:en><C-.><down><?cand>2<?nocand><enter><shift><?mode:zh>"},
        {"括號", "<S-[>5j/ <S-]><enter>"},
        {"Esc 清掉整個組字區", "su3cl3<esc>"},
        {"Esc 清掉正在拼的注音", "su<esc>"},
        {"Backspace 刪掉注音符號（ㄋㄧ → ㄋ → ㄋㄨㄥˊ）", "su<bs>j/6<enter>"},
        {"Backspace 刪掉游標前的字", "su3cl3<bs><enter>"},
        {"游標移到中間再插入", "su3cl3<left>cl3<enter>"},
        {"Home 與 Delete", "su3cl3<home><del><enter>"},
        {"沒有注音時按聲調鍵：吃掉、不輸出", "3"},
        {"數字鍵盤：沒有組字時直接送出", "<num1><num2>"},
        {"數字鍵盤：組字中進組字區", "su3<num1><enter>"},
        {"組字區超過 30 格，自動送出最前面的詞", longSentence + "<enter>"},
        {"沒有送出時，文件裡是組字中的文字", "su3cl3"},
        // 候選窗（第 4 階段）。「中文輸入法」= 5j/ jp6gj bj4z83
        {"候選窗：游標在最後按 ↓，只列最後一個字", "5j/ jp6gj bj4z83<down><?cand><?list><enter><?nocand>"},
        {"候選窗：↓ 移動反白，Enter 選字", "5j/ jp6<down><down><?cand><?list><enter><?nocand>"},
        {"候選窗：數字鍵選字", "5j/ jp6<down><?cand>2<?nocand>"},
        {"候選窗：游標在字前面按 ↓，列出從這個字開始的詞", "5j/ jp6gj bj4z83<home><right><right><down><?cand><?list>1<?nocand>"},
        {"候選窗：空白鍵翻頁，Esc 關閉", "5j/ <down><space><?cand><?list><esc><?nocand>"},
        {"符號候選：在「；」上按 ↓", "<C-;><down><?cand><?list><down><enter><?nocand>"},
        {"括號配對：在組字區換括號", "<S-[>5j/ <S-]><home><down><?list>2<?nocand>"},
        {"UI-less：程式自己畫，輸入法不顯示候選窗", "<?uiless>5j/ jp6<down><?nocand><?list><?ui-select:1><?list><?ui-finalize><?nocand>"},
        {"UI-less：Abort 關閉候選字", "<?uiless>5j/ jp6<down><?ui-abort><?nocand><enter>"},
        // 中英切換（第 5 階段）
        {"單按 Shift 切到英文，按鍵全部交還", "<?mode:zh><shift><?mode:en>su3<enter><bs><shift><?mode:zh>"},
        {"組字中單按 Shift：組字區留著，英文接著打在組字區", "su3cl3<shift><?mode:en>ab<shift><?mode:zh>su3<enter>"},
        {"英文模式接著組字區打：空白、數字、符號也放進組字區，Enter 才送出", "su3<shift><?mode:en>a b1<S-1>.<S-[><enter><shift><?mode:zh>"},
        {"英文模式下再單按 Shift 切回中文", "<shift><shift><?mode:zh>su3<enter>"},
        {"Shift＋字母不會切換", "<shift-down>A<shift-up><?mode:zh>su3<enter>"},
        {"按住 Shift 點滑鼠不會切換", "<shift-down><click><shift-up><?mode:zh>su3<enter>"},
        {"候選窗開著時單按 Shift：關閉候選窗，組字區留著", "5j/ jp6<down><?cand><shift><?nocand><?mode:en><shift><?mode:zh><enter>"},
        {"程式自己關掉輸入法：只有這個程式變英文", "<?set-open:0><?mode:en-local>su3<?set-open:1><?mode:zh>su3<enter>"},
        {"程式關掉輸入法後單按 Shift：切回中文", "<?set-open:0><shift><?mode:zh>su3<enter>"},
        {"點工作列的「中／英」圖示切換", "<?lbi-click><?mode:en>su3<?lbi-click><?mode:zh>su3<enter>"},
    };
}

/// 不經過輸入法、直接用引擎打字，並在候選窗選 word 後送出（扮演「別的程式」學習）。
bool pickWithEngine(InputSession& session, std::string_view keys, const std::string& word) {
    for (const auto& key : parseKeys(keys)) {
        if (key.check.empty()) session.handle(key.event);
    }
    session.handle(KeyEvent::of(KeyEvent::Type::Down));
    const auto& list = session.candidateList();
    if (!list) return false;
    const auto it = std::find_if(list->items.begin(), list->items.end(), [&](const Candidate& c) { return c.text == word; });
    if (it == list->items.end()) return false;
    session.chooseCandidate(static_cast<int>(it - list->items.begin()));
    session.handle(KeyEvent::of(KeyEvent::Type::Enter));
    return true;
}

// 只測 RichEdit（直接使用 TSF）。一般的 EDIT 控制項走 IMM32 相容層，按鍵不經過 ITfKeystrokeMgr，
// 要模擬只能送真的鍵盤輸入（SendInput），會打進當時在前景的任何視窗，所以那條路徑用手動測試。
int runSelfTest(const std::filesystem::path& modelPath) {
    std::printf("棒棒注音自動測試（%ls，%s，RichEdit／TSF）\n", kArch, BANGBANG_VERSION_STRING);
    std::shared_ptr<const LanguageModel> lm;
    try {
        lm = LanguageModel::load(modelPath);
    } catch (const std::exception& e) {
        std::printf("無法讀取模型（標準答案用）：%s\n", e.what());
        return 2;
    }
    // 輸入法的選字記憶改用暫存的記憶檔，不碰使用者的記憶檔；測完刪掉。最後兩題用它測跨程式共用。
    const std::filesystem::path memoryFolder =
        std::filesystem::temp_directory_path() / (L"bangbang-testpad-" + std::to_wstring(GetCurrentProcessId()));
    std::error_code memoryError;
    std::filesystem::create_directories(memoryFolder, memoryError);
    const std::filesystem::path memoryFile = memoryFolder / L"user-model.json";
    SetEnvironmentVariableW(L"BANGBANG_USER_MODEL", memoryFile.c_str());

    HWND window = createTestWindow();
    const HWND target = g_richEdit;
    ShowWindow(window, SW_SHOWNORMAL);
    SetForegroundWindow(window);
    SetFocus(target);
    pumpMessages(10);

    ComPtr<ITfInputProcessorProfileMgr> profiles;
    if (FAILED(CoCreateInstance(CLSID_TF_InputProcessorProfiles, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&profiles)))) {
        std::printf("無法建立 TSF 的語言設定檔管理員\n");
        return 2;
    }
    HRESULT hr = profiles->ActivateProfile(TF_PROFILETYPE_INPUTPROCESSOR, kLangId, kTextServiceClsid, kProfileGuid, nullptr,
                                           TF_IPPMF_FORPROCESS | TF_IPPMF_DONTCARECURRENTINPUTLANGUAGE);
    pumpMessages(10);
    TF_INPUTPROCESSORPROFILE active{};
    if (FAILED(hr) || FAILED(profiles->GetActiveProfile(GUID_TFCAT_TIP_KEYBOARD, &active)) ||
        !IsEqualCLSID(active.clsid, kTextServiceClsid)) {
        std::printf("無法啟用棒棒注音（0x%08lX）。請先執行 windows\\scripts\\register.ps1。\n", static_cast<unsigned long>(hr));
        return 2;
    }
    ComPtr<ITfThreadMgr> threadMgr;
    ComPtr<ITfKeystrokeMgr> keystrokes;
    ComPtr<ITfUIElementMgr> uiMgr;
    if (FAILED(CoCreateInstance(CLSID_TF_ThreadMgr, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&threadMgr))) ||
        FAILED(threadMgr.As(&keystrokes)) || FAILED(threadMgr.As(&uiMgr))) {
        std::printf("無法取得 TSF 的按鍵管理員\n");
        return 2;
    }
    // 扮演程式接收候選字 UI 元件的通知（UI-less 測試用；一般測試時回答「請輸入法自己畫」）。
    ComPtr<UIElementSink> sink;
    sink.Attach(new UIElementSink());
    ComPtr<ITfSource> source;
    DWORD sinkCookie = TF_INVALID_COOKIE;
    if (FAILED(threadMgr.As(&source)) || FAILED(source->AdviseSink(IID_ITfUIElementSink, sink.Get(), &sinkCookie))) {
        std::printf("（無法註冊 ITfUIElementSink，候選字的 UI 元件檢查會失敗）\n");
    }

    // 測試會切換所有程式共用的中英狀態：先記下來，測完還原。
    TfClientId client = TF_CLIENTID_NULL;
    threadMgr->Activate(&client);
    const ComPtr<ITfCompartmentMgr> global = globalCompartments(threadMgr.Get());
    const CompartmentValue initialGlobal = global ? readCompartment(global.Get(), kChineseModeCompartment) : CompartmentValue{};
    TestRun base;
    base.window = window;
    base.target = target;
    base.threadMgr = threadMgr.Get();
    base.client = client;
    base.keystrokes = keystrokes.Get();
    base.uiMgr = uiMgr.Get();
    base.sink = sink.Get();
    forceChinese(base);

    int passed = 0, failed = 0, skipped = 0;
    // 啟用後應該回報「開啟、中文模式」：IMM32 程式看到的開關狀態與轉換模式都讀這兩個值。
    if (g_probeCompartments) probeCompartments(threadMgr.Get());
    const CompartmentValue open = readCompartment(threadMgr.Get(), GUID_COMPARTMENT_KEYBOARD_OPENCLOSE);
    const CompartmentValue conversion = readCompartment(threadMgr.Get(), GUID_COMPARTMENT_KEYBOARD_INPUTMODE_CONVERSION);
    if (open.type == VT_I4 && open.value == 1 && conversion.type == VT_I4 && (conversion.value & TF_CONVERSIONMODE_NATIVE) != 0) {
        ++passed;
        std::printf("✓ 輸入模式：開啟、中文\n");
    } else {
        ++failed;
        std::printf("✗ 輸入模式：開關 %ld（型別 %u）、轉換模式 0x%lX（型別 %u），應為開啟、含中文 0x1\n", static_cast<long>(open.value),
                    static_cast<unsigned>(open.type), static_cast<unsigned long>(conversion.value), static_cast<unsigned>(conversion.type));
    }
    for (const auto& test : testCases()) {
        SetWindowTextW(target, L"");
        if (!ensureForeground(window, target)) {
            ++skipped;
            std::printf("－ %s：測試視窗不在前景（可能點了別的視窗），略過\n", test.name);
            continue;
        }
        InputSession oracle(lm);
        oracle.setPunctuationStyle(Punctuation::Style::Windows);  // 和輸入法一樣用 Windows 的標點配置
        oracle.maxCompositionLength = TextService::kMaxCompositionLength;
        TestRun run = base;
        run.oracle = &oracle;
        forceChinese(run);
        sink->drawItself = false;
        for (const auto& key : parseKeys(test.keys)) {
            if (!key.check.empty()) {
                runStep(key, run);
                continue;
            }
            run.shiftPending = false;  // Shift 按著時按了別的鍵
            if (!run.chinese && !key.ctrl) {
                // 英文模式：沒有組字時輸入法不吃任何鍵（testpad 不會再把鍵送給 RichEdit，所以文件不變）。
                // 組字區裡還有字時，照 InputSession::englishModeKey（和輸入法相同）：交給輸入法，或先送出組字區再交給程式。
                const bool composing = oracle.isComposing();
                const auto mapped = composing ? oracle.englishModeKey(key.event, false) : std::nullopt;
                if (mapped && oracle.wouldHandle(*mapped)) {
                    const SessionOutput out = oracle.handle(*mapped);
                    run.expectedCommits += out.commit;
                    const bool eaten = sendKey(keystrokes.Get(), key);
                    if (eaten != out.handled) run.problems += "  英文模式組字中按鍵 " + key.label + "：輸入法" + (eaten ? "吃掉了" : "沒有吃掉") + "\n";
                    continue;
                }
                if (composing) run.expectedCommits += oracle.commitAll();
                if (sendKey(keystrokes.Get(), key)) run.problems += "  英文模式下按鍵 " + key.label + " 被輸入法吃掉了\n";
                continue;
            }
            const SessionOutput out = oracle.handle(key.event);
            run.expectedCommits += out.commit;
            const bool eaten = sendKey(keystrokes.Get(), key);
            if (eaten != out.handled) {
                run.problems += "  按鍵 " + key.label + "：輸入法" + (eaten ? "吃掉了" : "沒有吃掉") + "，引擎" + (out.handled ? "會處理" : "不處理") + "\n";
            }
        }
        const std::string expected = run.expectedCommits + (oracle.isComposing() ? oracle.markedText() : "");
        const std::string actual = windowText(target);
        std::string problems = run.problems;
        if (actual != expected) problems += "  文件內容：「" + actual + "」，應為「" + expected + "」\n";
        // 結束這一題：回到中文，輸入法還在組字的話按 Esc 清掉（候選窗、注音、組字區各要一次）。
        forceChinese(run);
        if (oracle.isComposing()) {
            const ScriptKey escape = parseKeys("<esc>").front();
            for (int i = 0; i < 3; ++i) sendKey(keystrokes.Get(), escape);
        }
        if (problems.empty()) {
            ++passed;
            std::printf("✓ %s：「%s」\n", test.name, actual.c_str());
        } else if (GetForegroundWindow() != window) {
            // 測到一半視窗被搶走前景：之後的按鍵根本沒送到輸入法，不是輸入法的問題。
            ++skipped;
            std::printf("－ %s：測試中視窗失去前景（可能點了別的視窗），不算失敗\n", test.name);
        } else {
            ++failed;
            std::printf("✗ %s（%s）\n%s", test.name, test.keys.c_str(), problems.c_str());
        }
        std::fflush(stdout);
    }

    // 選字記憶跨程式共用（第 6 階段）：testpad 用自己的另一份引擎扮演「別的程式」，和輸入法讀寫同一個記憶檔。
    {
        UserModel other(lm, memoryFile);
        const auto report = [&](const char* name, const std::string& problems, const std::string& actual) {
            if (problems.empty()) {
                ++passed;
                std::printf("✓ %s：「%s」\n", name, actual.c_str());
            } else if (GetForegroundWindow() != window) {
                ++skipped;
                std::printf("－ %s：測試中視窗失去前景（可能點了別的視窗），不算失敗\n", name);
            } else {
                ++failed;
                std::printf("✗ %s\n%s", name, problems.c_str());
            }
            std::fflush(stdout);
        };
        const auto typeWithIme = [&](const std::string& keys) {
            for (const auto& key : parseKeys(keys)) {
                if (key.check.empty()) sendKey(keystrokes.Get(), key);
            }
        };
        const auto ready = [&](const char* name) {
            SetWindowTextW(target, L"");
            if (ensureForeground(window, target)) {
                TestRun run = base;
                forceChinese(run);
                return true;
            }
            ++skipped;
            std::printf("－ %s：測試視窗不在前景（可能點了別的視窗），略過\n", name);
            return false;
        };

        const char* first = "記憶：別的程式在「我愛」後選「妳」，這裡下次打就是「妳」";
        InputSession elsewhere(lm, &other);
        const bool pickedThere = pickWithEngine(elsewhere, "ji394su3", "妳");
        if (ready(first)) {
            typeWithIme("ji394su3<enter>");
            const std::string actual = windowText(target);
            std::string problems;
            if (!pickedThere) problems += "  （測試錯誤）別的程式的候選窗裡沒有「妳」\n";
            if (actual != "我愛妳") problems += "  文件內容：「" + actual + "」，應為「我愛妳」\n";
            report(first, problems, actual);
        }

        const char* second = "記憶：這裡改選「你」，別的程式也跟著變回「你」";
        if (ready(second)) {
            // 用和輸入法一樣的記憶算出「你」在候選窗的第幾個（只看、不選，不會學習）。
            std::string keys;
            {
                InputSession oracle(lm, &other);
                for (const auto& key : parseKeys("ji394su3<down>")) oracle.handle(key.event);
                const auto& list = oracle.candidateList();
                if (list) {
                    const auto it = std::find_if(list->items.begin(), list->items.end(), [](const Candidate& c) { return c.text == "你"; });
                    const auto index = it - list->items.begin();
                    if (it != list->items.end() && index < list->perPage) keys = "ji394su3<down>" + std::to_string(index + 1) + "<enter>";
                }
            }
            std::string problems;
            std::string actual;
            if (keys.empty()) {
                problems += "  （測試錯誤）候選窗第一頁沒有「你」\n";
            } else {
                typeWithIme(keys);
                actual = windowText(target);
                if (actual != "我愛你") problems += "  文件內容：「" + actual + "」，應為「我愛你」\n";
                if (!other.refresh()) problems += "  別的程式沒有發現記憶檔被輸入法改過\n";
                InputSession check(lm, &other);
                for (const auto& key : parseKeys("ji394su3")) check.handle(key.event);
                if (check.markedText() != "我愛你") problems += "  別的程式打出「" + check.markedText() + "」，應為「我愛你」\n";
            }
            report(second, problems, actual);
        }
    }

    if (skipped > 0) {
        std::printf("通過 %d 項，失敗 %d 項，略過 %d 項（測試時請不要點別的視窗）\n", passed, failed, skipped);
    } else {
        std::printf("通過 %d 項，失敗 %d 項\n", passed, failed);
    }
    // 還原測試前的中英狀態（所有程式共用）。
    if (global) {
        if (initialGlobal.type == VT_I4) {
            writeCompartment(global.Get(), client, kChineseModeCompartment, initialGlobal.value);
        } else {
            global->ClearCompartment(client, kChineseModeCompartment);
        }
        pumpMessages();
    }
    threadMgr->Deactivate();
    if (source && sinkCookie != TF_INVALID_COOKIE) source->UnadviseSink(sinkCookie);
    DestroyWindow(window);
    pumpMessages();
    std::filesystem::remove_all(memoryFolder, memoryError);
    if (failed > 0) return 1;
    return skipped > 0 ? 3 : 0;
}

// MARK: - 全域 compartment 診斷

/// 診斷用的全域 compartment（和輸入法用的分開）：{0043C68D-311D-4E54-963F-4A87807219B4}
constexpr GUID kProbeGlobalCompartment = {0x0043c68d, 0x311d, 0x4e54, {0x96, 0x3f, 0x4a, 0x87, 0x80, 0x72, 0x19, 0xb4}};

class CompartmentWatcher final : public ITfCompartmentEventSink {
public:
    explicit CompartmentWatcher(ITfCompartment* compartment) : compartment_(compartment) {}
    STDMETHODIMP QueryInterface(REFIID riid, void** ppv) override {
        if (!ppv) return E_POINTER;
        *ppv = nullptr;
        if (!IsEqualIID(riid, IID_IUnknown) && !IsEqualIID(riid, IID_ITfCompartmentEventSink)) return E_NOINTERFACE;
        *ppv = static_cast<ITfCompartmentEventSink*>(this);
        AddRef();
        return S_OK;
    }
    STDMETHODIMP_(ULONG) AddRef() override { return ++references_; }
    STDMETHODIMP_(ULONG) Release() override {
        const ULONG remaining = --references_;
        if (remaining == 0) delete this;
        return remaining;
    }
    STDMETHODIMP OnChange(REFGUID) override {
        const CompartmentValue v = readCompartmentValue();
        std::printf("通知：值變成 %ld（型別 %u）\n", static_cast<long>(v.value), static_cast<unsigned>(v.type));
        std::fflush(stdout);
        return S_OK;
    }
    CompartmentValue readCompartmentValue() const {
        CompartmentValue result;
        VARIANT value;
        VariantInit(&value);
        if (SUCCEEDED(compartment_->GetValue(&value))) {
            result.type = value.vt;
            if (value.vt == VT_I4) result.value = value.lVal;
        }
        VariantClear(&value);
        return result;
    }

private:
    ULONG references_ = 1;
    ComPtr<ITfCompartment> compartment_;
};

/// --probe-global set <值> | get | watch <秒>：全域 compartment 是否跨程式共用、改了會不會通知別的程式。
int probeGlobal(const std::wstring& action, int argument) {
    ComPtr<ITfThreadMgr> threadMgr;
    TfClientId client = TF_CLIENTID_NULL;
    ComPtr<ITfCompartmentMgr> global;
    ComPtr<ITfCompartment> compartment;
    if (FAILED(CoCreateInstance(CLSID_TF_ThreadMgr, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&threadMgr))) ||
        FAILED(threadMgr->Activate(&client)) || FAILED(threadMgr->GetGlobalCompartment(&global)) ||
        FAILED(global->GetCompartment(kProbeGlobalCompartment, &compartment))) {
        std::printf("無法取得全域 compartment\n");
        return 2;
    }
    ComPtr<CompartmentWatcher> watcher;
    watcher.Attach(new CompartmentWatcher(compartment.Get()));
    if (action == L"set") {
        VARIANT value;
        VariantInit(&value);
        value.vt = VT_I4;
        value.lVal = argument;
        std::printf("寫入 %d：0x%08lX\n", argument, static_cast<unsigned long>(compartment->SetValue(client, &value)));
    } else if (action == L"get") {
        const CompartmentValue v = watcher->readCompartmentValue();
        std::printf("讀到 %ld（型別 %u）\n", static_cast<long>(v.value), static_cast<unsigned>(v.type));
    } else if (action == L"watch") {
        ComPtr<ITfSource> source;
        DWORD cookie = TF_INVALID_COOKIE;
        if (FAILED(compartment.As(&source)) || FAILED(source->AdviseSink(IID_ITfCompartmentEventSink, watcher.Get(), &cookie))) {
            std::printf("無法註冊通知\n");
            return 2;
        }
        std::printf("開始監看 %d 秒\n", argument);
        std::fflush(stdout);
        const ULONGLONG end = GetTickCount64() + static_cast<ULONGLONG>(argument) * 1000;
        while (GetTickCount64() < end) {
            pumpMessages();
            MsgWaitForMultipleObjects(0, nullptr, FALSE, 50, QS_ALLINPUT);
        }
        source->UnadviseSink(cookie);
        std::printf("結束監看\n");
    }
    threadMgr->Deactivate();
    return 0;
}

// MARK: - 候選窗外觀（PNG）

HRESULT renderPng(ID2D1Factory* d2d, IWICImagingFactory* wic, const CandidateRenderer& renderer, const CandidatePage& page,
                  const CandidateTheme& theme, UINT dpi, const std::filesystem::path& path) {
    const D2D1_SIZE_F size = renderer.measure(page);
    const float scale = static_cast<float>(dpi) / 96.0f;
    const auto width = static_cast<UINT>(std::ceil(size.width * scale));
    const auto height = static_cast<UINT>(std::ceil(size.height * scale));
    ComPtr<IWICBitmap> bitmap;
    HRESULT hr = wic->CreateBitmap(width, height, GUID_WICPixelFormat32bppPBGRA, WICBitmapCacheOnLoad, &bitmap);
    ComPtr<ID2D1RenderTarget> target;
    if (SUCCEEDED(hr)) {
        const auto properties = D2D1::RenderTargetProperties(D2D1_RENDER_TARGET_TYPE_DEFAULT,
                                                             D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED),
                                                             static_cast<float>(dpi), static_cast<float>(dpi));
        hr = d2d->CreateWicBitmapRenderTarget(bitmap.Get(), properties, &target);
    }
    if (SUCCEEDED(hr)) {
        target->SetTextAntialiasMode(D2D1_TEXT_ANTIALIAS_MODE_GRAYSCALE);
        target->BeginDraw();
        renderer.draw(target.Get(), page, theme);
        hr = target->EndDraw();
    }
    ComPtr<IWICStream> stream;
    ComPtr<IWICBitmapEncoder> encoder;
    ComPtr<IWICBitmapFrameEncode> frame;
    if (SUCCEEDED(hr)) hr = wic->CreateStream(&stream);
    if (SUCCEEDED(hr)) hr = stream->InitializeFromFilename(path.c_str(), GENERIC_WRITE);
    if (SUCCEEDED(hr)) hr = wic->CreateEncoder(GUID_ContainerFormatPng, nullptr, &encoder);
    if (SUCCEEDED(hr)) hr = encoder->Initialize(stream.Get(), WICBitmapEncoderNoCache);
    if (SUCCEEDED(hr)) hr = encoder->CreateNewFrame(&frame, nullptr);
    if (SUCCEEDED(hr)) hr = frame->Initialize(nullptr);
    if (SUCCEEDED(hr)) hr = frame->SetSize(width, height);
    WICPixelFormatGUID format = GUID_WICPixelFormat32bppPBGRA;
    if (SUCCEEDED(hr)) hr = frame->SetPixelFormat(&format);
    if (SUCCEEDED(hr)) hr = frame->WriteSource(bitmap.Get(), nullptr);
    if (SUCCEEDED(hr)) hr = frame->Commit();
    if (SUCCEEDED(hr)) hr = encoder->Commit();
    return hr;
}

/// --render-candidates <資料夾>：把幾種候選窗內容畫成 PNG（淺色／深色、100%／150%），檢查外觀用，不需要輸入法。
int renderCandidates(const std::filesystem::path& folder) {
    CandidateRenderer renderer;
    ComPtr<ID2D1Factory> d2d;
    ComPtr<IWICImagingFactory> wic;
    if (FAILED(renderer.initialize()) || FAILED(D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, d2d.GetAddressOf())) ||
        FAILED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&wic)))) {
        std::printf("無法建立 Direct2D／DirectWrite／WIC\n");
        return 2;
    }
    std::error_code error;
    std::filesystem::create_directories(folder, error);
    struct Sample {
        const char* name;
        CandidatePage page;
    };
    const std::vector<Sample> samples = {
        {"chars", {{L"法", L"髮", L"發", L"罰", L"伐", L"筏", L"乏", L"閥", L"琺"}, 0, 0, 3}},
        {"phrases", {{L"輸入法", L"輸入", L"輸", L"書", L"舒", L"殊"}, 2, 0, 1}},
        {"symbols", {{L"：", L":", L"；", L";"}, 1, 0, 1}},
        {"spaces", {{L"半形空白", L"全形空白"}, 0, 0, 1}},
        {"extb", {{L"𨑨迌", L"𨑨", L"𰻞𰻞麵"}, 0, 1, 2}},
    };
    const COLORREF accent = CandidateTheme::systemAccent();
    int written = 0;
    for (const bool dark : {false, true}) {
        for (const UINT dpi : {96u, 144u}) {
            for (const auto& sample : samples) {
                const std::wstring name = utf8::toWide(sample.name) + (dark ? L"-dark-" : L"-light-") + std::to_wstring(dpi) + L".png";
                const HRESULT hr = renderPng(d2d.Get(), wic.Get(), renderer, sample.page, CandidateTheme::make(dark, accent), dpi, folder / name);
                if (FAILED(hr)) {
                    std::printf("無法輸出 %ls（0x%08lX）\n", name.c_str(), static_cast<unsigned long>(hr));
                    return 1;
                }
                ++written;
            }
        }
    }
    std::printf("已輸出 %d 張候選窗圖片到 %ls\n", written, folder.c_str());
    return 0;
}

// MARK: - 候選窗的顯示時間

/// 從螢幕擷取視窗所在的範圍（含 DWM 畫的圓角），存成 PNG：看輸入法真正的候選窗在螢幕上長什麼樣子。
HRESULT captureWindow(HWND hwnd, const std::filesystem::path& path) {
    DwmFlush();  // 等 DWM 把視窗畫上螢幕
    DwmFlush();
    RECT rect{};
    GetWindowRect(hwnd, &rect);
    InflateRect(&rect, 6, 6);
    const int width = rect.right - rect.left;
    const int height = rect.bottom - rect.top;
    ComPtr<IWICImagingFactory> wic;
    HRESULT hr = CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&wic));
    if (FAILED(hr)) return hr;
    const HDC screen = GetDC(nullptr);
    const HDC memory = CreateCompatibleDC(screen);
    const HBITMAP bitmap = CreateCompatibleBitmap(screen, width, height);
    const HGDIOBJ old = SelectObject(memory, bitmap);
    BitBlt(memory, 0, 0, width, height, screen, rect.left, rect.top, SRCCOPY | CAPTUREBLT);
    SelectObject(memory, old);
    ComPtr<IWICBitmap> source;
    hr = wic->CreateBitmapFromHBITMAP(bitmap, nullptr, WICBitmapIgnoreAlpha, &source);
    DeleteObject(bitmap);
    DeleteDC(memory);
    ReleaseDC(nullptr, screen);
    ComPtr<IWICStream> stream;
    ComPtr<IWICBitmapEncoder> encoder;
    ComPtr<IWICBitmapFrameEncode> frame;
    if (SUCCEEDED(hr)) hr = wic->CreateStream(&stream);
    if (SUCCEEDED(hr)) hr = stream->InitializeFromFilename(path.c_str(), GENERIC_WRITE);
    if (SUCCEEDED(hr)) hr = wic->CreateEncoder(GUID_ContainerFormatPng, nullptr, &encoder);
    if (SUCCEEDED(hr)) hr = encoder->Initialize(stream.Get(), WICBitmapEncoderNoCache);
    if (SUCCEEDED(hr)) hr = encoder->CreateNewFrame(&frame, nullptr);
    if (SUCCEEDED(hr)) hr = frame->Initialize(nullptr);
    if (SUCCEEDED(hr)) hr = frame->SetSize(static_cast<UINT>(width), static_cast<UINT>(height));
    WICPixelFormatGUID format = GUID_WICPixelFormat24bppBGR;
    if (SUCCEEDED(hr)) hr = frame->SetPixelFormat(&format);
    if (SUCCEEDED(hr)) hr = frame->WriteSource(source.Get(), nullptr);
    if (SUCCEEDED(hr)) hr = frame->Commit();
    if (SUCCEEDED(hr)) hr = encoder->Commit();
    return hr;
}

/// --time-candidates steps [software]：把候選窗第一次顯示拆成各步驟量時間（自己照輸入法的步驟做一次）。
/// --time-candidates window [capture <PNG 檔>]：用輸入法真正的 CandidateWindow，量第一次與第二次從顯示到畫好的時間；
/// 加上 capture 時把螢幕上的候選窗存成圖片。
/// 每次都要在新的程序裡執行，量到的才是「新開的程式第一次選字」。
int timeCandidates(const std::wstring& mode, bool software, const std::filesystem::path& capture) {
    using Clock = std::chrono::steady_clock;
    auto last = Clock::now();
    const auto lap = [&](const char* what) {
        const auto now = Clock::now();
        std::printf("  %-34s %7.1f ms\n", what, std::chrono::duration<double, std::milli>(now - last).count());
        std::fflush(stdout);
        last = now;
    };
    CandidatePage page{{L"試", L"是", L"事", L"市", L"式", L"世", L"室", L"視", L"士"}, 0, 0, 13};
    const RECT anchor{300, 300, 360, 330};

    if (mode == L"window") {
        std::printf("輸入法的候選窗（%ls）：\n", kArch);
        CandidateWindow window;
        last = Clock::now();
        window.show(page, anchor, nullptr);
        lap("第一次 show()");
        UpdateWindow(window.hwnd());
        lap("第一次畫好（WM_PAINT）");
        window.hide();
        page.highlighted = 1;
        last = Clock::now();
        window.show(page, anchor, nullptr);
        lap("第二次 show()");
        UpdateWindow(window.hwnd());
        lap("第二次畫好（WM_PAINT）");
        if (!capture.empty()) {
            const HRESULT hr = captureWindow(window.hwnd(), capture);
            std::printf(SUCCEEDED(hr) ? "已擷取候選窗：%ls\n" : "無法擷取候選窗：%ls\n", capture.c_str());
        }
        return 0;
    }

    std::printf("候選窗第一次顯示的各步驟（%ls，%s）：\n", kArch, software ? "軟體繪製" : "顯示卡繪製");
    LoadLibraryW(L"dwrite.dll");
    lap("載入 dwrite.dll");
    LoadLibraryW(L"d2d1.dll");
    lap("載入 d2d1.dll");
    CandidateRenderer renderer;
    if (FAILED(renderer.initialize())) return 2;
    lap("DirectWrite 字型格式");
    const D2D1_SIZE_F size = renderer.measure(page);
    lap("第一次排版（量大小）");
    renderer.measure(page);
    lap("（對照）第二次排版");
    ComPtr<ID2D1Factory> d2d;
    if (FAILED(D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, d2d.GetAddressOf()))) return 2;
    lap("Direct2D factory");
    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = DefWindowProcW;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.lpszClassName = L"BangbangTimingWindow";
    RegisterClassExW(&wc);
    const HWND hwnd = CreateWindowExW(WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE, wc.lpszClassName, L"", WS_POPUP, 0, 0, 0, 0,
                                      nullptr, nullptr, wc.hInstance, nullptr);
    lap("建立視窗");
    const UINT dpi = GetDpiForWindow(hwnd);
    const int width = static_cast<int>(std::ceil(size.width * dpi / 96.0f));
    const int height = static_cast<int>(std::ceil(size.height * dpi / 96.0f));
    SetWindowPos(hwnd, HWND_TOPMOST, anchor.left, anchor.bottom, width, height, SWP_NOACTIVATE | SWP_SHOWWINDOW);
    lap("顯示視窗");
    ComPtr<ID2D1HwndRenderTarget> target;
    const auto properties = D2D1::RenderTargetProperties(software ? D2D1_RENDER_TARGET_TYPE_SOFTWARE : D2D1_RENDER_TARGET_TYPE_DEFAULT,
                                                         D2D1::PixelFormat(), static_cast<float>(dpi), static_cast<float>(dpi));
    if (FAILED(d2d->CreateHwndRenderTarget(properties, D2D1::HwndRenderTargetProperties(hwnd, D2D1::SizeU(width, height)), &target))) return 2;
    lap("建立繪圖目標");
    const CandidateTheme theme = CandidateTheme::current();
    for (const char* what : {"第一次畫", "第二次畫"}) {
        target->BeginDraw();
        renderer.draw(target.Get(), page, theme);
        target->EndDraw();
        lap(what);
    }
    DestroyWindow(hwnd);
    return 0;
}

}  // namespace

int wmain(int argc, wchar_t** argv) {
    SetConsoleOutputCP(CP_UTF8);
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    bool selfTest = false;
    std::optional<std::filesystem::path> renderFolder;
    std::filesystem::path modelPath(BANGBANG_DEFAULT_MODEL);
    for (int i = 1; i < argc; ++i) {
        const std::wstring arg = argv[i];
        if (arg == L"--selftest") {
            selfTest = true;
        } else if (arg == L"--probe-compartments") {
            selfTest = true;
            g_probeCompartments = true;
        } else if (arg == L"--render-candidates" && i + 1 < argc) {
            renderFolder = argv[++i];
        } else if (arg == L"--probe-global" && i + 1 < argc) {
            const std::wstring action = argv[++i];
            const int argument = i + 1 < argc ? _wtoi(argv[i + 1]) : 0;
            CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
            const int code = probeGlobal(action, argument);
            CoUninitialize();
            return code;
        } else if (arg == L"--time-candidates") {
            const std::wstring mode = i + 1 < argc ? argv[i + 1] : L"steps";
            const std::wstring option = i + 2 < argc ? argv[i + 2] : L"";
            const std::filesystem::path capture = option == L"capture" && i + 3 < argc ? argv[i + 3] : L"";
            CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
            const int code = timeCandidates(mode, option == L"software", capture);
            CoUninitialize();
            return code;
        } else if (arg == L"--lm" && i + 1 < argc) {
            modelPath = argv[++i];
        }
    }
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    const int code = renderFolder ? renderCandidates(*renderFolder) : selfTest ? runSelfTest(modelPath) : runInteractive();
    CoUninitialize();
    return code;
}
