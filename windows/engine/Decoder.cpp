#include "Decoder.h"

#include <algorithm>
#include <limits>

#include "Punctuation.h"

namespace bangbang {

namespace {

constexpr float kLowest = -std::numeric_limits<float>::max();  // Swift 的 -Float.greatestFiniteMagnitude

/// 搜尋狀態：走到某節點、且前一個詞是 prevWord 時的最佳分數。
struct State {
    float score = 0;
    WordId prevWord = 0;
    int backNode = -1;
    int backState = -1;
};

}  // namespace

std::string Candidate::displayText() const {
    if (text == " ") return "半形空白";
    if (text == "　") return "全形空白";
    return text;
}

Decoder::Decoder(std::shared_ptr<const LanguageModel> lm, ScoreAdjuster* adj) : adjuster(adj), lm_(std::move(lm)) {}

std::vector<WalkNode> Decoder::decode(const std::vector<Token>& tokens, const std::vector<Pin>& pins, WordId context) const {
    const int n = static_cast<int>(tokens.size());
    if (n == 0) return {};
    std::vector<ReadingKey> keys;
    const auto nodes = buildLattice(tokens, pins, keys);
    const int nodeCount = static_cast<int>(nodes.size());

    // 結束在每個位置的節點（依節點順序），存成一整塊：endsAt[e] = endList[endStart[e] ..< endStart[e + 1]]。
    std::vector<int> endStart(static_cast<std::size_t>(n) + 2, 0);
    for (const auto& node : nodes) endStart[static_cast<std::size_t>(node.end()) + 1] += 1;
    for (std::size_t e = 1; e < endStart.size(); ++e) endStart[e] += endStart[e - 1];
    std::vector<int> endList(static_cast<std::size_t>(nodeCount));
    {
        std::vector<int> fill(endStart.begin(), endStart.end() - 1);
        for (int i = 0; i < nodeCount; ++i) endList[static_cast<std::size_t>(fill[static_cast<std::size_t>(nodes[i].end())]++)] = i;
    }

    // 二階搜尋：分數同時考慮前一個詞（雙連詞）與前前一個詞（隔詞搭配）。
    // nodes 已依起點排序，處理到某節點時，所有結束在它起點的前驅都已算完。
    // 每個節點最多保留 statesPerNode 個狀態，全部放在同一塊記憶體裡，避免每個節點各配置一次。
    const int width = std::max(1, statesPerNode);
    std::vector<State> stateBuffer(static_cast<std::size_t>(nodeCount) * static_cast<std::size_t>(width));
    std::vector<int> stateCount(static_cast<std::size_t>(nodeCount), 0);
    const auto stateAt = [&](int node, int k) -> State& { return stateBuffer[static_cast<std::size_t>(node) * width + k]; };
    std::vector<State> candidates;
    candidates.reserve(64);
    for (int j = 0; j < nodeCount; ++j) {
        const auto& node = nodes[j];
        if (node.start == 0) {
            stateAt(j, 0) = State{score(node, keys, context, LanguageModel::kBoundary), context, -1, -1};
            stateCount[j] = 1;
            continue;
        }
        candidates.clear();
        for (int e = endStart[node.start]; e < endStart[node.start + 1]; ++e) {
            const int m = endList[e];
            if (stateCount[m] == 0) continue;
            const auto& prevNode = nodes[m];
            // 直接輸入的字元（標點、英文）視為句子邊界。
            const WordId prevWord = prevNode.word.value_or(LanguageModel::kBoundary);
            float bestScore = kLowest;
            int bestIndex = 0;
            for (int k = 0; k < stateCount[m]; ++k) {
                const auto& st = stateAt(m, k);
                const WordId prev2 = prevNode.word ? st.prevWord : LanguageModel::kBoundary;
                const float s = st.score + (node.word ? lm_->skip(prev2, *node.word) : 0.0f);
                if (s > bestScore) {
                    bestScore = s;
                    bestIndex = k;
                }
            }
            candidates.push_back(State{bestScore + score(node, keys, prevWord, LanguageModel::kBoundary), prevWord, m, bestIndex});
        }
        if (!node.word) {
            // 標點之後重新開始，只需保留最好的一條（同分時取第一個，同 Swift 的 max(by:)）。
            if (!candidates.empty()) {
                State top = candidates.front();
                for (std::size_t c = 1; c < candidates.size(); ++c) {
                    if (top.score < candidates[c].score) top = candidates[c];
                }
                stateAt(j, 0) = top;
                stateCount[j] = 1;
            }
        } else {
            std::stable_sort(candidates.begin(), candidates.end(), [](const State& a, const State& b) { return a.score > b.score; });
            const int keep = std::min(static_cast<int>(candidates.size()), statesPerNode);
            for (int k = 0; k < keep; ++k) stateAt(j, k) = candidates[static_cast<std::size_t>(k)];
            stateCount[j] = keep;
        }
    }

    int lastNode = -1;
    float lastScore = kLowest;
    for (int e = endStart[n]; e < endStart[n + 1]; ++e) {
        const int m = endList[e];
        if (stateCount[m] > 0 && stateAt(m, 0).score > lastScore) {
            lastScore = stateAt(m, 0).score;
            lastNode = m;
        }
    }
    if (lastNode < 0) return fallbackWalk(tokens);
    std::vector<int> path;
    int cur = lastNode;
    int st = 0;
    while (cur >= 0) {
        path.push_back(cur);
        const State& s = stateAt(cur, st);
        cur = s.backNode;
        st = s.backState;
    }
    std::vector<WalkNode> walk;
    walk.reserve(path.size());
    for (auto it = path.rbegin(); it != path.rend(); ++it) {
        const auto& node = nodes[*it];
        walk.push_back(WalkNode{node.start, node.length, node.word,
                                node.literal ? *node.literal : std::string(lm_->words()[*node.word])});
    }
    return walk;
}

float Decoder::score(const LatticeNode& node, const std::vector<ReadingKey>& keys, WordId prev, WordId prev2) const {
    if (!node.word) return 0;
    const WordId word = *node.word;
    float s = lm_->logProb(prev, word) + node.weight + lm_->skip(prev2, word);
    if (adjuster && node.keyIndex >= 0) s += adjuster->bonus(prev, word, keys[static_cast<std::size_t>(node.keyIndex)]);
    return s;
}

std::vector<Decoder::LatticeNode> Decoder::buildLattice(const std::vector<Token>& tokens, const std::vector<Pin>& pins,
                                                        std::vector<ReadingKey>& keys) const {
    const int n = static_cast<int>(tokens.size());
    std::vector<LatticeNode> nodes;
    nodes.reserve(static_cast<std::size_t>(n) * 16);
    const auto allowed = [&](int start, int length, WordId word) {
        for (const auto& p : pins) {
            if (start < p.end() && p.start < start + length) {
                if (!(p.start == start && p.length == length && p.word == word)) return false;
            }
        }
        return true;
    };
    for (int i = 0; i < n; ++i) {
        if (tokens[i].isLiteral()) {
            nodes.push_back(LatticeNode{i, 1, std::nullopt, &tokens[i].text, -1, 0});
            continue;
        }
        std::vector<std::uint16_t> syllables;
        const int maxLength = std::min(lm_->maxPhraseLength(), n - i);
        for (int len = 1; len <= maxLength; ++len) {
            const auto s = tokens[i + len - 1].syllable();
            if (!s) break;
            syllables.push_back(s->code());
            const auto [entries, hasLonger] = lm_->lookup(syllables);
            // 沒有以這串音節開頭的更長詞時，就不必再往後延伸。
            if (entries.empty()) {
                if (hasLonger) continue;
                break;
            }
            // 讀音只給選字記憶加分用；沒有記憶時不必複製。
            int keyIndex = -1;
            if (adjuster) {
                keyIndex = static_cast<int>(keys.size());
                keys.emplace_back(syllables);
            }
            const int beam = len == 1 ? singleCharBeam : phraseBeam;
            int taken = 0;
            for (const auto e : entries) {
                if (taken >= beam) break;
                if (!allowed(i, len, e.word)) continue;
                nodes.push_back(LatticeNode{i, len, e.word, nullptr, keyIndex, e.readingWeight});
                ++taken;
            }
            // 被釘選的詞就算不在前幾名也要放進去。
            for (const auto& p : pins) {
                if (p.start != i || p.length != len) continue;
                const bool present = std::any_of(nodes.begin(), nodes.end(), [&](const LatticeNode& node) {
                    return node.start == i && node.length == len && node.word == p.word;
                });
                if (present) continue;
                for (const auto e : entries) {
                    if (e.word == p.word) {
                        nodes.push_back(LatticeNode{i, len, e.word, nullptr, keyIndex, e.readingWeight});
                        break;
                    }
                }
            }
            if (!hasLonger) break;
        }
    }
    return nodes;
}

std::vector<WalkNode> Decoder::fallbackWalk(const std::vector<Token>& tokens) const {
    // 理論上不會發生（每個音節都至少有一個字）；保險起見逐字取第一名。
    std::vector<WalkNode> walk;
    for (int i = 0; i < static_cast<int>(tokens.size()); ++i) {
        const auto& token = tokens[i];
        if (token.isLiteral()) {
            walk.push_back(WalkNode{i, 1, std::nullopt, token.text});
            continue;
        }
        const auto entries = lm_->entries(ReadingKey({token.sound.code()}));
        if (entries.empty()) {
            walk.push_back(WalkNode{i, 1, std::nullopt, "?"});
        } else {
            walk.push_back(WalkNode{i, 1, entries[0].word, std::string(lm_->words()[entries[0].word])});
        }
    }
    return walk;
}

std::vector<Candidate> Decoder::candidates(const std::vector<Token>& tokens, int cursor, const std::vector<WalkNode>& walk) const {
    const int n = static_cast<int>(tokens.size());
    if (n == 0) return {};
    const int position = cursor < n ? cursor : n - 1;
    if (tokens[position].isLiteral()) {
        std::vector<Candidate> result;
        for (auto& symbol : Punctuation::alternatives(tokens[position].text)) {
            result.push_back(Candidate{position, 1, std::nullopt, std::move(symbol)});
        }
        return result;
    }
    struct Span {
        int start;
        int length;
    };
    std::vector<Span> spans;
    if (cursor < n) {
        if (!tokens[cursor].syllable()) return {};
        int len = 0;
        while (len < lm_->maxPhraseLength() && cursor + len < n && tokens[cursor + len].syllable()) ++len;
        for (int l = len; l >= 1; --l) spans.push_back({cursor, l});
    } else {
        if (!tokens[n - 1].syllable()) return {};
        spans.push_back({n - 1, 1});
    }
    std::vector<Candidate> result;
    for (const auto& span : spans) {
        std::vector<std::uint16_t> codes;
        for (int i = span.start; i < span.start + span.length; ++i) {
            if (const auto s = tokens[i].syllable()) codes.push_back(s->code());
        }
        const ReadingKey key(std::move(codes));
        const auto entries = lm_->entries(key);
        if (entries.empty()) continue;
        // 前一個詞：選字結果中剛好結束在這個位置的詞；沒有的話是句子邊界。
        WordId prev = LanguageModel::kBoundary;
        for (auto it = walk.rbegin(); it != walk.rend(); ++it) {
            if (it->end() <= span.start) {
                if (it->end() == span.start) prev = it->word.value_or(LanguageModel::kBoundary);
                break;
            }
        }
        std::vector<std::pair<LexiconEntry, float>> ranked;
        ranked.reserve(entries.size());
        for (const auto e : entries) {
            float s = lm_->logProb(prev, e.word) + e.readingWeight;
            if (adjuster) s += adjuster->bonus(prev, e.word, key);
            ranked.emplace_back(e, s);
        }
        std::stable_sort(ranked.begin(), ranked.end(), [](const auto& a, const auto& b) { return a.second > b.second; });
        for (const auto& item : ranked) {
            const WordId word = item.first.word;
            result.push_back(Candidate{span.start, span.length, word, std::string(lm_->words()[word])});
        }
    }
    return result;
}

}  // namespace bangbang
