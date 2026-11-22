#pragma once

// 測試共用的工具：迷你語言模型、依大千排列打字、暫存檔。對應 BangbangEngineTests.swift 開頭的 makeLM 與 type。

#include <windows.h>

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <map>
#include <memory>
#include <string>
#include <string_view>
#include <tuple>
#include <vector>

#include "Decoder.h"
#include "InputSession.h"
#include "LanguageModel.h"

namespace bangbang::test {

using Triple = std::tuple<std::string, std::string, float>;

/// 測試用的迷你語言模型（同 Mac 版的 makeLM）。
inline std::shared_ptr<const LanguageModel> makeLM(const std::vector<Triple>& bigrams = {}, const std::vector<Triple>& skips = {}) {
    const std::vector<std::string> words = {"<s>", "你", "妳", "好", "你好", "越", "月", "更新", "難", "南",
                                            "用", "越南", "更", "新", "中", "鐘", "文", "聞", "中文"};
    const std::map<std::string, float> unigram = {
        {"<s>", -30.0f}, {"你", -3.0f}, {"妳", -6.0f}, {"好", -3.0f}, {"你好", -4.0f}, {"越", -6.0f}, {"月", -4.0f},
        {"更新", -6.0f}, {"難", -5.0f}, {"南", -5.5f}, {"用", -4.0f}, {"越南", -6.0f}, {"更", -6.0f}, {"新", -6.0f},
        {"中", -5.0f}, {"鐘", -6.0f}, {"文", -5.0f}, {"聞", -7.0f}, {"中文", -4.0f},
    };
    std::map<std::string, WordId> id;
    for (std::size_t i = 0; i < words.size(); ++i) id[words[i]] = static_cast<WordId>(i);
    const std::vector<std::pair<std::string, std::vector<std::string>>> lexicon = {
        {"ㄋㄧˇ", {"你", "妳"}}, {"ㄏㄠˇ", {"好"}}, {"ㄋㄧˇ ㄏㄠˇ", {"你好"}},
        {"ㄩㄝˋ", {"越", "月"}}, {"ㄍㄥ ㄒㄧㄣ", {"更新"}}, {"ㄋㄢˊ", {"難", "南"}},
        {"ㄩㄥˋ", {"用"}}, {"ㄩㄝˋ ㄋㄢˊ", {"越南"}}, {"ㄍㄥ", {"更"}}, {"ㄒㄧㄣ", {"新"}},
        {"ㄓㄨㄥ", {"中", "鐘"}}, {"ㄨㄣˊ", {"文", "聞"}}, {"ㄓㄨㄥ ㄨㄣˊ", {"中文"}},
    };
    std::map<ReadingKey, std::vector<LexiconEntry>> lex;
    for (const auto& [reading, ws] : lexicon) {
        auto& entries = lex[ReadingKey::of(*Syllable::parseReading(reading))];
        for (const auto& w : ws) entries.push_back(LexiconEntry{id.at(w), 0});
    }
    std::vector<std::tuple<WordId, WordId, float>> sorted;
    for (const auto& [a, b, v] : bigrams) sorted.emplace_back(id.at(a), id.at(b), v);
    std::sort(sorted.begin(), sorted.end(), [](const auto& x, const auto& y) {
        return std::pair(std::get<0>(x), std::get<1>(x)) < std::pair(std::get<0>(y), std::get<1>(y));
    });
    std::vector<std::uint32_t> start(words.size() + 1, 0);
    for (const auto& t : sorted) start[std::get<0>(t) + 1] += 1;
    for (std::size_t i = 1; i <= words.size(); ++i) start[i] += start[i - 1];
    std::vector<std::uint32_t> next;
    std::vector<float> logProb;
    for (const auto& t : sorted) {
        next.push_back(std::get<1>(t));
        logProb.push_back(std::get<2>(t));
    }
    std::map<std::uint64_t, float> skipBoost;
    for (const auto& [a, b, v] : skips) skipBoost[static_cast<std::uint64_t>(id.at(a)) << 32 | id.at(b)] = v;
    std::vector<float> logUnigram;
    for (const auto& w : words) logUnigram.push_back(unigram.at(w));
    const std::vector<float> logBackoff(words.size(), std::log(0.1f));
    return LanguageModel::build(words, logUnigram, logBackoff, start, next, logProb, lex, skipBoost);
}

inline KeyEvent key(KeyEvent::Type type) {
    return KeyEvent::of(type);
}

inline KeyEvent ch(char c, bool shift = false) {
    return KeyEvent::character(c, shift);
}

/// 依大千排列打字；空白鍵為一聲。
inline std::vector<SessionOutput> type(InputSession& session, std::string_view keys) {
    std::vector<SessionOutput> outputs;
    for (const char c : keys) outputs.push_back(c == ' ' ? session.handle(key(KeyEvent::Type::Space)) : session.handle(ch(c)));
    return outputs;
}

/// 選第 index 個（從 0 起算）候選：按對應的數字鍵。
inline SessionOutput pickNumber(InputSession& session, std::size_t index) {
    return session.handle(ch(static_cast<char>('1' + index)));
}

inline std::vector<std::string> texts(const std::vector<Candidate>& items) {
    std::vector<std::string> result;
    for (const auto& c : items) result.push_back(c.text);
    return result;
}

inline std::vector<std::string> texts(const std::vector<WalkNode>& walk) {
    std::vector<std::string> result;
    for (const auto& n : walk) result.push_back(n.text);
    return result;
}

inline std::string joined(const std::vector<WalkNode>& walk) {
    std::string out;
    for (const auto& n : walk) out += n.text;
    return out;
}

inline std::size_t indexOf(const std::vector<Candidate>& items, const std::string& text) {
    const auto it = std::find_if(items.begin(), items.end(), [&](const Candidate& c) { return c.text == text; });
    return static_cast<std::size_t>(it - items.begin());
}

inline std::vector<Token> syllableTokens(const std::string& reading) {
    // 先存起來：在 range-for 裡直接走訪 *parseReading(...) 的話，暫時物件會在迴圈開始前就被釋放。
    const auto syllables = Syllable::parseReading(reading);
    std::vector<Token> tokens;
    for (const auto& s : *syllables) tokens.push_back(Token::syllableToken(s));
    return tokens;
}

/// 測試結束時自動刪除的暫存檔。
struct TempFile {
    std::filesystem::path path;
    explicit TempFile(const std::string& stem, const std::string& extension) {
        static int counter = 0;
        path = std::filesystem::temp_directory_path() /
               (stem + "-" + std::to_string(GetCurrentProcessId()) + "-" + std::to_string(++counter) + extension);
    }
    TempFile(const TempFile&) = delete;
    TempFile& operator=(const TempFile&) = delete;
    ~TempFile() {
        std::error_code ec;
        std::filesystem::remove(path, ec);
    }
};

/// 測試結束時連同裡面的檔案一起刪除的暫存資料夾（記憶檔旁邊還會有鎖檔）。
struct TempDir {
    std::filesystem::path path;
    explicit TempDir(const std::string& stem) {
        static int counter = 0;
        path = std::filesystem::temp_directory_path() /
               (stem + "-" + std::to_string(GetCurrentProcessId()) + "-" + std::to_string(++counter));
        std::filesystem::create_directories(path);
    }
    TempDir(const TempDir&) = delete;
    TempDir& operator=(const TempDir&) = delete;
    ~TempDir() {
        std::error_code ec;
        std::filesystem::remove_all(path, ec);
    }
};

}  // namespace bangbang::test
