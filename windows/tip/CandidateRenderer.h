#pragma once

// 候選窗的內容：量測大小與繪製（Direct2D＋DirectWrite，單位一律是 DIP）。
// 候選窗視窗（CandidateWindow）與 testpad 的 PNG 輸出共用這份程式。
// 版面比照 Sources/BangbangZhuyin/CandidatePanel.swift：直式清單、左側 1–9 編號、反白圓角、
// 多頁時右下角顯示頁碼；列高 30、邊距 6、編號欄 22、候選字 20、編號 13、頁碼 11。

#include <windows.h>
#include <d2d1.h>
#include <dwrite.h>
#include <wrl/client.h>

#include <string>
#include <vector>

namespace bangbang {

/// 候選窗一頁的內容。
struct CandidatePage {
    std::vector<std::wstring> items;
    /// 本頁裡反白的位置；-1 表示沒有。
    int highlighted = -1;
    /// 第幾頁（從 0 起算）與總頁數。
    int page = 0;
    int pageCount = 1;

    friend bool operator==(const CandidatePage&, const CandidatePage&) = default;
};

/// 候選窗的顏色。
struct CandidateTheme {
    bool dark = false;
    D2D1_COLOR_F background{};
    D2D1_COLOR_F text{};
    D2D1_COLOR_F number{};
    D2D1_COLOR_F footer{};
    D2D1_COLOR_F highlight{};
    D2D1_COLOR_F highlightText{};

    /// 依系統設定：應用程式的淺色／深色模式，反白用系統的強調色；高對比模式用系統色。
    static CandidateTheme current();
    static CandidateTheme make(bool dark, COLORREF accent);
    /// 系統的強調色（讀不到時用 COLOR_HIGHLIGHT）。
    static COLORREF systemAccent();
};

class CandidateRenderer {
public:
    /// 建立 DirectWrite 的字型格式。失敗時之後的 measure／draw 什麼都不做。
    HRESULT initialize();
    bool ready() const { return candidateFormat_ != nullptr; }
    /// 這一頁需要的大小（DIP）。
    D2D1_SIZE_F measure(const CandidatePage& page) const;
    /// 畫滿整個 target（target 的 DPI 要先設好，座標用 DIP）。
    void draw(ID2D1RenderTarget* target, const CandidatePage& page, const CandidateTheme& theme) const;
    /// 點（DIP 座標）落在本頁第幾列；不在任何一列上時回傳 -1。
    int rowAt(const CandidatePage& page, D2D1_POINT_2F point) const;

private:
    float textWidth(IDWriteTextFormat* format, const std::wstring& text) const;

    Microsoft::WRL::ComPtr<IDWriteFactory> dwrite_;
    Microsoft::WRL::ComPtr<IDWriteTextFormat> candidateFormat_;
    Microsoft::WRL::ComPtr<IDWriteTextFormat> numberFormat_;
    Microsoft::WRL::ComPtr<IDWriteTextFormat> footerFormat_;
};

}  // namespace bangbang
