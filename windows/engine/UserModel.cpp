#include "UserModel.h"

#include <windows.h>

#include <chrono>
#include <fstream>
#include <limits>
#include <string_view>
#include <unordered_set>
#include <vector>

#include <nlohmann/json.hpp>

namespace bangbang {

namespace {

constexpr float kLowest = -std::numeric_limits<float>::max();

std::vector<std::string> splitTabs(const std::string& s) {
    // 保留空白片段，同 Swift 的 split(separator:omittingEmptySubsequences: false)。
    std::vector<std::string> parts;
    std::size_t start = 0;
    for (std::size_t i = 0; i <= s.size(); ++i) {
        if (i == s.size() || s[i] == '\t') {
            parts.push_back(s.substr(start, i - start));
            start = i + 1;
        }
    }
    return parts;
}

struct StringHash {
    using is_transparent = void;
    std::size_t operator()(std::string_view s) const noexcept { return std::hash<std::string_view>{}(s); }
};

/// 讀出 {"鍵": {"count": n, "lastUsed": t}} 形式的記錄；格式不對的記錄略過。
std::vector<std::pair<std::string, std::pair<int, double>>> readRecords(const nlohmann::json& root, const char* name) {
    std::vector<std::pair<std::string, std::pair<int, double>>> result;
    const auto it = root.find(name);
    if (it == root.end() || !it->is_object()) return result;
    for (const auto& [key, value] : it->items()) {
        if (!value.is_object()) continue;
        const auto count = value.find("count");
        const auto lastUsed = value.find("lastUsed");
        if (count == value.end() || lastUsed == value.end() || !count->is_number() || !lastUsed->is_number()) continue;
        const double c = count->get<double>();
        if (c < 0 || c > 1e9) continue;
        result.push_back({key, {static_cast<int>(c), lastUsed->get<double>()}});
    }
    return result;
}

}  // namespace

std::size_t UserModel::SlotHash::operator()(const SlotView& s) const noexcept {
    return ReadingKeyHash{}(s.codes) ^ (static_cast<std::size_t>(s.prev) * static_cast<std::size_t>(0x9E3779B97F4A7C15ULL));
}

UserModel::UserModel(std::shared_ptr<const LanguageModel> lm, std::optional<std::filesystem::path> file)
    : lm_(std::move(lm)), file_(std::move(file)) {
    load();
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
            if (const auto pick = it->second.find(word); pick != it->second.end()) {
                b += kGlobalBonusPerPick * static_cast<float>(pick->second.count);
            }
        }
    }
    return b;
}

void UserModel::learn(WordId prev, WordId word, const ReadingKey& key, double time) {
    const Pick contextPick = add(word, contextual_[ContextSlot{prev, key}], time);
    const Pick globalPick = add(word, global_[key], time);
    bonusCache_.clear();

    const std::string reading = readingText(key);
    const std::string text(lm_->words()[word]);
    storedContextual_[std::string(lm_->words()[prev]) + "\t" + text + "\t" + reading] = Record{contextPick.count, time};
    storedUnigram_[text + "\t" + reading] = Record{globalPick.count, time};
    save();
}

void UserModel::reset() {
    storedContextual_.clear();
    storedUnigram_.clear();
    contextual_.clear();
    global_.clear();
    bonusCache_.clear();
    save();
}

std::optional<WordId> UserModel::winner(const Picks& picks) {
    std::optional<WordId> best;
    Pick bestPick;
    for (const auto& [word, pick] : picks) {
        if (!best || std::pair(bestPick.count, bestPick.lastUsed) < std::pair(pick.count, pick.lastUsed)) {
            best = word;
            bestPick = pick;
        }
    }
    return best;
}

UserModel::Pick UserModel::add(WordId word, Picks& picks, double time) {
    const auto it = picks.find(word);
    Pick pick = it != picks.end() ? it->second : Pick{0, time};
    pick.count = std::min(kMaxCount, pick.count + 1);
    pick.lastUsed = time;
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

void UserModel::load() {
    if (!file_) return;
    std::ifstream in(*file_, std::ios::binary);
    if (!in) return;
    const std::string data((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    const nlohmann::json root = nlohmann::json::parse(data, nullptr, false);
    if (root.is_discarded() || !root.is_object()) return;
    const auto unigramRecords = readRecords(root, "unigram");
    const auto contextualRecords = readRecords(root, "contextual");

    // 文字 → 編號：只找記憶裡用到的詞，掃一次詞表就好，不建整份索引（每個程式都會載入記憶，要輕）。
    std::unordered_map<std::string, WordId, StringHash, std::equal_to<>> wordIndex;
    constexpr WordId kMissing = std::numeric_limits<WordId>::max();
    for (const auto& item : unigramRecords) {
        const auto parts = splitTabs(item.first);
        if (!parts.empty()) wordIndex.emplace(parts[0], kMissing);
    }
    for (const auto& item : contextualRecords) {
        const auto parts = splitTabs(item.first);
        for (std::size_t i = 0; i < parts.size() && i < 2; ++i) wordIndex.emplace(parts[i], kMissing);
    }
    if (!wordIndex.empty()) {
        const auto& words = lm_->words();
        for (std::size_t i = 0; i < words.size(); ++i) {
            if (const auto it = wordIndex.find(words[i]); it != wordIndex.end()) it->second = static_cast<WordId>(i);
        }
    }
    const auto idOf = [&](const std::string& text) -> std::optional<WordId> {
        const auto it = wordIndex.find(text);
        if (it == wordIndex.end() || it->second == kMissing) return std::nullopt;
        return it->second;
    };

    std::map<std::string, Record> convertedContextual;
    std::map<std::string, Record> convertedUnigram;
    std::unordered_map<std::string, std::vector<std::string>> readingsOfWord;
    for (const auto& [key, record] : unigramRecords) {
        const auto parts = splitTabs(key);
        if (parts.size() != 2) continue;
        const auto w = idOf(parts[0]);
        const auto syllables = Syllable::parseReading(parts[1]);
        if (!w || !syllables) continue;
        const Pick pick{std::min(kMaxCount, record.first), record.second};
        global_[ReadingKey::of(*syllables)][*w] = pick;
        convertedUnigram[key] = Record{pick.count, pick.lastUsed};
        readingsOfWord[parts[0]].push_back(parts[1]);
    }
    for (const auto& [key, record] : contextualRecords) {
        const auto parts = splitTabs(key);
        if (parts.size() < 2) continue;
        const auto p = idOf(parts[0]);
        const auto w = idOf(parts[1]);
        if (!p || !w) continue;
        // 舊格式（沒有 version、上下文鍵只有兩段）沒有讀音：用同一個詞的單詞記錄補上。
        std::vector<std::string> readings;
        if (parts.size() == 3) {
            readings.push_back(parts[2]);
        } else if (const auto it = readingsOfWord.find(parts[1]); it != readingsOfWord.end()) {
            readings = it->second;
        }
        for (const auto& reading : readings) {
            const auto syllables = Syllable::parseReading(reading);
            if (!syllables) continue;
            const Pick pick{std::min(kMaxCount, record.first), record.second};
            contextual_[ContextSlot{*p, ReadingKey::of(*syllables)}][*w] = pick;
            convertedContextual[parts[0] + "\t" + parts[1] + "\t" + reading] = Record{pick.count, pick.lastUsed};
        }
    }
    storedContextual_ = std::move(convertedContextual);
    storedUnigram_ = std::move(convertedUnigram);
}

void UserModel::save() const {
    if (!file_) return;
    try {
        nlohmann::json root = nlohmann::json::object();
        root["version"] = 2;
        root["contextual"] = nlohmann::json::object();
        root["unigram"] = nlohmann::json::object();
        for (const auto& [key, r] : storedContextual_) root["contextual"][key] = {{"count", r.count}, {"lastUsed", r.lastUsed}};
        for (const auto& [key, r] : storedUnigram_) root["unigram"][key] = {{"count", r.count}, {"lastUsed", r.lastUsed}};
        const std::string text = root.dump(2, ' ', false, nlohmann::json::error_handler_t::replace) + "\n";

        // 先寫暫存檔再取代，寫到一半當掉也不會留下壞掉的記憶檔。
        // 多個程式同時寫入的合併與鎖定在第 6 階段處理。
        std::error_code ec;
        std::filesystem::create_directories(file_->parent_path(), ec);
        std::filesystem::path temp = *file_;
        temp += L".tmp-" + std::to_wstring(GetCurrentProcessId());
        bool written = false;
        {
            std::ofstream out(temp, std::ios::binary | std::ios::trunc);
            out.write(text.data(), static_cast<std::streamsize>(text.size()));
            written = static_cast<bool>(out);
        }
        if (written) std::filesystem::rename(temp, *file_, ec);
        if (!written || ec) std::filesystem::remove(temp, ec);
    } catch (...) {
        // 寫不進去（例如沙盒裡的 Store 程式）時安靜略過：記憶仍在記憶體裡有效。
    }
}

}  // namespace bangbang
