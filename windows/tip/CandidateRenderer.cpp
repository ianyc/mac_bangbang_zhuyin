#include "CandidateRenderer.h"

#include <algorithm>
#include <cmath>

using Microsoft::WRL::ComPtr;

namespace bangbang {

namespace {

constexpr float kRowHeight = 30.0f;
constexpr float kPadding = 6.0f;
constexpr float kNumberWidth = 22.0f;
constexpr float kCandidateFontSize = 20.0f;
constexpr float kNumberFontSize = 13.0f;
constexpr float kFooterFontSize = 11.0f;
constexpr float kFooterHeight = 18.0f;
constexpr float kMinWidth = 60.0f;
constexpr float kHighlightRadius = 5.0f;

/// 0xRRGGBB → D2D 顏色。
D2D1_COLOR_F rgb(UINT32 value) {
    return D2D1::ColorF(value);
}

D2D1_COLOR_F fromColorRef(COLORREF c) {
    return D2D1::ColorF(GetRValue(c) / 255.0f, GetGValue(c) / 255.0f, GetBValue(c) / 255.0f);
}

/// 依亮度決定反白上的字用黑或白，確保看得清楚。
D2D1_COLOR_F textOn(COLORREF c) {
    const float luminance = 0.2126f * GetRValue(c) / 255.0f + 0.7152f * GetGValue(c) / 255.0f + 0.0722f * GetBValue(c) / 255.0f;
    return luminance > 0.55f ? rgb(0x000000) : rgb(0xFFFFFF);
}

bool readDword(HKEY root, const wchar_t* path, const wchar_t* name, DWORD& value) {
    DWORD size = sizeof(value);
    return RegGetValueW(root, path, name, RRF_RT_REG_DWORD, nullptr, &value, &size) == ERROR_SUCCESS;
}

std::wstring footerText(const CandidatePage& page) {
    return std::to_wstring(page.page + 1) + L"/" + std::to_wstring(page.pageCount);
}

HRESULT makeFormat(IDWriteFactory* factory, const wchar_t* family, float size, DWRITE_TEXT_ALIGNMENT alignment,
                   IDWriteTextFormat** format) {
    HRESULT hr = factory->CreateTextFormat(family, nullptr, DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STYLE_NORMAL,
                                           DWRITE_FONT_STRETCH_NORMAL, size, L"zh-TW", format);
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
    theme.footer = dark ? rgb(0x9E9E9E) : rgb(0x8A8A8A);
    theme.highlight = fromColorRef(accent);
    theme.highlightText = textOn(accent);
    return theme;
}

CandidateTheme CandidateTheme::current() {
    HIGHCONTRASTW contrast{sizeof(contrast)};
    if (SystemParametersInfoW(SPI_GETHIGHCONTRAST, sizeof(contrast), &contrast, 0) && (contrast.dwFlags & HCF_HIGHCONTRASTON)) {
        CandidateTheme theme;
        theme.background = fromColorRef(GetSysColor(COLOR_WINDOW));
        theme.text = theme.number = theme.footer = fromColorRef(GetSysColor(COLOR_WINDOWTEXT));
        theme.highlight = fromColorRef(GetSysColor(COLOR_HIGHLIGHT));
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
    ComPtr<IDWriteTextFormat> candidate, number, footer;
    hr = makeFormat(dwrite_.Get(), L"Microsoft JhengHei UI", kCandidateFontSize, DWRITE_TEXT_ALIGNMENT_LEADING, &candidate);
    if (SUCCEEDED(hr)) hr = makeFormat(dwrite_.Get(), L"Segoe UI", kNumberFontSize, DWRITE_TEXT_ALIGNMENT_LEADING, &number);
    if (SUCCEEDED(hr)) hr = makeFormat(dwrite_.Get(), L"Segoe UI", kFooterFontSize, DWRITE_TEXT_ALIGNMENT_TRAILING, &footer);
    if (FAILED(hr)) return hr;
    candidateFormat_ = candidate;
    numberFormat_ = number;
    footerFormat_ = footer;
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
    float width = kMinWidth;
    for (const auto& item : page.items) width = std::max(width, kNumberWidth + textWidth(candidateFormat_.Get(), item) + kPadding * 3);
    const bool footer = page.pageCount > 1;
    if (footer) width = std::max(width, textWidth(footerFormat_.Get(), footerText(page)) + kPadding * 2);
    const float height = static_cast<float>(page.items.size()) * kRowHeight + kPadding * 2 + (footer ? kFooterHeight : 0.0f);
    return D2D1::SizeF(std::ceil(width), std::ceil(height));
}

void CandidateRenderer::draw(ID2D1RenderTarget* target, const CandidatePage& page, const CandidateTheme& theme) const {
    if (!ready() || !target) return;
    const D2D1_SIZE_F size = target->GetSize();
    target->Clear(theme.background);
    ComPtr<ID2D1SolidColorBrush> brush;
    if (FAILED(target->CreateSolidColorBrush(theme.text, &brush))) return;
    for (std::size_t row = 0; row < page.items.size(); ++row) {
        const float top = kPadding + static_cast<float>(row) * kRowHeight;
        const D2D1_RECT_F rowRect = D2D1::RectF(kPadding / 2, top, size.width - kPadding / 2, top + kRowHeight);
        const bool highlighted = static_cast<int>(row) == page.highlighted;
        if (highlighted) {
            brush->SetColor(theme.highlight);
            target->FillRoundedRectangle(D2D1::RoundedRect(rowRect, kHighlightRadius, kHighlightRadius), brush.Get());
        }
        const std::wstring number = std::to_wstring(row + 1);
        brush->SetColor(highlighted ? theme.highlightText : theme.number);
        target->DrawText(number.c_str(), static_cast<UINT32>(number.size()), numberFormat_.Get(),
                         D2D1::RectF(rowRect.left + kPadding, rowRect.top, rowRect.left + kPadding + kNumberWidth, rowRect.bottom), brush.Get());
        const std::wstring& text = page.items[row];
        brush->SetColor(highlighted ? theme.highlightText : theme.text);
        target->DrawText(text.c_str(), static_cast<UINT32>(text.size()), candidateFormat_.Get(),
                         D2D1::RectF(rowRect.left + kPadding + kNumberWidth, rowRect.top, rowRect.right, rowRect.bottom), brush.Get());
    }
    if (page.pageCount > 1) {
        const std::wstring footer = footerText(page);
        brush->SetColor(theme.footer);
        target->DrawText(footer.c_str(), static_cast<UINT32>(footer.size()), footerFormat_.Get(),
                         D2D1::RectF(kPadding, size.height - kFooterHeight - 2.0f, size.width - kPadding, size.height - 2.0f), brush.Get());
    }
}

int CandidateRenderer::rowAt(const CandidatePage& page, D2D1_POINT_2F point) const {
    if (point.y < kPadding) return -1;
    const int row = static_cast<int>((point.y - kPadding) / kRowHeight);
    return row < static_cast<int>(page.items.size()) ? row : -1;
}

}  // namespace bangbang
