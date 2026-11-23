#pragma once

// 對應 Sources/BangbangEngine/InputSession.swift 裡的 Brackets 與 Punctuation。
// 分成獨立檔案，是因為 Decoder（符號候選）與 Composition（括號配對）也要用到。

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace bangbang {

/// 成對的括號。
namespace Brackets {
/// bracket 是括號前半時，回傳它的後半。
std::optional<std::string> closing(std::string_view bracket);
/// bracket 是括號後半時，回傳它的前半。
std::optional<std::string> opening(std::string_view bracket);
/// 把括號 old 換成 replacement 時，配對的另一半應該換成什麼；replacement 不是同一邊的括號時回傳 nullopt。
std::optional<std::string> counterpart(std::string_view old, std::string_view replacement);
}  // namespace Brackets

/// 標點符號。
namespace Punctuation {
/// 標點的配置。
enum class Style {
    /// Mac 版（仿微軟新注音／小麥注音的標準大千配置）：Shift＋標點是全形，例如 Shift+, → ，。
    Mac,
    /// Windows 版（2026-11-23 使用者指定，比照 Windows 11 的微軟注音）：Shift＋標點是半形（Shift+, → <），
    /// 全形標點改用 Ctrl（Ctrl+, → ，），[ ] ' 也是半形。對照表見 windows/README.md。
    Windows,
};

/// 沒按 Shift 時的標點，例如 Mac 配置的 ' → 、。
std::optional<std::string> unshifted(char key, Style style = Style::Mac);
/// 按著 Shift 時的標點，例如 Mac 配置的 Shift+, → ，、Windows 配置的 Shift+, → <。
std::optional<std::string> shifted(char key, Style style = Style::Mac);
/// 按著 Ctrl 時的標點（只有 Windows 配置有），例如 Ctrl+, → ，。
std::optional<std::string> control(char key, Style style = Style::Mac);
/// 某個符號的候選清單：自己排第一，其餘照組內順序。優先用「以它開頭」的那一組，
/// 沒有的話用第一個含有它的組。不在任何一組時回傳空陣列。
std::vector<std::string> alternatives(std::string_view symbol, Style style = Style::Mac);
/// 符號候選的分組（測試用）。
const std::vector<std::vector<std::string>>& groups(Style style = Style::Mac);
}  // namespace Punctuation

}  // namespace bangbang
