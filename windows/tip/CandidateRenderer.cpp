#include "CandidateRenderer.h"

#include <algorithm>
#include <cmath>

using Microsoft::WRL::ComPtr;

namespace bangbang {

namespace {

// 版面（DIP），比照 Windows 11 微軟注音的候選窗。
constexpr float kRowHeight = 32.0f;
constexpr float kRowsTop = 2.0f;       // 第一列上方、最後一列下方的空白
constexpr float kMinWidth = 140.0f;
constexpr float kNumberLeft = 16.0f;   // 編號的左邊
constexpr float kTextLeft = 31.0f;     // 候選字的左邊
constexpr float kTextRight = 14.0f;    // 只有一頁時，候選字右邊至少留的空白
constexpr float kCandidateFontSize = 15.0f;
constexpr float kNumberFontSize = 15.0f;
// 反白：淺灰圓角底，左側一條強調色短線。
constexpr float kHighlightLeft = 3.0f;
constexpr float kHighlightRight = 4.0f;  // 距右邊
constexpr float kHighlightInset = 2.0f;  // 上下各縮進
constexpr float kHighlightRadius = 4.0f;
constexpr float kIndicatorWidth = 3.0f;
constexpr float kIndicatorHeight = 16.0f;
// 頁數圓點：右側直排、整串上下置中；目前這一頁的點比較大。頁數太多時最多畫 5 個，依比例對應。
constexpr float kDotsFromRight = 11.5f;  // 圓心距右邊
constexpr float kDotsReserve = 26.0f;    // 有圓點時，候選字右邊要讓出的寬度
constexpr float kDotSpacing = 12.0f;
constexpr float kActiveDot = 6.0f;
constexpr float kDot = 4.0f;
constexpr int kMaxDots = 5;

/// 0xRRGGBB → D2D 顏色。
D2D1_COLOR_F rgb(UINT32 value) {
    return D2D1::ColorF(value);
}

D2D1_COLOR_F fromColorRef(COLORREF c) {
    return D2D1::ColorF(GetRValue(c) / 255.0f, GetGValue(c) / 255.0f, GetBValue(c) / 255.0f);
}

/// 深色模式的短線用比較亮的強調色（同 Windows 在深色背景上的做法），才看得清楚。
D2D1_COLOR_F lighter(COLORREF c, float amount) {
    const auto mix = [&](BYTE v) { return (v + (255 - v) * amount) / 255.0f; };
    return D2D1::ColorF(mix(GetRValue(c)), mix(GetGValue(c)), mix(GetBValue(c)));
}

bool readDword(HKEY root, const wchar_t* path, const wchar_t* name, DWORD& value) {
    DWORD size = sizeof(value);
    return RegGetValueW(root, path, name, RRF_RT_REG_DWORD, nullptr, &value, &size) == ERROR_SUCCESS;
}

HRESULT makeFormat(IDWriteFactory* factory, const wchar_t* family, DWRITE_FONT_WEIGHT weight, float size,
                   DWRITE_TEXT_ALIGNMENT alignment, IDWriteTextFormat** format) {
    HRESULT hr = factory->CreateTextFormat(family, nullptr, weight, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, size, L"zh-TW", format);
    if (FAILED(hr)) return hr;
    (*format)->SetTextAlignment(alignment);
    (*format)->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
    (*format)->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
    return S_OK;
}

}  // namespace

// MARK: - 顏色

CandidateTheme CandidateTheme::make(bool dark, COLORREF accent) {
    CandidateTheme theme;
    theme.dark = dark;
    theme.background = dark ? rgb(0x2C2C2C) : rgb(0xF9F9F9);
    theme.text = dark ? rgb(0xFFFFFF) : rgb(0x1A1A1A);
    theme.number = dark ? rgb(0xC8C8C8) : rgb(0x5C5C5C);
    theme.pageDots = dark ? rgb(0x9E9E9E) : rgb(0x8A8A8A);
    theme.highlight = dark ? rgb(0x3A3A3A) : rgb(0xF0F0F0);
    theme.highlightText = theme.text;
    theme.indicator = dark ? lighter(accent, 0.45f) : fromColorRef(accent);
    return theme;
}

CandidateTheme CandidateTheme::current() {
    HIGHCONTRASTW contrast{sizeof(contrast)};
    if (SystemParametersInfoW(SPI_GETHIGHCONTRAST, sizeof(contrast), &contrast, 0) && (contrast.dwFlags & HCF_HIGHCONTRASTON)) {
        CandidateTheme theme;
        theme.highContrast = true;
        theme.background = fromColorRef(GetSysColor(COLOR_WINDOW));
        theme.text = theme.number = theme.pageDots = fromColorRef(GetSysColor(COLOR_WINDOWTEXT));
        theme.highlight = theme.indicator = fromColorRef(GetSysColor(COLOR_HIGHLIGHT));
        theme.highlightText = fromColorRef(GetSysColor(COLOR_HIGHLIGHTTEXT));
        return theme;
    }
    DWORD light = 1;
    readDword(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize", L"AppsUseLightTheme", light);
    return make(light == 0, systemAccent());
}

COLORREF CandidateTheme::systemAccent() {
    // 強調色存成 0xAABBGGRR，和 COLORREF（0x00BBGGRR）的位元組順序相同。
    DWORD accent = 0;
    if (readDword(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\DWM", L"AccentColor", accent)) return static_cast<COLORREF>(accent & 0x00FFFFFF);
    return GetSysColor(COLOR_HIGHLIGHT);
}

// MARK: - 量測與繪製

HRESULT CandidateRenderer::initialize() {
    HRESULT hr = DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory), reinterpret_cast<IUnknown**>(dwrite_.GetAddressOf()));
    if (FAILED(hr)) return hr;
    ComPtr<IDWriteTextFormat> candidate, number;
    hr = makeFormat(dwrite_.Get(), L"Microsoft JhengHei UI", DWRITE_FONT_WEIGHT_NORMAL, kCandidateFontSize, DWRITE_TEXT_ALIGNMENT_LEADING,
                    &candidate);
    // 編號用細一點的字重，同微軟注音。
    if (SUCCEEDED(hr)) {
        hr = makeFormat(dwrite_.Get(), L"Segoe UI", DWRITE_FONT_WEIGHT_SEMI_LIGHT, kNumberFontSize, DWRITE_TEXT_ALIGNMENT_LEADING, &number);
    }
    if (FAILED(hr)) return hr;
    candidateFormat_ = candidate;
    numberFormat_ = number;
    return S_OK;
}

float CandidateRenderer::textWidth(IDWriteTextFormat* format, const std::wstring& text) const {
    ComPtr<IDWriteTextLayout> layout;
    if (FAILED(dwrite_->CreateTextLayout(text.c_str(), static_cast<UINT32>(text.size()), format, 10000.0f, 100.0f, &layout))) return 0.0f;
    DWRITE_TEXT_METRICS metrics{};
    if (FAILED(layout->GetMetrics(&metrics))) return 0.0f;
    return metrics.widthIncludingTrailingWhitespace;
}

D2D1_SIZE_F CandidateRenderer::measure(const CandidatePage& page) const {
    if (!ready()) return D2D1::SizeF(0.0f, 0.0f);
    const float right = page.pageCount > 1 ? kDotsReserve : kTextRight;
    float width = kMinWidth;
    for (const auto& item : page.items) width = std::max(width, kTextLeft + textWidth(candidateFormat_.Get(), item) + right);
    const float height = static_cast<float>(page.items.size()) * kRowHeight + kRowsTop * 2;
    return D2D1::SizeF(std::ceil(width), std::ceil(height));
}

void CandidateRenderer::draw(ID2D1RenderTarget* target, const CandidatePage& page, const CandidateTheme& theme) const {
    if (!ready() || !target) return;
    const D2D1_SIZE_F size = target->GetSize();
    target->Clear(theme.background);
    ComPtr<ID2D1SolidColorBrush> brush;
    if (FAILED(target->CreateSolidColorBrush(theme.text, &brush))) return;
    for (std::size_t row = 0; row < page.items.size(); ++row) {
        const float top = kRowsTop + static_cast<float>(row) * kRowHeight;
        const D2D1_RECT_F rowRect = D2D1::RectF(0.0f, top, size.width, top + kRowHeight);
        const bool highlighted = static_cast<int>(row) == page.highlighted;
        if (highlighted) {
            const D2D1_RECT_F box = D2D1::RectF(kHighlightLeft, top + kHighlightInset, size.width - kHighlightRight, top + kRowHeight - kHighlightInset);
            brush->SetColor(theme.highlight);
            target->FillRoundedRectangle(D2D1::RoundedRect(box, kHighlightRadius, kHighlightRadius), brush.Get());
            if (!theme.highContrast) {
                const float middle = top + kRowHeight / 2;
                const D2D1_RECT_F bar = D2D1::RectF(kHighlightLeft, middle - kIndicatorHeight / 2, kHighlightLeft + kIndicatorWidth,
                                                    middle + kIndicatorHeight / 2);
                brush->SetColor(theme.indicator);
                target->FillRoundedRectangle(D2D1::RoundedRect(bar, kIndicatorWidth / 2, kIndicatorWidth / 2), brush.Get());
            }
        }
        const std::wstring number = std::to_wstring(row + 1);
        brush->SetColor(highlighted && theme.highContrast ? theme.highlightText : theme.number);
        target->DrawText(number.c_str(), static_cast<UINT32>(number.size()), numberFormat_.Get(),
                         D2D1::RectF(kNumberLeft, rowRect.top, kTextLeft, rowRect.bottom), brush.Get());
        const std::wstring& text = page.items[row];
        brush->SetColor(highlighted ? theme.highlightText : theme.text);
        target->DrawText(text.c_str(), static_cast<UINT32>(text.size()), candidateFormat_.Get(),
                         D2D1::RectF(kTextLeft, rowRect.top, size.width, rowRect.bottom), brush.Get());
    }
    if (page.pageCount > 1) {
        // 頁數太多時只畫 5 個點，目前這一頁依比例對應到其中一個（第一頁是第一個，最後一頁是最後一個）。
        const int dots = std::min(page.pageCount, kMaxDots);
        const int active = page.pageCount <= kMaxDots ? page.page : page.page * dots / page.pageCount;
        const float centerX = size.width - kDotsFromRight;
        const float middle = kRowsTop + static_cast<float>(page.items.size()) * kRowHeight / 2;
        const float firstY = middle - static_cast<float>(dots - 1) * kDotSpacing / 2;
        brush->SetColor(theme.pageDots);
        for (int i = 0; i < dots; ++i) {
            const float radius = (i == active ? kActiveDot : kDot) / 2;
            target->FillEllipse(D2D1::Ellipse(D2D1::Point2F(centerX, firstY + static_cast<float>(i) * kDotSpacing), radius, radius), brush.Get());
        }
    }
}

int CandidateRenderer::rowAt(const CandidatePage& page, D2D1_POINT_2F point) const {
    if (point.y < kRowsTop) return -1;
    const int row = static_cast<int>((point.y - kRowsTop) / kRowHeight);
    return row < static_cast<int>(page.items.size()) ? row : -1;
}

}  // namespace bangbang
