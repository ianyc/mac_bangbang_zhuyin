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

std::optional<std::string> unshifted(char key, Style style) {
    static const std::vector<std::pair<char, std::string>> mac = {
        {'\'', "、"}, {'[', "「"}, {']', "」"}, {'`', "`"}, {'=', "="}, {'\\', "\\"},
    };
    static const std::vector<std::pair<char, std::string>> windows = {
        {'\'', "'"}, {'[', "["}, {']', "]"}, {'`', "`"}, {'=', "="}, {'\\', "\\"},
    };
    return lookupKey(style == Style::Windows ? windows : mac, key);
}

std::optional<std::string> shifted(char key, Style style) {
    static const std::vector<std::pair<char, std::string>> mac = {
        {',', "，"}, {'.', "。"}, {'/', "？"}, {';', "："}, {'\'', "；"},
        {'1', "！"}, {'2', "@"}, {'3', "#"}, {'4', "$"}, {'5', "%"}, {'6', "^"}, {'7', "&"}, {'8', "*"},
        {'9', "（"}, {'0', "）"}, {'-', "_"}, {'=', "+"},
        {'[', "『"}, {']', "』"}, {'\\', "|"}, {'`', "～"},
    };
    // Windows 配置：和美式鍵盤按 Shift 打出的半形符號相同（[ ] 仍是全形的『』）。
    static const std::vector<std::pair<char, std::string>> windows = {
        {',', "<"}, {'.', ">"}, {'/', "?"}, {';', ":"}, {'\'', "\""},
        {'1', "!"}, {'2', "@"}, {'3', "#"}, {'4', "$"}, {'5', "%"}, {'6', "^"}, {'7', "&"}, {'8', "*"},
        {'9', "("}, {'0', ")"}, {'-', "_"}, {'=', "+"},
        {'[', "『"}, {']', "』"}, {'\\', "|"}, {'`', "~"},
    };
    return lookupKey(style == Style::Windows ? windows : mac, key);
}

std::optional<std::string> control(char key, Style style) {
    if (style != Style::Windows) return std::nullopt;
    static const std::vector<std::pair<char, std::string>> windows = {
        {',', "，"}, {'.', "。"}, {';', "；"}, {'\'', "、"}, {'[', "【"}, {']', "】"},
    };
    return lookupKey(windows, key);
}

/// 符號候選：同一組裡是該符號的全形／半形，以及同一個鍵上的另一個符號（含全形／半形）。
/// 例如「：」的鍵上還有「；」，所以候選是 ：:；;。每個符號都必須是單一字元。
const std::vector<std::vector<std::string>>& groups(Style style) {
    static const std::vector<std::vector<std::string>> mac = {
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
    // Windows 配置：Ctrl+, 的「，」第二個候選是「、」；Ctrl+[ 的「【」之後是「「」「『」，Ctrl+] 同理。
    static const std::vector<std::vector<std::string>> windows = {
        {"，", "、", ",", "＜", "<", "〈", "《"},
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
        {"【", "「", "『", "[", "{", "［", "｛", "〔"},
        {"】", "」", "』", "]", "}", "］", "｝", "〕"},
        {"～", "~", "`", "‵"},
        {"\\", "＼", "|", "｜"},
        {" ", "　"},
    };
    return style == Style::Windows ? windows : mac;
}

std::vector<std::string> alternatives(std::string_view symbol, Style style) {
    const auto& table = groups(style);
    // 先找以這個符號開頭的組（例如「、」也出現在「，」那一組，但它有自己的一組），沒有才找第一個含有它的組。
    auto group = std::find_if(table.begin(), table.end(), [&](const auto& g) { return g.front() == symbol; });
    if (group == table.end()) {
        group = std::find_if(table.begin(), table.end(), [&](const auto& g) { return std::find(g.begin(), g.end(), symbol) != g.end(); });
    }
    if (group == table.end()) return {};
    std::vector<std::string> result{std::string(symbol)};
    for (const auto& s : *group) {
        if (s != symbol) result.push_back(s);
    }
    return result;
}

}  // namespace Punctuation

}  // namespace bangbang
