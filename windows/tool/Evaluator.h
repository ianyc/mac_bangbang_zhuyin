#pragma once

// 對應 Sources/bangbang-tool/Evaluator.swift（中文轉注音、算正確率）與 SourceDictionary.swift 的 ScalarTrie。

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "Decoder.h"
#include "LanguageModel.h"

namespace bangbang {

/// 以 Unicode 字元為邊的字典樹，用來斷詞。
class ScalarTrie {
public:
    explicit ScalarTrie(const WordTable& words);

    std::optional<std::int32_t> child(std::int32_t node, char32_t scalar) const;
    std::int32_t terminal(std::int32_t node) const { return terminal_[static_cast<std::size_t>(node)]; }

private:
    std::unordered_map<std::uint64_t, std::int32_t> edges_;
    std::vector<std::int32_t> terminal_{-1};
};

/// 把中文句子轉成注音（評測用），以及計算選字正確率。
class Evaluator {
public:
    explicit Evaluator(std::shared_ptr<const LanguageModel> lm);

    /// 以詞庫斷詞後，每個詞取最常見的讀音。
    std::optional<std::vector<Syllable>> reading(std::string_view text) const;
    std::string convert(const std::vector<Syllable>& syllables) const;

    struct Failure {
        std::string expected;
        std::string got;
        std::string reading;
    };
    struct Result {
        int sentences = 0;
        int exact = 0;
        long long chars = 0;
        long long correctChars = 0;
        std::vector<Failure> failures;
        /// 其中花在整句選字（convert）的時間。
        double decodeSeconds = 0;
        double charAccuracy() const { return chars == 0 ? 0 : static_cast<double>(correctChars) / static_cast<double>(chars); }
        double sentenceAccuracy() const { return sentences == 0 ? 0 : static_cast<double>(exact) / static_cast<double>(sentences); }
    };

    /// 每行一句；可用「句子 | 注音」指定讀音（音節以空白分隔）。# 開頭為註解。
    Result evaluate(const std::vector<std::string>& lines, std::size_t limit) const;

    /// 他／她／它要看使用者想指誰；台／臺兩種寫法都正確。選哪個都不算錯。
    static bool equivalent(std::string_view a, std::string_view b);

private:
    float score(const ReadingKey& key, WordId word) const;

    std::shared_ptr<const LanguageModel> lm_;
    Decoder decoder_;
    std::unordered_map<WordId, std::vector<ReadingKey>> readingsOfWord_;
    std::unordered_map<WordId, std::unordered_map<std::uint16_t, float>> singleWeight_;
    std::unordered_map<std::string, WordId> wordIndex_;
    ScalarTrie trie_;
};

}  // namespace bangbang
