#pragma once

// 對應 Sources/BangbangEngine/UserModel.swift：記住使用者手動選過的字詞。

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <map>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <unordered_map>
#include <vector>

#include "Decoder.h"
#include "LanguageModel.h"
#include "SharedFile.h"

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
///
/// 多個程式共用同一個記憶檔（Windows 上每個程式都各自載入輸入法）：
/// - 學習時鎖住記憶檔；檔案在上次讀取後被別的程式改過，就先重新載入，再把這次的選字套上去，然後整個取代原檔。
///   所以同時學習也不會互相覆蓋，次數照樣累加。
/// - 開始組字前呼叫 refresh()：檔案被改過就重新載入，A 程式學到的，B 程式下一次打字就用得到。
/// - 鎖檔等太久或資料夾不能用時，這次的變更先留在記憶體（下次學習時再寫），輸入不會卡住。
class UserModel final : public ScoreAdjuster {
public:
    static constexpr int kMaxCount = 3;
    /// 上下文勝出者比語言模型的首選再多出的分數，確保它一定被選中。
    static constexpr float kContextualMargin = 4;
    /// 單詞記憶的勝出者每選一次加的分數。
    static constexpr float kGlobalBonusPerPick = 0.8f;
    /// 等別的程式寫完記憶檔，最多等這麼久（學習是在程式的介面執行緒上做的，不能讓它卡住）。
    static constexpr std::chrono::milliseconds kLockTimeout{200};

    /// file 為 nullopt 時不讀寫檔案（測試用）。
    UserModel(std::shared_ptr<const LanguageModel> lm, std::optional<std::filesystem::path> file);

    float bonus(WordId prev, WordId word, const ReadingKey& key) override;
    /// 記住這次的選字，並寫進記憶檔。
    void learn(WordId prev, WordId word, const ReadingKey& key, double time = now());
    /// 忘掉全部的選字（記憶檔也清空，其他程式下次組字時跟著忘掉）。
    void reset();
    /// 記憶檔被別的程式改過（或被刪除、換成別的檔案）就重新載入；還沒寫進檔案的變更會保留。
    /// 開始組字前呼叫：檔案沒有變時，只查一次檔案資訊。回傳是否重新載入了。
    bool refresh();
    /// 把還沒寫進記憶檔的變更寫進去（例如輸入法停用時）。回傳是否全部寫進去了。
    bool flush();

    const std::optional<std::filesystem::path>& file() const { return file_; }
    /// 還沒寫進記憶檔的變更數（等不到鎖、資料夾不能用時會暫時留在記憶體）。
    std::size_t unsavedChanges() const { return pending_.size(); }
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
    /// 一格裡選過的詞。通常只有一兩個：用小陣列比雜湊表少很多次記憶體配置，載入大記憶檔快得多。
    struct Picks {
        std::vector<std::pair<WordId, Pick>> items;
        const Pick* find(WordId word) const {
            for (const auto& item : items) {
                if (item.first == word) return &item.second;
            }
            return nullptr;
        }
        Pick& operator[](WordId word) {
            for (auto& item : items) {
                if (item.first == word) return item.second;
            }
            return items.emplace_back(word, Pick{}).second;
        }
    };
    template <class V>
    using SlotMap = std::unordered_map<ContextSlot, V, SlotHash, SlotEqual>;
    using Records = std::map<std::string, Record>;

    /// 一筆還沒寫進記憶檔的變更：選了某個詞，或忘掉全部。
    struct Change {
        bool reset = false;
        WordId prev = 0;
        WordId word = 0;
        ReadingKey key;
        double time = 0;
    };
    enum class LoadResult {
        Loaded,  // 已換成檔案的內容（檔案不存在時是清空）
        Kept,    // 檔案壞了：記憶體內容不變，寫入時先把壞檔改名保留
        Failed,  // 讀不到檔案：什麼都沒變，下次再試
    };

    /// 一格裡選最多次的勝出；次數一樣時，最近選的勝出。
    static std::optional<WordId> winner(const Picks& picks);
    static Pick add(WordId word, Picks& picks, double time);
    static std::string readingText(const ReadingKey& key);
    /// 讓勝出者剛好贏過語言模型在這一格的首選，再多 kContextualMargin 分。
    float contextualBonus(const SlotView& slot, WordId winnerWord);

    /// 把一筆變更套到記憶體裡的內容（不會加進 pending_）。
    void apply(const Change& change);
    void clear();
    /// 解析記憶檔的內容並取代記憶體裡的內容；格式不對時回傳 false，什麼都不變。
    bool parse(const std::string& data);
    /// 從記憶檔載入，再把還沒寫進檔案的變更重做一次。
    LoadResult reload();
    /// 把還沒寫進檔案的變更寫進記憶檔（鎖檔、必要時先重新載入、取代原檔）。
    void save();
    std::string serialize() const;
    std::filesystem::path lockFile() const;
    void keepPendingBounded();

    std::shared_ptr<const LanguageModel> lm_;
    std::optional<std::filesystem::path> file_;
    /// 檔案內容：鍵是文字（「前一個詞\t詞\t讀音」、「詞\t讀音」），重建模型後仍可用。
    Records storedContextual_;
    Records storedUnigram_;
    SlotMap<Picks> contextual_;
    std::unordered_map<ReadingKey, Picks, ReadingKeyHash> global_;
    SlotMap<float> bonusCache_;

    /// 記憶體裡的內容是記憶檔的哪個版本（加上 pending_）。
    FileStamp loadedStamp_;
    /// loadedStamp_ 那個版本的檔案壞了。
    bool loadedCorrupt_ = false;
    /// 已經套用到記憶體、但還沒寫進記憶檔的變更。
    std::vector<Change> pending_;
    /// 記憶檔的資料夾確認過是所有程式共用的真正位置（沒有被 MSIX 轉存）。
    bool folderVerified_ = false;
    bool redirectReported_ = false;
};

}  // namespace bangbang
