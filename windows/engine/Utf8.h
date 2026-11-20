#pragma once

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

// UTF-8 工具。引擎內部的文字一律是 UTF-8，組字區的一格就是一個 Unicode 碼位
// （Mac 版用 Swift 的 Character 計數；詞庫裡沒有組合字元，所以兩者一致）。
// TSF 用 UTF-16，BMP 以外的字（例如「𨑨迌」的「𨑨」）佔 2 個單位，外殼要用 utf16Offset 換算位置。
namespace bangbang::utf8 {

/// 從 s[i] 解出一個碼位，並把 i 移到下一個碼位。格式錯誤的位元組當成 U+FFFD，只前進 1 個位元組。
char32_t decode(std::string_view s, std::size_t& i);

/// 把一個碼位以 UTF-8 加到 out 後面。
void append(std::string& out, char32_t c);
std::string encode(char32_t c);

/// 碼位數。
std::size_t length(std::string_view s);
/// 前 n 個碼位；超過長度時回傳整個字串。
std::string_view prefix(std::string_view s, std::size_t n);
/// 去掉前 n 個碼位。
std::string_view dropFirst(std::string_view s, std::size_t n);
/// 拆成一個一個碼位。
std::vector<std::string> characters(std::string_view s);
bool isAscii(std::string_view s);

/// 轉成 Windows 的 UTF-16（wchar_t）。
std::wstring toWide(std::string_view s);
/// 從 UTF-16 轉回 UTF-8；落單的代理字元當成 U+FFFD。
std::string fromWide(std::wstring_view s);
/// 前 n 個碼位在 UTF-16 裡佔幾個單位。
std::size_t utf16Offset(std::string_view s, std::size_t n);

}  // namespace bangbang::utf8
