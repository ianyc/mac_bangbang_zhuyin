#include "Punctuation.h"

#include <algorithm>
#include <utility>

namespace bangbang {

namespace {

const std::vector<std::pair<std::string, std::string>>& bracketPairs() {
    static const std::vector<std::pair<std::string, std::string>> pairs = {
        {"「", "」"}, {"『", "』"}, {"（", "）"}, {"〈", "〉"}, {"《", "》"}, {"【", "】"}, {"〔", "〕"},
        {"［", "］"}, {"｛", "｝"}, {"“", "”"}, {"‘", "’"}, {"(", ")"}, {"[", "]"}, {"{", "}"},
    };
    return pairs;
}

std::optional<std::string> lookupKey(const std::vector<std::pair<char, std::string>>& table, char key) {
    for (const auto& [k, value] : table) {
        if (k == key) return value;
    }
    return std::nullopt;
}

}  // namespace

namespace Brackets {

std::optional<std::string> closing(std::string_view bracket) {
    for (const auto& [open, close] : bracketPairs()) {
        if (open == bracket) return close;
    }
    return std::nullopt;
}

std::optional<std::string> opening(std::string_view bracket) {
    for (const auto& [open, close] : bracketPairs()) {
        if (close == bracket) return open;
    }
    return std::nullopt;
}

std::optional<std::string> counterpart(std::string_view old, std::string_view replacement) {
    if (closing(old)) return closing(replacement);
    if (opening(old)) return opening(replacement);
    return std::nullopt;
}

}  // namespace Brackets

namespace Punctuation {

std::optional<std::string> unshifted(char key) {
    static const std::vector<std::pair<char, std::string>> table = {
        {'\'', "、"}, {'[', "「"}, {']', "」"}, {'`', "`"}, {'=', "="}, {'\\', "\\"},
    };
    return lookupKey(table, key);
}

std::optional<std::string> shifted(char key) {
    static const std::vector<std::pair<char, std::string>> table = {
        {',', "，"}, {'.', "。"}, {'/', "？"}, {';', "："}, {'\'', "；"},
        {'1', "！"}, {'2', "@"}, {'3', "#"}, {'4', "$"}, {'5', "%"}, {'6', "^"}, {'7', "&"}, {'8', "*"},
        {'9', "（"}, {'0', "）"}, {'-', "_"}, {'=', "+"},
        {'[', "『"}, {']', "』"}, {'\\', "|"}, {'`', "～"},
    };
    return lookupKey(table, key);
}

/// 符號候選：同一組裡是該符號的全形／半形，以及同一個鍵上的另一個符號（含全形／半形）。
/// 例如「：」的鍵上還有「；」，所以候選是 ：:；;。每個符號都必須是單一字元。
const std::vector<std::vector<std::string>>& groups() {
    static const std::vector<std::vector<std::string>> table = {
        {"，", ",", "＜", "<", "〈", "《"},
        {"。", ".", "．", "＞", ">", "〉", "》"},
        {"？", "?", "／", "/"},
        {"：", ":", "；", ";"},
        {"、", "'", "；", "＂", "\"", "“", "”", "‘", "’"},
        {"！", "!", "１", "1"},
        {"@", "＠", "２", "2"},
        {"#", "＃", "３", "3"},
        {"$", "＄", "４", "4"},
        {"%", "％", "５", "5"},
        {"^", "＾", "︿", "６", "6"},
        {"&", "＆", "７", "7"},
        {"*", "＊", "８", "8"},
        {"（", "(", "９", "9"},
        {"）", ")", "０", "0"},
        {"_", "＿", "-", "－", "—"},
        {"+", "＋", "=", "＝"},
        {"「", "『", "[", "{", "［", "｛", "【", "〔"},
        {"」", "』", "]", "}", "］", "｝", "】", "〕"},
        {"～", "~", "`", "‵"},
        {"\\", "＼", "|", "｜"},
        {" ", "　"},
    };
    return table;
}

std::vector<std::string> alternatives(std::string_view symbol) {
    for (const auto& group : groups()) {
        if (std::find(group.begin(), group.end(), symbol) == group.end()) continue;
        std::vector<std::string> result{std::string(symbol)};
        for (const auto& s : group) {
            if (s != symbol) result.push_back(s);
        }
        return result;
    }
    return {};
}

}  // namespace Punctuation

}  // namespace bangbang
