#pragma once

// 對應 Sources/BangbangEngine/Decoder.swift：整句選字（二階 Viterbi 搜尋）、候選字清單。

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "Bopomofo.h"
#include "LanguageModel.h"

namespace bangbang {

/// 組字區裡的一格：一個注音音節，或是一個直接輸入的字元（英文字母、標點、空白）。
struct Token {
    enum class Kind : std::uint8_t { Syllable, Literal };
    Kind kind = Kind::Literal;
    Syllable sound;    // kind == Syllable 時有效
    std::string text;  // kind == Literal 時有效（一個碼位）

    static Token syllableToken(Syllable s) { return {Kind::Syllable, s, {}}; }
    static Token literalToken(std::string t) { return {Kind::Literal, {}, std::move(t)}; }

    bool isLiteral() const { return kind == Kind::Literal; }
    std::optional<Syllable> syllable() const {
        if (kind == Kind::Syllable) return sound;
        return std::nullopt;
    }

    friend bool operator==(const Token& a, const Token& b) {
        if (a.kind != b.kind) return false;
        return a.kind == Kind::Syllable ? a.sound == b.sound : a.text == b.text;
    }
};

/// 選字結果中的一段：從第 start 格開始、佔 length 格的一個詞（或字元）。
struct WalkNode {
    int start = 0;
    int length = 0;
    /// 詞庫裡的詞；直接輸入的字元為 nullopt。
    std::optional<WordId> word;
    std::string text;
    int end() const { return start + length; }

    friend bool operator==(const WalkNode&, const WalkNode&) = default;
};

/// 使用者手動選定的詞，重新選字時必須保留。
struct Pin {
    int start = 0;
    int length = 0;
    WordId word = 0;
    int end() const { return start + length; }

    friend bool operator==(const Pin&, const Pin&) = default;
};

/// 候選字詞。
struct Candidate {
    int start = 0;
    int length = 0;
    /// 詞庫裡的詞；nullopt 表示符號候選（選了就直接換掉該格的符號）。
    std::optional<WordId> word;
    std::string text;

    /// 候選字視窗上顯示的文字（空白看不見，改用文字說明）。
    std::string displayText() const;

    friend bool operator==(const Candidate&, const Candidate&) = default;
};

/// 讓使用者習慣影響選字的介面（見 UserModel）。
class ScoreAdjuster {
public:
    virtual ~ScoreAdjuster() = default;
    virtual float bonus(WordId prev, WordId word, const ReadingKey& key) = 0;
};

/// 整句選字：在所有可能的斷詞組合中，找出語言模型分數最高的一條路徑。
class Decoder {
public:
    explicit Decoder(std::shared_ptr<const LanguageModel> lm, ScoreAdjuster* adjuster = nullptr);

    const LanguageModel& lm() const { return *lm_; }
    const std::shared_ptr<const LanguageModel>& lmPointer() const { return lm_; }

    /// 不擁有：由 InputSession 的擁有者管理壽命。
    ScoreAdjuster* adjuster = nullptr;
    /// 每個讀音只拿前幾名候選詞參與整句計算，避免太慢；候選清單仍會列出全部。
    int singleCharBeam = 40;
    int phraseBeam = 12;
    /// 每個節點保留幾種「前一個詞」的狀態（二階搜尋的寬度）。
    int statesPerNode = 6;

    std::vector<WalkNode> decode(const std::vector<Token>& tokens, const std::vector<Pin>& pins = {},
                                 WordId context = LanguageModel::kBoundary) const;

    /// 候選清單。游標在字前面時列出「從游標開始」的詞（長詞在前）；游標在最後時只列出最後一個字。
    /// 同長度依上下文分數排序。該位置是符號時，列出它的全形／半形與同一個鍵上的其他符號。
    std::vector<Candidate> candidates(const std::vector<Token>& tokens, int cursor, const std::vector<WalkNode>& walk) const;

    struct LatticeNode {
        int start = 0;
        int length = 0;
        std::optional<WordId> word;
        const std::string* literal = nullptr;  // 指向 tokens 裡的文字
        int keyIndex = -1;                     // 指向讀音表；直接輸入的字元、或沒有選字記憶時為 -1
        float weight = 0;
        int end() const { return start + length; }
    };

    std::vector<LatticeNode> buildLattice(const std::vector<Token>& tokens, const std::vector<Pin>& pins,
                                          std::vector<ReadingKey>& keys) const;

private:
    float score(const LatticeNode& node, const std::vector<ReadingKey>& keys, WordId prev, WordId prev2) const;
    std::vector<WalkNode> fallbackWalk(const std::vector<Token>& tokens) const;

    std::shared_ptr<const LanguageModel> lm_;
};

}  // namespace bangbang
