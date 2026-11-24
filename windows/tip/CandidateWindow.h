#pragma once

// 候選字視窗：自己畫的頂層視窗，不搶焦點（取代 Mac 版的 NSPanel，見 CandidatePanel.swift）。
// - WS_POPUP＋WS_EX_TOPMOST／TOOLWINDOW／NOACTIVATE，WM_MOUSEACTIVATE 回 MA_NOACTIVATE。
// - 在宿主程式的 UI 執行緒建立，沿用該執行緒的 DPI 設定（不更改宿主的 DPI 設定）：
//   視窗座標和 ITfContextView::GetTextExt 回傳的座標是同一套，依視窗實際的 DPI 放大。
// - Windows 11 圓角；跟隨系統的淺色／深色與強調色；顯示、隱藏、內容改變時送出 EVENT_OBJECT_IME_*。
// - Direct2D 軟體繪製：不必在每個程式裡初始化顯示卡（第一次要將近 0.2 秒）。顯示時同步畫好；
//   從隱藏變成顯示時先 cloak、畫好再現身，第一次選字也不會先看到空白的視窗。
// - 沉浸式程式（例如工作列搜尋）不能用 HWND 的繪圖目標：改畫在記憶體裡的點陣圖（WIC），再用 GDI 貼到視窗。

#include <windows.h>
#include <d2d1.h>
#include <wincodec.h>
#include <wrl/client.h>

#include <functional>

#include "CandidateRenderer.h"

namespace bangbang {

class CandidateWindow {
public:
    /// 使用者用滑鼠點了本頁第 row 列。
    using ClickHandler = std::function<void(int row)>;

    CandidateWindow() = default;
    ~CandidateWindow();
    CandidateWindow(const CandidateWindow&) = delete;
    CandidateWindow& operator=(const CandidateWindow&) = delete;

    /// 顯示或更新。anchor：正在選字的那段文字（螢幕座標）；視窗放在它下方，放不下就放上方，
    /// 不超出螢幕工作區。owner 必須是同一個執行緒的視窗，或 nullptr。
    void show(const CandidatePage& page, const RECT& anchor, HWND owner);
    /// 只換位置（例如程式的版面改變）。
    void move(const RECT& anchor);
    void hide();
    bool isShown() const;
    HWND hwnd() const { return hwnd_; }
    void setClickHandler(ClickHandler handler) { onClick_ = std::move(handler); }
    /// testpad 用：第一次顯示前呼叫，不用 HWND 的繪圖目標，直接走沉浸式程式的路徑。
    void forceBitmapTarget() { useBitmap_ = true; }

    /// 視窗類別名稱（testpad 用來找視窗）。
    static constexpr const wchar_t* kClassName = L"BangbangZhuyinCandidateWindow";
    /// DLL 被卸載時呼叫（DllMain，FreeLibrary 的情況）：視窗類別的視窗程序在 DLL 裡，不能留下來。
    static void unregisterClass();

private:
    bool ensureWindow(HWND owner);
    bool createTarget();
    /// 讓 DWM 暫時不顯示這個視窗（視窗仍然算是顯示中，可以照常畫）。
    void cloak(bool hidden);
    void place();
    void paint();
    /// 畫在 WIC 點陣圖上（大小跟著視窗），再用 SetDIBitsToDevice 貼到 dc。
    void paintBitmap(HDC dc);
    UINT dpiFor(const RECT& anchor) const;
    LRESULT handleMessage(UINT message, WPARAM wParam, LPARAM lParam);
    friend LRESULT CALLBACK candidateWindowProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam);

    HWND hwnd_ = nullptr;
    CandidatePage page_;
    CandidateTheme theme_;
    RECT anchor_{};
    UINT dpi_ = 96;
    CandidateRenderer renderer_;
    Microsoft::WRL::ComPtr<ID2D1Factory> d2d_;
    Microsoft::WRL::ComPtr<ID2D1HwndRenderTarget> target_;
    // 沉浸式程式用的繪圖目標（useBitmap_ 時）。
    bool useBitmap_ = false;
    Microsoft::WRL::ComPtr<IWICImagingFactory> wic_;
    Microsoft::WRL::ComPtr<IWICBitmap> bitmap_;
    Microsoft::WRL::ComPtr<ID2D1RenderTarget> bitmapTarget_;
    ClickHandler onClick_;
};

}  // namespace bangbang
