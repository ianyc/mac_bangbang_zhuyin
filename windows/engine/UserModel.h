#pragma once

// 對應 Sources/BangbangEngine/UserModel.swift：記住使用者手動選過的字詞。

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <map>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <unordered_map>

#include "Decoder.h"
#include "LanguageModel.h"

namespace bangbang {

/// 記住使用者手動選過的字詞，下次在相同情況預設選它。
///
/// 選過的字詞依「格」互相競爭：
/// - 上下文記憶：前一個詞相同、讀音相同的算同一格。格內選最多次的一定勝出（不管它是不是常用字），
///   次數一樣時以最近一次選的為準。例如在「愛」後面選過「妳」，下次「愛」後面打 ㄋㄧˇ 就是「妳」；
///   之後改選「你」一次就平手，最近選的是「你」，所以變回「你」。
/// - 單詞記憶：只看讀音的一格，勝出者在任何位置都小幅加分。
/// 每個字詞最多累計 kMaxCount 次，舊習慣不會累積到改不掉。
///
/// 記憶檔格式與 Mac 版相同（JSON 第 2 版），可以直接互相複製使用。
class UserModel final : public ScoreAdjuster {
public:
    static constexpr int kMaxCount = 3;
    /// 上下文勝出者比語言模型的首選再多出的分數，確保它一定被選中。
    static constexpr float kContextualMargin = 4;
    /// 單詞記憶的勝出者每選一次加的分數。
    static constexpr float kGlobalBonusPerPick = 0.8f;

    /// file 為 nullopt 時不讀寫檔案（測試用）。
    UserModel(std::shared_ptr<const LanguageModel> lm, std::optional<std::filesystem::path> file);

    float bonus(WordId prev, WordId word, const ReadingKey& key) override;
    void learn(WordId prev, WordId word, const ReadingKey& key, double time = now());
    void reset();

    const std::optional<std::filesystem::path>& file() const { return file_; }
    /// 現在時間（1970 年起算的秒數），同 Swift 的 Date().timeIntervalSince1970。
    static double now();

private:
    struct Pick {
        int count = 0;
        double lastUsed = 0;
    };
    struct Record {
        int count = 0;
        double lastUsed = 0;
    };
    struct ContextSlot {
        WordId prev = 0;
        ReadingKey key;
    };
    /// 查表用、不複製讀音的格子。
    struct SlotView {
        WordId prev = 0;
        std::span<const std::uint16_t> codes;
    };
    struct SlotHash {
        using is_transparent = void;
        std::size_t operator()(const ContextSlot& s) const noexcept { return (*this)(SlotView{s.prev, s.key.codes}); }
        std::size_t operator()(const SlotView& s) const noexcept;
    };
    struct SlotEqual {
        using is_transparent = void;
        static SlotView view(const ContextSlot& s) { return {s.prev, s.key.codes}; }
        static SlotView view(const SlotView& s) { return s; }
        template <class A, class B>
        bool operator()(const A& a, const B& b) const {
            const SlotView x = view(a), y = view(b);
            return x.prev == y.prev && std::ranges::equal(x.codes, y.codes);
        }
    };
    using Picks = std::unordered_map<WordId, Pick>;
    template <class V>
    using SlotMap = std::unordered_map<ContextSlot, V, SlotHash, SlotEqual>;

    /// 一格裡選最多次的勝出；次數一樣時，最近選的勝出。
    static std::optional<WordId> winner(const Picks& picks);
    static Pick add(WordId word, Picks& picks, double time);
    static std::string readingText(const ReadingKey& key);
    /// 讓勝出者剛好贏過語言模型在這一格的首選，再多 kContextualMargin 分。
    float contextualBonus(const SlotView& slot, WordId winnerWord);
    void load();
    void save() const;

    std::shared_ptr<const LanguageModel> lm_;
    std::optional<std::filesystem::path> file_;
    /// 檔案內容：鍵是文字（「前一個詞\t詞\t讀音」、「詞\t讀音」），重建模型後仍可用。
    std::map<std::string, Record> storedContextual_;
    std::map<std::string, Record> storedUnigram_;
    SlotMap<Picks> contextual_;
    std::unordered_map<ReadingKey, Picks, ReadingKeyHash> global_;
    SlotMap<float> bonusCache_;
};

}  // namespace bangbang
