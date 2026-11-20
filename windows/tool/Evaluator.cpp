#include "Evaluator.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <limits>

#include "Utf8.h"

namespace bangbang {

namespace {

constexpr float kLowest = -std::numeric_limits<float>::max();

std::vector<char32_t> scalarsOf(std::string_view text) {
    std::vector<char32_t> result;
    for (std::size_t i = 0; i < text.size();) result.push_back(utf8::decode(text, i));
    return result;
}

/// 去掉頭尾的空白（同 Swift 的 trimmingCharacters(in: .whitespaces)：空白、Tab、全形空白、不換行空白）。
std::string trimWhitespace(std::string_view s) {
    static constexpr std::string_view kSpaces[] = {" ", "\t", "\r", "　", " "};
    const auto chars = utf8::characters(s);
    const auto isSpace = [](const std::string& c) { return std::find(std::begin(kSpaces), std::end(kSpaces), c) != std::end(kSpaces); };
    std::size_t begin = 0, end = chars.size();
    while (begin < end && isSpace(chars[begin])) ++begin;
    while (end > begin && isSpace(chars[end - 1])) --end;
    std::string out;
    for (std::size_t i = begin; i < end; ++i) out += chars[i];
    return out;
}

void info(const std::string& message) {
    std::fputs((message + "\n").c_str(), stderr);
}

}  // namespace

ScalarTrie::ScalarTrie(const WordTable& words) {
    for (std::size_t id = 1; id < words.size(); ++id) {
        std::int32_t node = 0;
        const std::string_view word = words[id];
        for (std::size_t i = 0; i < word.size();) {
            const char32_t scalar = utf8::decode(word, i);
            const std::uint64_t key = static_cast<std::uint64_t>(static_cast<std::uint32_t>(node)) << 32 | scalar;
            if (const auto it = edges_.find(key); it != edges_.end()) {
                node = it->second;
            } else {
                const auto next = static_cast<std::int32_t>(terminal_.size());
                terminal_.push_back(-1);
                edges_.emplace(key, next);
                node = next;
            }
        }
        terminal_[static_cast<std::size_t>(node)] = static_cast<std::int32_t>(id);
    }
}

std::optional<std::int32_t> ScalarTrie::child(std::int32_t node, char32_t scalar) const {
    const auto it = edges_.find(static_cast<std::uint64_t>(static_cast<std::uint32_t>(node)) << 32 | scalar);
    if (it == edges_.end()) return std::nullopt;
    return it->second;
}

Evaluator::Evaluator(std::shared_ptr<const LanguageModel> lm) : lm_(lm), decoder_(lm), trie_(lm->words()) {
    for (const auto& [key, entries] : lm_->lexicon()) {
        for (const auto& e : entries) {
            readingsOfWord_[e.word].push_back(key);
            if (key.codes.size() == 1) singleWeight_[e.word][key.codes[0]] = e.readingWeight;
        }
    }
    // 讀音依音節排序，同分時固定選第一個，評測結果才能重現。
    for (auto& [word, keys] : readingsOfWord_) std::sort(keys.begin(), keys.end());
    const auto& words = lm_->words();
    for (std::size_t i = 1; i < words.size(); ++i) wordIndex_[std::string(words[i])] = static_cast<WordId>(i);
}

std::optional<std::vector<Syllable>> Evaluator::reading(std::string_view text) const {
    const auto scalars = scalarsOf(text);
    const std::size_t n = scalars.size();
    if (n == 0) return std::nullopt;
    std::vector<float> best(n + 1, kLowest);
    std::vector<std::size_t> from(n + 1, 0);
    std::vector<WordId> word(n + 1, 0);
    best[0] = 0;
    for (std::size_t i = 0; i < n; ++i) {
        if (!(best[i] > kLowest)) continue;
        std::int32_t node = 0;
        std::size_t j = i;
        while (j < n) {
            const auto next = trie_.child(node, scalars[j]);
            if (!next) break;
            node = *next;
            j += 1;
            const std::int32_t w = trie_.terminal(node);
            if (w >= 0 && readingsOfWord_.contains(static_cast<WordId>(w))) {
                const float s = best[i] + lm_->logUnigram()[static_cast<std::size_t>(w)];
                if (s > best[j]) {
                    best[j] = s;
                    from[j] = i;
                    word[j] = static_cast<WordId>(w);
                }
            }
        }
    }
    if (!(best[n] > kLowest)) return std::nullopt;
    std::vector<WordId> words;
    for (std::size_t k = n; k > 0; k = from[k]) words.push_back(word[k]);
    std::vector<Syllable> result;
    for (auto it = words.rbegin(); it != words.rend(); ++it) {
        const auto found = readingsOfWord_.find(*it);
        if (found == readingsOfWord_.end()) return std::nullopt;
        // 同 Swift 的 max(by:)：同分時取第一個。
        const ReadingKey* chosen = &found->second.front();
        float chosenScore = score(*chosen, *it);
        for (std::size_t k = 1; k < found->second.size(); ++k) {
            const float s = score(found->second[k], *it);
            if (chosenScore < s) {
                chosen = &found->second[k];
                chosenScore = s;
            }
        }
        for (const auto& s : chosen->syllables()) result.push_back(s);
    }
    return result;
}

float Evaluator::score(const ReadingKey& key, WordId word) const {
    // 詞的讀音取「每個字在該讀音下的權重總和」最高者。
    if (key.codes.size() == 1) {
        const auto w = singleWeight_.find(word);
        if (w == singleWeight_.end()) return 0;
        const auto c = w->second.find(key.codes[0]);
        return c == w->second.end() ? 0.0f : c->second;
    }
    const auto chars = utf8::characters(lm_->words()[word]);
    float s = 0;
    for (std::size_t i = 0; i < key.codes.size() && i < chars.size(); ++i) {
        const auto c = wordIndex_.find(chars[i]);
        if (c == wordIndex_.end()) continue;
        const auto w = singleWeight_.find(c->second);
        float weight = -5;
        if (w != singleWeight_.end()) {
            if (const auto found = w->second.find(key.codes[i]); found != w->second.end()) weight = found->second;
        }
        s += weight;
    }
    return s;
}

std::string Evaluator::convert(const std::vector<Syllable>& syllables) const {
    std::vector<Token> tokens;
    tokens.reserve(syllables.size());
    for (const auto& s : syllables) tokens.push_back(Token::syllableToken(s));
    std::string out;
    for (const auto& node : decoder_.decode(tokens)) out += node.text;
    return out;
}

bool Evaluator::equivalent(std::string_view a, std::string_view b) {
    static const std::vector<std::vector<std::string_view>> groups = {{"他", "她", "它", "牠"}, {"台", "臺"}};
    if (a == b) return true;
    for (const auto& g : groups) {
        if (std::find(g.begin(), g.end(), a) != g.end() && std::find(g.begin(), g.end(), b) != g.end()) return true;
    }
    return false;
}

Evaluator::Result Evaluator::evaluate(const std::vector<std::string>& lines, std::size_t limit) const {
    Result result;
    const std::size_t count = std::min(limit, lines.size());
    for (std::size_t li = 0; li < count; ++li) {
        const std::string trimmed = trimWhitespace(lines[li]);
        if (trimmed.empty() || trimmed.front() == '#') continue;
        std::string expected = trimmed;
        std::optional<std::vector<Syllable>> syllables;
        if (const auto bar = trimmed.find('|'); bar != std::string::npos) {
            expected = trimWhitespace(std::string_view(trimmed).substr(0, bar));
            syllables = Syllable::parseReading(trimWhitespace(std::string_view(trimmed).substr(bar + 1)));
        } else {
            syllables = reading(expected);
        }
        const auto expectedChars = utf8::characters(expected);
        if (!syllables || syllables->size() != expectedChars.size()) {
            info("無法轉成注音：" + expected);
            continue;
        }
        const auto decodeStart = std::chrono::steady_clock::now();
        const std::string got = convert(*syllables);
        result.decodeSeconds += std::chrono::duration<double>(std::chrono::steady_clock::now() - decodeStart).count();
        const auto gotChars = utf8::characters(got);
        result.sentences += 1;
        result.chars += static_cast<long long>(expectedChars.size());
        long long same = 0;
        for (std::size_t i = 0; i < expectedChars.size() && i < gotChars.size(); ++i) {
            if (equivalent(expectedChars[i], gotChars[i])) ++same;
        }
        result.correctChars += same;
        if (same == static_cast<long long>(expectedChars.size())) {
            result.exact += 1;
        } else {
            std::string readingText;
            for (std::size_t i = 0; i < syllables->size(); ++i) {
                if (i > 0) readingText += ' ';
                readingText += (*syllables)[i].description();
            }
            result.failures.push_back({expected, got, readingText});
        }
    }
    return result;
}

}  // namespace bangbang
