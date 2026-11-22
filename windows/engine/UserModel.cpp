#include "UserModel.h"

#include <windows.h>

#include <array>
#include <charconv>
#include <chrono>
#include <cmath>
#include <limits>
#include <string_view>
#include <vector>

#include <nlohmann/json.hpp>

namespace bangbang {

namespace {

constexpr float kLowest = -std::numeric_limits<float>::max();
/// 一直寫不進記憶檔時，記憶體裡最多留這麼多筆待寫入的變更（記憶本身仍然有效）。
constexpr std::size_t kMaxPending = 1000;

/// 記憶檔裡的一筆記錄。
struct RawRecord {
    std::string key;
    int count = 0;
    double lastUsed = 0;
};

/// 用串流方式從記憶檔取出 {"contextual": {鍵: {"count": n, "lastUsed": t}}, "unigram": {…}}，
/// 不建整棵 JSON 樹（記憶檔可能有好幾 MB，每個程式都要載入）。格式不對的記錄略過，同 Mac 版。
class RecordReader {
public:
    using json = nlohmann::json;

    std::vector<RawRecord> contextual;
    std::vector<RawRecord> unigram;
    bool rootIsObject() const { return rootIsObject_; }

    bool null() { return scalar(); }
    bool boolean(bool) { return scalar(); }
    bool number_integer(json::number_integer_t v) { return number(static_cast<double>(v)); }
    bool number_unsigned(json::number_unsigned_t v) { return number(static_cast<double>(v)); }
    bool number_float(json::number_float_t v, const json::string_t&) { return number(v); }
    bool string(json::string_t&) { return scalar(); }
    bool binary(json::binary_t&) { return scalar(); }

    bool start_object(std::size_t) {
        if (stack_.empty()) rootIsObject_ = true;
        if (stack_.size() == 3 && inRecord_) field_ = Field::Other;  // 記錄裡的巢狀值：不是我們要的
        stack_.push_back(Container::Object);
        if (stack_.size() == 2 && rootIsObject_) {
            section_ = nextSection_;  // "contextual" 或 "unigram" 的內容
        } else if (stack_.size() == 3 && section_ && keyReady_) {
            inRecord_ = true;  // 一筆記錄 {"count": …, "lastUsed": …}
            current_ = RawRecord{std::move(key_), 0, 0};
            hasCount_ = hasLastUsed_ = false;
            countValue_ = 0;
        }
        nextSection_ = nullptr;
        keyReady_ = false;
        return true;
    }
    bool end_object() {
        if (stack_.size() == 3 && inRecord_) {
            inRecord_ = false;
            if (hasCount_ && hasLastUsed_ && countValue_ >= 0 && countValue_ <= 1e9) {
                current_.count = static_cast<int>(countValue_);
                section_->push_back(std::move(current_));
            }
        } else if (stack_.size() == 2) {
            section_ = nullptr;
        }
        stack_.pop_back();
        return true;
    }
    bool start_array(std::size_t) {
        if (stack_.size() == 3 && inRecord_) field_ = Field::Other;
        stack_.push_back(Container::Array);
        nextSection_ = nullptr;
        keyReady_ = false;
        return true;
    }
    bool end_array() {
        stack_.pop_back();
        return true;
    }
    bool key(json::string_t& k) {
        if (stack_.size() == 1) {
            nextSection_ = k == "contextual" ? &contextual : k == "unigram" ? &unigram : nullptr;
        } else if (stack_.size() == 2 && section_) {
            key_ = std::move(k);
            keyReady_ = true;
        } else if (stack_.size() == 3 && inRecord_) {
            field_ = k == "count" ? Field::Count : k == "lastUsed" ? Field::LastUsed : Field::Other;
        }
        return true;
    }
    bool parse_error(std::size_t, const std::string&, const nlohmann::detail::exception&) { return false; }

private:
    enum class Container { Object, Array };
    enum class Field { Other, Count, LastUsed };

    bool scalar() {
        nextSection_ = nullptr;
        keyReady_ = false;
        if (stack_.size() == 3 && inRecord_) field_ = Field::Other;
        return true;
    }
    bool number(double v) {
        if (stack_.size() == 3 && inRecord_) {
            if (field_ == Field::Count) {
                countValue_ = v;
                hasCount_ = true;
            } else if (field_ == Field::LastUsed) {
                current_.lastUsed = v;
                hasLastUsed_ = true;
            }
            field_ = Field::Other;
            return true;
        }
        return scalar();
    }

    std::vector<Container> stack_;
    bool rootIsObject_ = false;
    std::vector<RawRecord>* nextSection_ = nullptr;
    std::vector<RawRecord>* section_ = nullptr;
    std::string key_;
    bool keyReady_ = false;
    bool inRecord_ = false;
    RawRecord current_;
    Field field_ = Field::Other;
    bool hasCount_ = false;
    bool hasLastUsed_ = false;
    double countValue_ = 0;
};

/// 以 Tab 分開的鍵（保留空白片段，同 Swift 的 split(separator:omittingEmptySubsequences: false)）。最多記 3 段。
struct KeyParts {
    std::array<std::string_view, 3> part;
    std::size_t count = 0;
};

KeyParts splitKey(std::string_view s) {
    KeyParts parts;
    std::size_t start = 0;
    for (std::size_t i = 0; i <= s.size(); ++i) {
        if (i == s.size() || s[i] == '\t') {
            if (parts.count < parts.part.size()) parts.part[parts.count] = s.substr(start, i - start);
            ++parts.count;
            start = i + 1;
        }
    }
    return parts;
}

void appendJsonString(std::string& out, std::string_view s) {
    out += '"';
    for (const char ch : s) {
        const auto c = static_cast<unsigned char>(ch);
        switch (c) {
        case '"': out += "\\\""; break;
        case '\\': out += "\\\\"; break;
        case '\t': out += "\\t"; break;
        case '\n': out += "\\n"; break;
        case '\r': out += "\\r"; break;
        case '\b': out += "\\b"; break;
        case '\f': out += "\\f"; break;
        default:
            if (c < 0x20) {
                static constexpr char hex[] = "0123456789abcdef";
                out += "\\u00";
                out += hex[c >> 4];
                out += hex[c & 0xF];
            } else {
                out += ch;
            }
        }
    }
    out += '"';
}

template <class T>
void appendNumber(std::string& out, T value) {
    char buffer[32];
    const auto result = std::to_chars(buffer, buffer + sizeof(buffer), value);  // 浮點數用最短、可以原樣讀回的寫法
    out.append(buffer, result.ptr);
}

}  // namespace

std::size_t UserModel::SlotHash::operator()(const SlotView& s) const noexcept {
    return ReadingKeyHash{}(s.codes) ^ (static_cast<std::size_t>(s.prev) * static_cast<std::size_t>(0x9E3779B97F4A7C15ULL));
}

UserModel::UserModel(std::shared_ptr<const LanguageModel> lm, std::optional<std::filesystem::path> file)
    : lm_(std::move(lm)), file_(std::move(file)) {
    if (!file_) return;
    // 資料夾被轉存到這個程式的私有位置時，裡面的檔案不是大家共用的那一份：不讀。
    if (checkSharedFolder(file_->parent_path(), false) != FolderState::Ready) return;
    folderVerified_ = true;
    reload();
}

double UserModel::now() {
    using namespace std::chrono;
    return duration<double>(system_clock::now().time_since_epoch()).count();
}

float UserModel::bonus(WordId prev, WordId word, const ReadingKey& key) {
    float b = 0;
    if (!contextual_.empty()) {
        const SlotView slot{prev, key.codes};
        if (const auto it = contextual_.find(slot); it != contextual_.end() && winner(it->second) == word) {
            b += contextualBonus(slot, word);
        }
    }
    if (!global_.empty()) {
        if (const auto it = global_.find(key); it != global_.end() && winner(it->second) == word) {
            if (const Pick* pick = it->second.find(word)) b += kGlobalBonusPerPick * static_cast<float>(pick->count);
        }
    }
    return b;
}

void UserModel::learn(WordId prev, WordId word, const ReadingKey& key, double time) {
    Change change{false, prev, word, key, time};
    apply(change);
    if (!file_) return;
    pending_.push_back(std::move(change));
    save();
}

void UserModel::reset() {
    clear();
    if (!file_) return;
    pending_.clear();  // 之前的變更都不用寫了
    pending_.push_back(Change{.reset = true});
    save();
}

bool UserModel::refresh() {
    if (!file_) return false;
    if (!folderVerified_) {
        const FolderState state = checkSharedFolder(file_->parent_path(), false);
        if (state == FolderState::Ready) {
            folderVerified_ = true;
        } else if (state != FolderState::Missing) {
            return false;
        }
    }
    const auto stamp = fileStamp(*file_);
    if (!stamp || *stamp == loadedStamp_) return false;
    // 不用鎖檔：寫的人是整個取代檔案，讀到的一定是完整的某一版。
    return reload() == LoadResult::Loaded;
}

bool UserModel::flush() {
    save();
    return pending_.empty();
}

void UserModel::apply(const Change& change) {
    if (change.reset) {
        clear();
        return;
    }
    const Pick contextPick = add(change.word, contextual_[ContextSlot{change.prev, change.key}], change.time);
    const Pick globalPick = add(change.word, global_[change.key], change.time);
    bonusCache_.clear();

    const std::string reading = readingText(change.key);
    const std::string text(lm_->words()[change.word]);
    storedContextual_[std::string(lm_->words()[change.prev]) + "\t" + text + "\t" + reading] = Record{contextPick.count, contextPick.lastUsed};
    storedUnigram_[text + "\t" + reading] = Record{globalPick.count, globalPick.lastUsed};
}

void UserModel::clear() {
    storedContextual_.clear();
    storedUnigram_.clear();
    contextual_.clear();
    global_.clear();
    bonusCache_.clear();
}

std::optional<WordId> UserModel::winner(const Picks& picks) {
    std::optional<WordId> best;
    Pick bestPick;
    for (const auto& [word, pick] : picks.items) {
        if (!best || std::pair(bestPick.count, bestPick.lastUsed) < std::pair(pick.count, pick.lastUsed)) {
            best = word;
            bestPick = pick;
        }
    }
    return best;
}

UserModel::Pick UserModel::add(WordId word, Picks& picks, double time) {
    const Pick* found = picks.find(word);
    Pick pick = found ? *found : Pick{0, time};
    pick.count = std::min(kMaxCount, pick.count + 1);
    // 重做別的程式還沒看到的變更時，檔案裡可能已經有更晚的時間：時間不倒退。
    pick.lastUsed = std::max(pick.lastUsed, time);
    picks[word] = pick;
    return pick;
}

std::string UserModel::readingText(const ReadingKey& key) {
    std::string out;
    for (std::size_t i = 0; i < key.codes.size(); ++i) {
        if (i > 0) out += ' ';
        out += Syllable::fromCode(key.codes[i]).description();
    }
    return out;
}

float UserModel::contextualBonus(const SlotView& slot, WordId winnerWord) {
    if (const auto it = bonusCache_.find(slot); it != bonusCache_.end()) return it->second;
    float top = kLowest;
    float mine = kLowest;
    const LexiconEntries entries = lm_->lookup(slot.codes).entries;
    for (const auto e : entries) {
        const float s = lm_->logProb(slot.prev, e.word) + e.readingWeight;
        top = std::max(top, s);
        if (e.word == winnerWord) mine = s;
    }
    const float value = (mine > kLowest ? std::max(0.0f, top - mine) : 0.0f) + kContextualMargin;
    bonusCache_.emplace(ContextSlot{slot.prev, ReadingKey(std::vector<std::uint16_t>(slot.codes.begin(), slot.codes.end()))}, value);
    return value;
}

bool UserModel::parse(const std::string& data) {
    RecordReader reader;
    if (!nlohmann::json::sax_parse(data, &reader) || !reader.rootIsObject()) return false;
    const auto& unigramRecords = reader.unigram;
    const auto& contextualRecords = reader.contextual;

    // 每個鍵只切一次；切出來的片段指向 reader 裡的字串，用到最後都不會移動。
    std::vector<KeyParts> unigramParts;
    std::vector<KeyParts> contextualParts;
    unigramParts.reserve(unigramRecords.size());
    contextualParts.reserve(contextualRecords.size());
    for (const auto& r : unigramRecords) unigramParts.push_back(splitKey(r.key));
    for (const auto& r : contextualRecords) contextualParts.push_back(splitKey(r.key));

    // 文字 → 編號：只找記憶裡用到的詞，掃一次詞表就好，不建整份索引（每個程式都會載入記憶，要輕）。
    constexpr WordId kMissing = std::numeric_limits<WordId>::max();
    std::unordered_map<std::string_view, WordId> wordIndex;
    wordIndex.reserve(unigramRecords.size() + contextualRecords.size());
    for (const auto& parts : unigramParts) wordIndex.emplace(parts.part[0], kMissing);
    for (const auto& parts : contextualParts) {
        for (std::size_t i = 0; i < std::min<std::size_t>(parts.count, 2); ++i) wordIndex.emplace(parts.part[i], kMissing);
    }
    if (!wordIndex.empty()) {
        const auto& words = lm_->words();
        for (std::size_t i = 0; i < words.size(); ++i) {
            if (const auto it = wordIndex.find(words[i]); it != wordIndex.end()) it->second = static_cast<WordId>(i);
        }
    }
    const auto idOf = [&](std::string_view text) -> std::optional<WordId> {
        const auto it = wordIndex.find(text);
        if (it == wordIndex.end() || it->second == kMissing) return std::nullopt;
        return it->second;
    };
    // 讀音文字 → 讀音：同一個讀音常常出現在好幾筆記錄裡，只解析一次。
    std::unordered_map<std::string_view, std::optional<ReadingKey>> readingCache;
    const auto readingOf = [&](std::string_view text) -> const std::optional<ReadingKey>& {
        auto [it, inserted] = readingCache.try_emplace(text);
        if (inserted) {
            if (const auto syllables = Syllable::parseReading(text)) it->second = ReadingKey::of(*syllables);
        }
        return it->second;
    };

    // 先建在區域變數裡，全部成功才換掉記憶體裡的內容。檔案裡的鍵已經排好序，依序放進 map 最快。
    SlotMap<Picks> contextual;
    std::unordered_map<ReadingKey, Picks, ReadingKeyHash> global;
    contextual.reserve(contextualRecords.size());
    global.reserve(unigramRecords.size());
    Records convertedContextual;
    Records convertedUnigram;
    // 只有舊格式（上下文鍵沒有讀音）才需要「詞 → 讀音」。
    const bool oldFormat = std::any_of(contextualParts.begin(), contextualParts.end(), [](const KeyParts& p) { return p.count == 2 || p.count > 3; });
    std::unordered_map<std::string_view, std::vector<std::string_view>> readingsOfWord;
    for (std::size_t i = 0; i < unigramRecords.size(); ++i) {
        const auto& r = unigramRecords[i];
        const auto& parts = unigramParts[i];
        if (parts.count != 2) continue;
        const auto w = idOf(parts.part[0]);
        const auto& key = readingOf(parts.part[1]);
        if (!w || !key) continue;
        const Pick pick{std::min(kMaxCount, r.count), r.lastUsed};
        global[*key][*w] = pick;
        convertedUnigram.insert_or_assign(convertedUnigram.end(), r.key, Record{pick.count, pick.lastUsed});
        if (oldFormat) readingsOfWord[parts.part[0]].push_back(parts.part[1]);
    }
    for (std::size_t i = 0; i < contextualRecords.size(); ++i) {
        const auto& r = contextualRecords[i];
        const auto& parts = contextualParts[i];
        if (parts.count < 2) continue;
        const auto p = idOf(parts.part[0]);
        const auto w = idOf(parts.part[1]);
        if (!p || !w) continue;
        const Pick pick{std::min(kMaxCount, r.count), r.lastUsed};
        if (parts.count == 3) {
            const auto& key = readingOf(parts.part[2]);
            if (!key) continue;
            contextual[ContextSlot{*p, *key}][*w] = pick;
            convertedContextual.insert_or_assign(convertedContextual.end(), r.key, Record{pick.count, pick.lastUsed});
            continue;
        }
        // 舊格式（沒有 version、上下文鍵只有兩段）沒有讀音：用同一個詞的單詞記錄補上。
        const auto readings = readingsOfWord.find(parts.part[1]);
        if (readings == readingsOfWord.end()) continue;
        for (const auto reading : readings->second) {
            const auto& key = readingOf(reading);
            if (!key) continue;
            contextual[ContextSlot{*p, *key}][*w] = pick;
            std::string text;
            text.append(parts.part[0]).append("\t").append(parts.part[1]).append("\t").append(reading);
            convertedContextual.insert_or_assign(std::move(text), Record{pick.count, pick.lastUsed});
        }
    }
    storedContextual_ = std::move(convertedContextual);
    storedUnigram_ = std::move(convertedUnigram);
    contextual_ = std::move(contextual);
    global_ = std::move(global);
    bonusCache_.clear();
    return true;
}

UserModel::LoadResult UserModel::reload() {
    std::string data;
    FileStamp stamp;
    switch (readSharedFile(*file_, data, stamp)) {
    case ReadResult::Failed:
        return LoadResult::Failed;
    case ReadResult::Missing:  // 被刪掉了（例如使用者想清除記憶）：跟著忘掉
        clear();
        loadedStamp_ = {};
        loadedCorrupt_ = false;
        break;
    case ReadResult::Ok: {
        bool parsed = false;
        try {
            parsed = parse(data);
        } catch (...) {
        }
        // 壞檔也記下版本：檔案沒再變之前不用每次重讀。
        loadedStamp_ = stamp;
        loadedCorrupt_ = !parsed;
        if (!parsed) return LoadResult::Kept;
        break;
    }
    }
    for (const auto& change : pending_) apply(change);
    return LoadResult::Loaded;
}

void UserModel::save() {
    if (!file_ || pending_.empty()) return;
    try {
        switch (checkSharedFolder(file_->parent_path(), true)) {
        case FolderState::Ready:
            folderVerified_ = true;
            break;
        case FolderState::Redirected:
            // 在 MSIX 封裝程式裡、真正的資料夾又還不存在：建立的資料夾會變成這個程式私有的，其他程式看不到。
            // 等不在封裝環境裡的程式建立資料夾之後，下次學習就寫得進去。
            if (!redirectReported_) {
                redirectReported_ = true;
                OutputDebugStringW(L"棒棒注音：這個程式建立的記憶檔資料夾會被轉存到私有位置，選字記憶先放在記憶體\n");
            }
            keepPendingBounded();
            return;
        default:
            keepPendingBounded();
            return;
        }
        const auto lock = FileLock::acquire(lockFile(), kLockTimeout);
        if (!lock) {
            keepPendingBounded();
            return;
        }
        removeStaleTempFiles(*file_);
        // 別的程式在我們上次讀檔之後寫過：以檔案的內容為準，再把我們的變更重做一次。
        const auto stamp = fileStamp(*file_);
        if (!stamp || (*stamp != loadedStamp_ && reload() == LoadResult::Failed)) {
            keepPendingBounded();
            return;
        }
        if (loadedCorrupt_) {
            // 壞掉的記憶檔（例如手動改到一半）改名保留，不直接蓋掉。
            std::filesystem::path aside = *file_;
            aside += L".corrupt";
            MoveFileExW(file_->c_str(), aside.c_str(), MOVEFILE_REPLACE_EXISTING);
        }
        if (const auto written = replaceSharedFile(*file_, serialize())) {
            loadedStamp_ = *written;
            loadedCorrupt_ = false;
            pending_.clear();
        } else {
            keepPendingBounded();
        }
    } catch (...) {
        // 寫不進去時安靜略過：記憶仍在記憶體裡有效，下次學習時再寫。
        keepPendingBounded();
    }
}

std::string UserModel::serialize() const {
    // 和 Mac 版一樣的 JSON（鍵排好序、" : " 的間隔），但一筆記錄寫在一行：檔案小一半，寫起來也快。
    std::string out;
    out.reserve((storedContextual_.size() + storedUnigram_.size()) * 72 + 64);
    const auto section = [&](const char* name, const Records& records) {
        out += "  \"";
        out += name;
        out += "\" : {";
        bool first = true;
        for (const auto& [key, r] : records) {
            out += first ? "\n    " : ",\n    ";
            first = false;
            appendJsonString(out, key);
            out += " : {\"count\" : ";
            appendNumber(out, r.count);
            out += ", \"lastUsed\" : ";
            appendNumber(out, std::isfinite(r.lastUsed) ? r.lastUsed : 0.0);
            out += '}';
        }
        out += records.empty() ? "},\n" : "\n  },\n";
    };
    out += "{\n";
    section("contextual", storedContextual_);
    section("unigram", storedUnigram_);
    out += "  \"version\" : 2\n}\n";
    return out;
}

std::filesystem::path UserModel::lockFile() const {
    std::filesystem::path lock = *file_;
    lock += L".lock";
    return lock;
}

void UserModel::keepPendingBounded() {
    if (pending_.size() > kMaxPending) pending_.erase(pending_.begin(), pending_.end() - static_cast<std::ptrdiff_t>(kMaxPending));
}

}  // namespace bangbang
