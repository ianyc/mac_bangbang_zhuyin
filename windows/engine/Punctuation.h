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

/// 標點符號（仿微軟新注音／小麥注音的標準大千配置）。
namespace Punctuation {
/// 沒按 Shift 時的標點，例如 ' → 、。
std::optional<std::string> unshifted(char key);
/// 按著 Shift 時的標點，例如 Shift+, → ，。
std::optional<std::string> shifted(char key);
/// 某個符號的候選清單：自己排第一，其餘照組內順序。不在任何一組時回傳空陣列。
std::vector<std::string> alternatives(std::string_view symbol);
/// 符號候選的分組（測試用）。
const std::vector<std::vector<std::string>>& groups();
}  // namespace Punctuation

}  // namespace bangbang
