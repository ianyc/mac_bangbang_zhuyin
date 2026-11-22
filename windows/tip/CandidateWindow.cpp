#include "CandidateWindow.h"

#include <dwmapi.h>
#include <shellscalingapi.h>
#include <windowsx.h>

#include <algorithm>
#include <cmath>
#include <mutex>

#include "Globals.h"

using Microsoft::WRL::ComPtr;

namespace bangbang {

namespace {

std::mutex g_classMutex;
ATOM g_class = 0;

}  // namespace

LRESULT CALLBACK candidateWindowProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
    if (message == WM_NCCREATE) {
        // CreateWindowEx 還沒回傳前就會收到訊息：先記下 hwnd，handleMessage 才能用。
        auto* creator = static_cast<CandidateWindow*>(reinterpret_cast<const CREATESTRUCTW*>(lParam)->lpCreateParams);
        creator->hwnd_ = hwnd;
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(creator));
    }
    auto* self = reinterpret_cast<CandidateWindow*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (!self) return DefWindowProcW(hwnd, message, wParam, lParam);
    try {
        return self->handleMessage(message, wParam, lParam);
    } catch (...) {
        // 例外不能穿過宿主程式的訊息迴圈。
        debugLog(L"棒棒注音：候選窗攔下未預期的例外");
        return DefWindowProcW(hwnd, message, wParam, lParam);
    }
}

namespace {

bool registerClass() {
    const std::lock_guard lock(g_classMutex);
    if (g_class) return true;
    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = candidateWindowProc;
    wc.hInstance = g_module;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.lpszClassName = CandidateWindow::kClassName;
    g_class = RegisterClassExW(&wc);
    if (!g_class && GetLastError() == ERROR_CLASS_ALREADY_EXISTS) {
        // 上一次載入的同一個 DLL 留下的類別（視窗程序已經不在了）：換成這一次的。
        UnregisterClassW(CandidateWindow::kClassName, g_module);
        g_class = RegisterClassExW(&wc);
    }
    if (!g_class) debugLog(L"棒棒注音：無法註冊候選窗類別（%lu）", GetLastError());
    return g_class != 0;
}

}  // namespace

void CandidateWindow::unregisterClass() {
    const std::lock_guard lock(g_classMutex);
    if (g_class) UnregisterClassW(kClassName, g_module);
    g_class = 0;
}

CandidateWindow::~CandidateWindow() {
    if (hwnd_) {
        SetWindowLongPtrW(hwnd_, GWLP_USERDATA, 0);
        DestroyWindow(hwnd_);
    }
}

bool CandidateWindow::ensureWindow(HWND owner) {
    if (hwnd_) {
        if (reinterpret_cast<HWND>(GetWindowLongPtrW(hwnd_, GWLP_HWNDPARENT)) != owner) {
            SetWindowLongPtrW(hwnd_, GWLP_HWNDPARENT, reinterpret_cast<LONG_PTR>(owner));
        }
        return true;
    }
    if (!registerClass()) return false;
    if (!renderer_.ready() && FAILED(renderer_.initialize())) {
        debugLog(L"棒棒注音：無法建立 DirectWrite 字型");
        return false;
    }
    if (!d2d_ && FAILED(D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, d2d_.GetAddressOf()))) {
        debugLog(L"棒棒注音：無法建立 Direct2D");
        return false;
    }
    hwnd_ = CreateWindowExW(WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE, kClassName, L"", WS_POPUP, 0, 0, 0, 0, owner, nullptr,
                            g_module, this);
    if (!hwnd_) {
        debugLog(L"棒棒注音：無法建立候選窗（%lu）", GetLastError());
        return false;
    }
    // Windows 11 圓角（舊版 Windows 不支援，失敗就算了）。
    const DWM_WINDOW_CORNER_PREFERENCE corner = DWMWCP_ROUND;
    DwmSetWindowAttribute(hwnd_, DWMWA_WINDOW_CORNER_PREFERENCE, &corner, sizeof(corner));
    createTarget();  // 視窗還沒顯示時就先建好，第一次顯示時直接畫
    return true;
}

bool CandidateWindow::createTarget() {
    // 用軟體繪製：候選窗很小，CPU 畫一次只要 1～4 ms。用顯示卡的話，每個程式第一次建立 Direct3D 裝置
    // 要將近 0.2 秒，新開的程式第一次選字時會先看到空白的候選窗（2026-11-22 用 testpad --time-candidates 實測）；
    // 每次畫完還要等螢幕的垂直同步。
    const auto properties = D2D1::RenderTargetProperties(D2D1_RENDER_TARGET_TYPE_SOFTWARE, D2D1::PixelFormat(), static_cast<float>(dpi_),
                                                         static_cast<float>(dpi_));
    RECT client{};
    GetClientRect(hwnd_, &client);
    const auto size = D2D1::SizeU(static_cast<UINT32>(std::max(1L, client.right - client.left)),
                                  static_cast<UINT32>(std::max(1L, client.bottom - client.top)));
    const auto hwndProperties = D2D1::HwndRenderTargetProperties(hwnd_, size, D2D1_PRESENT_OPTIONS_IMMEDIATELY);
    if (FAILED(d2d_->CreateHwndRenderTarget(properties, hwndProperties, &target_))) {
        debugLog(L"棒棒注音：無法建立候選窗的繪圖目標");
        return false;
    }
    return true;
}

void CandidateWindow::cloak(bool hidden) {
    const BOOL value = hidden ? TRUE : FALSE;
    DwmSetWindowAttribute(hwnd_, DWMWA_CLOAK, &value, sizeof(value));
}

UINT CandidateWindow::dpiFor(const RECT& anchor) const {
    // 依每個螢幕 DPI 運作的程式：用候選窗所在螢幕的 DPI。其他程式（不感知或系統 DPI）：用視窗的 DPI，
    // 其餘交給 Windows 縮放。
    const DPI_AWARENESS awareness = GetAwarenessFromDpiAwarenessContext(GetWindowDpiAwarenessContext(hwnd_));
    if (awareness == DPI_AWARENESS_PER_MONITOR_AWARE) {
        UINT x = 96, y = 96;
        if (SUCCEEDED(GetDpiForMonitor(MonitorFromRect(&anchor, MONITOR_DEFAULTTONEAREST), MDT_EFFECTIVE_DPI, &x, &y))) return x;
    }
    const UINT dpi = GetDpiForWindow(hwnd_);
    return dpi ? dpi : 96;
}

void CandidateWindow::place() {
    dpi_ = dpiFor(anchor_);
    const float scale = static_cast<float>(dpi_) / 96.0f;
    const D2D1_SIZE_F size = renderer_.measure(page_);
    const int width = static_cast<int>(std::ceil(size.width * scale));
    const int height = static_cast<int>(std::ceil(size.height * scale));
    MONITORINFO info{};
    info.cbSize = sizeof(info);
    GetMonitorInfoW(MonitorFromRect(&anchor_, MONITOR_DEFAULTTONEAREST), &info);
    const RECT& work = info.rcWork;
    const int gap = static_cast<int>(std::lround(4.0f * scale));
    int x = anchor_.left;
    int y = anchor_.bottom + gap;
    if (y + height > work.bottom) y = anchor_.top - gap - height;  // 下面放不下就放上面
    y = std::clamp(y, static_cast<int>(work.top), std::max(static_cast<int>(work.top), static_cast<int>(work.bottom) - height));
    x = std::clamp(x, static_cast<int>(work.left), std::max(static_cast<int>(work.left), static_cast<int>(work.right) - width));
    if (target_) {
        target_->SetDpi(static_cast<float>(dpi_), static_cast<float>(dpi_));
        target_->Resize(D2D1::SizeU(static_cast<UINT32>(width), static_cast<UINT32>(height)));
    }
    SetWindowPos(hwnd_, HWND_TOPMOST, x, y, width, height, SWP_NOACTIVATE | SWP_SHOWWINDOW);
}

void CandidateWindow::show(const CandidatePage& page, const RECT& anchor, HWND owner) {
    if (!ensureWindow(owner)) return;
    const bool wasShown = isShown();
    page_ = page;
    anchor_ = anchor;
    theme_ = CandidateTheme::current();
    const BOOL dark = theme_.dark ? TRUE : FALSE;
    DwmSetWindowAttribute(hwnd_, DWMWA_USE_IMMERSIVE_DARK_MODE, &dark, sizeof(dark));
    // 從隱藏變成顯示：先讓視窗隱形（cloak），畫好內容再現身，不會先閃一下空白的視窗。
    if (!wasShown) cloak(true);
    place();
    // 馬上畫，不等程式的訊息迴圈有空才處理 WM_PAINT。
    InvalidateRect(hwnd_, nullptr, FALSE);
    UpdateWindow(hwnd_);
    if (!wasShown) cloak(false);
    NotifyWinEvent(wasShown ? EVENT_OBJECT_IME_CHANGE : EVENT_OBJECT_IME_SHOW, hwnd_, OBJID_CLIENT, CHILDID_SELF);
}

void CandidateWindow::move(const RECT& anchor) {
    if (!isShown()) return;
    anchor_ = anchor;
    place();
}

void CandidateWindow::hide() {
    if (!isShown()) return;
    ShowWindow(hwnd_, SW_HIDE);
    NotifyWinEvent(EVENT_OBJECT_IME_HIDE, hwnd_, OBJID_CLIENT, CHILDID_SELF);
}

bool CandidateWindow::isShown() const {
    return hwnd_ && IsWindowVisible(hwnd_);
}

void CandidateWindow::paint() {
    PAINTSTRUCT ps;
    BeginPaint(hwnd_, &ps);
    if (!target_) createTarget();  // 上次畫的時候要求重建
    if (target_) {
        target_->BeginDraw();
        renderer_.draw(target_.Get(), page_, theme_);
        if (target_->EndDraw() == D2DERR_RECREATE_TARGET) {
            target_.Reset();  // 顯示卡重設了：下次重畫時重建
            InvalidateRect(hwnd_, nullptr, FALSE);
        }
    }
    EndPaint(hwnd_, &ps);
}

LRESULT CandidateWindow::handleMessage(UINT message, WPARAM wParam, LPARAM lParam) {
    switch (message) {
    case WM_MOUSEACTIVATE:
        return MA_NOACTIVATE;  // 點候選窗不會搶走宿主程式的焦點
    case WM_LBUTTONUP:
        if (onClick_) {
            const float scale = static_cast<float>(dpi_) / 96.0f;
            const int row = renderer_.rowAt(page_, D2D1::Point2F(GET_X_LPARAM(lParam) / scale, GET_Y_LPARAM(lParam) / scale));
            if (row >= 0) {
                const ClickHandler handler = onClick_;  // 處理途中候選窗可能被關掉
                handler(row);
            }
        }
        return 0;
    case WM_PAINT:
        paint();
        return 0;
    case WM_ERASEBKGND:
        return 1;  // 全部由 Direct2D 畫，避免閃爍
    case WM_DPICHANGED:
        return 0;  // 大小與位置都在 place() 依 DPI 計算
    case WM_DESTROY:
        target_.Reset();
        hwnd_ = nullptr;
        return 0;
    default:
        return DefWindowProcW(hwnd_, message, wParam, lParam);
    }
}

}  // namespace bangbang
