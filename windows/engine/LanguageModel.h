#pragma once

// 對應 Sources/BangbangEngine/LanguageModel.swift：雙連詞語言模型、注音詞庫、模型檔格式（第 3 版）。

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <iterator>
#include <map>
#include <memory>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include "Bopomofo.h"

namespace bangbang {

using WordId = std::uint32_t;

/// 多個音節組成的讀音，作為詞庫查詢的鍵。
struct ReadingKey {
    std::vector<std::uint16_t> codes;

    ReadingKey() = default;
    explicit ReadingKey(std::vector<std::uint16_t> c) : codes(std::move(c)) {}
    static ReadingKey of(const std::vector<Syllable>& syllables);
    std::vector<Syllable> syllables() const;

    friend bool operator==(const ReadingKey&, const ReadingKey&) = default;
    /// 依音節序列的字典順序（短的是長的前綴時排在前面），同 Swift 的 lexicographicallyPrecedes。
    friend bool operator<(const ReadingKey& a, const ReadingKey& b) { return a.codes < b.codes; }
};

struct ReadingKeyHash {
    using is_transparent = void;
    std::size_t operator()(std::span<const std::uint16_t> codes) const noexcept;
    std::size_t operator()(const ReadingKey& key) const noexcept { return (*this)(std::span<const std::uint16_t>(key.codes)); }
};

/// 某個讀音下的一個候選詞。
struct LexiconEntry {
    WordId word = 0;
    /// log P(讀音 | 詞)：破音字的少用讀音會是負值，一般為 0。
    float readingWeight = 0;

    friend bool operator==(const LexiconEntry&, const LexiconEntry&) = default;
};

/// 某個讀音的候選詞清單，直接指向模型檔裡的資料（不複製）。已依「單詞機率 + 讀音權重」由高到低排序。
class LexiconEntries {
public:
    class Iterator {
    public:
        using iterator_category = std::input_iterator_tag;
        using value_type = LexiconEntry;
        using difference_type = std::ptrdiff_t;
        using pointer = void;
        using reference = LexiconEntry;
        Iterator() = default;
        Iterator(const LexiconEntries* owner, std::size_t index) : owner_(owner), index_(index) {}
        LexiconEntry operator*() const { return (*owner_)[index_]; }
        Iterator& operator++() {
            ++index_;
            return *this;
        }
        Iterator operator++(int) {
            Iterator copy = *this;
            ++index_;
            return copy;
        }
        friend bool operator==(const Iterator& a, const Iterator& b) { return a.index_ == b.index_; }

    private:
        const LexiconEntries* owner_ = nullptr;
        std::size_t index_ = 0;
    };

    LexiconEntries() = default;
    LexiconEntries(const std::uint32_t* words, const float* weights, std::size_t begin, std::size_t end)
        : words_(words), weights_(weights), begin_(begin), end_(end) {}

    std::size_t size() const { return end_ - begin_; }
    bool empty() const { return begin_ == end_; }
    /// 第 i 個候選（從 0 起算）。
    LexiconEntry operator[](std::size_t i) const { return {words_[begin_ + i], weights_[begin_ + i]}; }
    Iterator begin() const { return {this, 0}; }
    Iterator end() const { return {this, size()}; }

private:
    const std::uint32_t* words_ = nullptr;
    const float* weights_ = nullptr;
    std::size_t begin_ = 0;
    std::size_t end_ = 0;
};

/// 詞表：依編號取出詞的文字（直接指向模型檔裡的 UTF-8，不複製）。
class WordTable {
public:
    WordTable() = default;
    WordTable(const std::uint32_t* offsets, std::size_t count, const char* blob) : offsets_(offsets), count_(count), blob_(blob) {}

    std::size_t size() const { return count_; }
    std::string_view operator[](std::size_t i) const { return {blob_ + offsets_[i], offsets_[i + 1] - offsets_[i]}; }

private:
    const std::uint32_t* offsets_ = nullptr;
    std::size_t count_ = 0;
    const char* blob_ = nullptr;
};

class ModelError : public std::runtime_error {
public:
    enum class Kind { BadFormat, Truncated, CannotOpen };
    ModelError(Kind kind, const std::string& message) : std::runtime_error(message), kind_(kind) {}
    Kind kind() const { return kind_; }

private:
    Kind kind_;
};

/// 雙連詞（bigram）語言模型加上注音詞庫。
///
/// 分數都是自然對數機率。P(w | v) 有統計到就直接查表，否則退回 λ(v)·P(w)。
/// 模型檔用 file mapping 唯讀對應到記憶體、不複製，所有表格都以二分搜尋查詢；
/// 每個載入輸入法的程式都對應同一個檔案，共用同一份實體記憶體。
class LanguageModel {
public:
    /// 0 號詞是「句子邊界」，句首與標點之後都以它當前一個詞。
    static constexpr WordId kBoundary = 0;
    static constexpr std::string_view kBoundaryText = "<s>";

    struct Lookup {
        LexiconEntries entries;
        /// 有沒有以這串音節開頭的更長讀音。
        bool hasLonger = false;
    };

    /// 用 file mapping 唯讀對應模型檔。檔案格式不對時丟出 ModelError。
    static std::shared_ptr<const LanguageModel> load(const std::filesystem::path& path);

    /// 從記憶體中的資料建立（建模型、測試用）。skipBoost 的鍵是「前前一個詞 << 32 | 詞」。
    static std::shared_ptr<const LanguageModel> build(const std::vector<std::string>& words,
                                                      const std::vector<float>& logUnigram,
                                                      const std::vector<float>& logBackoff,
                                                      const std::vector<std::uint32_t>& bigramStart,
                                                      const std::vector<std::uint32_t>& bigramNext,
                                                      const std::vector<float>& bigramLogProb,
                                                      const std::map<ReadingKey, std::vector<LexiconEntry>>& lexicon,
                                                      const std::map<std::uint64_t, float>& skipBoost = {});

    ~LanguageModel();
    LanguageModel(const LanguageModel&) = delete;
    LanguageModel& operator=(const LanguageModel&) = delete;

    void write(const std::filesystem::path& path) const;

    const WordTable& words() const { return words_; }
    std::span<const float> logUnigram() const { return logUnigram_; }
    std::span<const float> logBackoff() const { return logBackoff_; }
    int maxPhraseLength() const { return maxPhraseLength_; }
    std::size_t bigramCount() const { return bigramNext_.size(); }

    /// 某個讀音的候選詞（直接讀模型檔裡的那一段，不複製）。
    LexiconEntries entries(const ReadingKey& key) const { return lookup(key.codes).entries; }
    /// 查讀音，同時回答「有沒有以它開頭的更長讀音」，讓整句選字可以提早停止往後延伸。
    Lookup lookup(std::span<const std::uint16_t> codes) const;
    bool hasReading(const Syllable& syllable) const;

    /// 所有讀音與候選詞（工具與測試用；會建立完整的表，輸入法本身不要呼叫）。
    std::map<ReadingKey, std::vector<LexiconEntry>> lexicon() const;

    /// 有統計到 (prev, word) 時回傳其對數機率。
    std::optional<float> storedBigram(WordId prev, WordId word) const {
        std::size_t lo = bigramStart_[prev];
        std::size_t hi = bigramStart_[prev + 1];
        while (lo < hi) {
            const std::size_t mid = (lo + hi) >> 1;
            const WordId w = bigramNext_[mid];
            if (w == word) return bigramLogProb_[mid];
            if (w < word) {
                lo = mid + 1;
            } else {
                hi = mid;
            }
        }
        return std::nullopt;
    }

    /// log P(word | prev)
    float logProb(WordId prev, WordId word) const {
        if (const auto stored = storedBigram(prev, word)) return *stored;
        return logBackoff_[prev] + logUnigram_[word];
    }

    /// 前前一個詞是 prev2 時，word 的額外加分（隔詞搭配，例如「越 ＿ 越」）。
    float skip(WordId prev2, WordId word) const {
        if (prev2 == kBoundary) return 0;
        std::size_t lo = skipStart_[prev2];
        std::size_t hi = skipStart_[prev2 + 1];
        while (lo < hi) {
            const std::size_t mid = (lo + hi) >> 1;
            const WordId w = skipNext_[mid];
            if (w == word) return skipValues_[mid];
            if (w < word) {
                lo = mid + 1;
            } else {
                hi = mid;
            }
        }
        return 0;
    }

    class Storage;

private:
    explicit LanguageModel(std::unique_ptr<Storage> storage);
    int compareKey(std::size_t k, std::span<const std::uint16_t> codes) const;
    bool keyHasPrefix(std::size_t k, std::span<const std::uint16_t> codes) const;

    std::unique_ptr<Storage> storage_;
    WordTable words_;
    std::span<const float> logUnigram_;
    std::span<const float> logBackoff_;
    std::span<const std::uint32_t> bigramStart_;
    std::span<const std::uint32_t> bigramNext_;
    std::span<const float> bigramLogProb_;
    /// 讀音表：依音節序列排序，keyCodeStart／entryStart 標出每個讀音的音節與候選詞範圍。
    std::span<const std::uint32_t> keyCodeStart_;
    std::span<const std::uint16_t> keyCodes_;
    std::span<const std::uint32_t> entryStart_;
    std::span<const std::uint32_t> entryWord_;
    std::span<const float> entryWeight_;
    /// 隔一個詞的搭配加分，依前前一個詞分段：skipStart[前前詞]..<skipStart[前前詞 + 1]，段內依詞排序。
    std::span<const std::uint32_t> skipStart_;
    std::span<const std::uint32_t> skipNext_;
    std::span<const float> skipValues_;
    int maxPhraseLength_ = 1;
};

}  // namespace bangbang
