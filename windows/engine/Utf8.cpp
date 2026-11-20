#include "Utf8.h"

static_assert(sizeof(wchar_t) == 2, "Windows 的 wchar_t 是 UTF-16");

namespace bangbang::utf8 {

char32_t decode(std::string_view s, std::size_t& i) {
    const auto byteAt = [&](std::size_t k) { return static_cast<unsigned char>(s[k]); };
    const unsigned char b0 = byteAt(i);
    if (b0 < 0x80) {
        ++i;
        return b0;
    }
    std::size_t extra = 0;
    char32_t c = 0;
    char32_t minimum = 0;
    if ((b0 & 0xE0) == 0xC0) {
        extra = 1, c = b0 & 0x1F, minimum = 0x80;
    } else if ((b0 & 0xF0) == 0xE0) {
        extra = 2, c = b0 & 0x0F, minimum = 0x800;
    } else if ((b0 & 0xF8) == 0xF0) {
        extra = 3, c = b0 & 0x07, minimum = 0x10000;
    } else {
        ++i;
        return 0xFFFD;
    }
    if (i + extra >= s.size()) {
        ++i;
        return 0xFFFD;
    }
    for (std::size_t k = 1; k <= extra; ++k) {
        const unsigned char b = byteAt(i + k);
        if ((b & 0xC0) != 0x80) {
            ++i;
            return 0xFFFD;
        }
        c = (c << 6) | (b & 0x3F);
    }
    if (c < minimum || c > 0x10FFFF || (c >= 0xD800 && c <= 0xDFFF)) {
        ++i;
        return 0xFFFD;
    }
    i += extra + 1;
    return c;
}

void append(std::string& out, char32_t c) {
    if (c > 0x10FFFF || (c >= 0xD800 && c <= 0xDFFF)) c = 0xFFFD;
    if (c < 0x80) {
        out.push_back(static_cast<char>(c));
    } else if (c < 0x800) {
        out.push_back(static_cast<char>(0xC0 | (c >> 6)));
        out.push_back(static_cast<char>(0x80 | (c & 0x3F)));
    } else if (c < 0x10000) {
        out.push_back(static_cast<char>(0xE0 | (c >> 12)));
        out.push_back(static_cast<char>(0x80 | ((c >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (c & 0x3F)));
    } else {
        out.push_back(static_cast<char>(0xF0 | (c >> 18)));
        out.push_back(static_cast<char>(0x80 | ((c >> 12) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | ((c >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (c & 0x3F)));
    }
}

std::string encode(char32_t c) {
    std::string out;
    append(out, c);
    return out;
}

std::size_t length(std::string_view s) {
    std::size_t n = 0;
    for (std::size_t i = 0; i < s.size(); ++n) decode(s, i);
    return n;
}

std::string_view prefix(std::string_view s, std::size_t n) {
    std::size_t i = 0;
    for (std::size_t k = 0; k < n && i < s.size(); ++k) decode(s, i);
    return s.substr(0, i);
}

std::string_view dropFirst(std::string_view s, std::size_t n) {
    return s.substr(prefix(s, n).size());
}

std::vector<std::string> characters(std::string_view s) {
    std::vector<std::string> result;
    for (std::size_t i = 0; i < s.size();) {
        const std::size_t start = i;
        decode(s, i);
        result.emplace_back(s.substr(start, i - start));
    }
    return result;
}

bool isAscii(std::string_view s) {
    for (const char ch : s) {
        if (static_cast<unsigned char>(ch) >= 0x80) return false;
    }
    return true;
}

std::wstring toWide(std::string_view s) {
    std::wstring out;
    out.reserve(s.size());
    for (std::size_t i = 0; i < s.size();) {
        const char32_t c = decode(s, i);
        if (c >= 0x10000) {
            const char32_t v = c - 0x10000;
            out.push_back(static_cast<wchar_t>(0xD800 + (v >> 10)));
            out.push_back(static_cast<wchar_t>(0xDC00 + (v & 0x3FF)));
        } else {
            out.push_back(static_cast<wchar_t>(c));
        }
    }
    return out;
}

std::string fromWide(std::wstring_view s) {
    std::string out;
    out.reserve(s.size() * 3);
    for (std::size_t i = 0; i < s.size(); ++i) {
        const char32_t unit = static_cast<char16_t>(s[i]);
        if (unit >= 0xD800 && unit < 0xDC00 && i + 1 < s.size()) {
            const char32_t low = static_cast<char16_t>(s[i + 1]);
            if (low >= 0xDC00 && low < 0xE000) {
                append(out, 0x10000 + ((unit - 0xD800) << 10) + (low - 0xDC00));
                ++i;
                continue;
            }
        }
        append(out, unit);  // 落單的代理字元會在 append 裡變成 U+FFFD
    }
    return out;
}

std::size_t utf16Offset(std::string_view s, std::size_t n) {
    std::size_t units = 0;
    std::size_t i = 0;
    for (std::size_t k = 0; k < n && i < s.size(); ++k) units += decode(s, i) >= 0x10000 ? 2 : 1;
    return units;
}

}  // namespace bangbang::utf8
