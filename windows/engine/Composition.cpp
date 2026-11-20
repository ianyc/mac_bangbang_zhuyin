#include "Composition.h"

#include <algorithm>

#include "Punctuation.h"
#include "Utf8.h"

namespace bangbang {

Composition::Composition(Decoder d) : decoder(std::move(d)) {}

std::string Composition::text() const {
    std::string out;
    for (const auto& node : walk_) out += node.text;
    return out;
}

std::string Composition::textBeforeCursor() const {
    return std::string(utf8::prefix(text(), static_cast<std::size_t>(cursor_)));
}

std::string Composition::textAfterCursor() const {
    return std::string(utf8::dropFirst(text(), static_cast<std::size_t>(cursor_)));
}

void Composition::insert(Token token) {
    // 插入點落在某個釘選詞中間時，該釘選失效。
    std::erase_if(pins_, [&](const Pin& p) { return p.start < cursor_ && cursor_ < p.end(); });
    for (auto& p : pins_) {
        if (p.start >= cursor_) p.start += 1;
    }
    tokens_.insert(tokens_.begin() + cursor_, std::move(token));
    cursor_ += 1;
    rewalk();
}

bool Composition::deleteBackward() {
    if (cursor_ <= 0) return false;
    remove(cursor_ - 1);
    cursor_ -= 1;
    rewalk();
    return true;
}

bool Composition::deleteForward() {
    if (cursor_ >= count()) return false;
    remove(cursor_);
    rewalk();
    return true;
}

void Composition::remove(int index) {
    std::erase_if(pins_, [&](const Pin& p) { return p.start <= index && index < p.end(); });
    for (auto& p : pins_) {
        if (p.start > index) p.start -= 1;
    }
    tokens_.erase(tokens_.begin() + index);
}

bool Composition::moveCursor(int position) {
    if (position < 0 || position > count() || position == cursor_) return false;
    cursor_ = position;
    return true;
}

void Composition::clear() {
    tokens_.clear();
    cursor_ = 0;
    pins_.clear();
    walk_.clear();
}

std::vector<Candidate> Composition::candidates() const {
    return decoder.candidates(tokens_, cursor_, walk_);
}

void Composition::select(const Candidate& candidate) {
    if (!candidate.word) {
        replaceLiteral(candidate.start, candidate.text);
        return;
    }
    freezeText(candidate.start);
    std::erase_if(pins_, [&](const Pin& p) { return p.start < candidate.start + candidate.length && candidate.start < p.end(); });
    pins_.push_back(Pin{candidate.start, candidate.length, *candidate.word});
    rewalk();
}

void Composition::replaceLiteral(int index, const std::string& text) {
    if (index < 0 || index >= count() || !tokens_[index].isLiteral()) return;
    const std::string old = tokens_[index].text;
    tokens_[index] = Token::literalToken(text);
    if (const auto match = matchingBracket(old, index)) {
        if (const auto replacement = Brackets::counterpart(old, text)) tokens_[*match] = Token::literalToken(*replacement);
    }
    rewalk();
}

std::optional<int> Composition::matchingBracket(const std::string& bracket, int index) const {
    if (const auto close = Brackets::closing(bracket)) {
        int depth = 0;
        for (int i = index + 1; i < count(); ++i) {
            if (!tokens_[i].isLiteral()) continue;
            const std::string& t = tokens_[i].text;
            if (t == bracket) depth += 1;
            if (t == *close) {
                if (depth == 0) return i;
                depth -= 1;
            }
        }
    } else if (const auto open = Brackets::opening(bracket)) {
        int depth = 0;
        for (int i = index - 1; i >= 0; --i) {
            if (!tokens_[i].isLiteral()) continue;
            const std::string& t = tokens_[i].text;
            if (t == bracket) depth += 1;
            if (t == *open) {
                if (depth == 0) return i;
                depth -= 1;
            }
        }
    }
    return std::nullopt;
}

void Composition::freezeText(int position) {
    // 把 position 之前目前顯示的字都釘住。跨過 position 的詞拆成單字釘住前半段。
    const LanguageModel& lm = decoder.lm();
    for (const auto& node : walk_) {
        if (node.start >= position) continue;
        if (!node.word) continue;  // 標點、英文本來就不會變
        if (node.end() <= position) {
            const Pin pin{node.start, node.length, *node.word};
            if (std::find(pins_.begin(), pins_.end(), pin) == pins_.end()) pins_.push_back(pin);
            continue;
        }
        const auto chars = utf8::characters(node.text);
        for (int i = node.start; i < position; ++i) {
            const auto s = tokens_[i].syllable();
            if (!s || i - node.start >= static_cast<int>(chars.size())) continue;
            const std::string& ch = chars[i - node.start];
            for (const auto e : lm.entries(ReadingKey({s->code()}))) {
                if (lm.words()[e.word] == ch) {
                    pins_.push_back(Pin{i, 1, e.word});
                    break;
                }
            }
        }
    }
}

WordId Composition::wordEnding(int position) const {
    if (position <= 0) return LanguageModel::kBoundary;
    for (const auto& node : walk_) {
        if (node.end() == position) return node.word.value_or(LanguageModel::kBoundary);
    }
    return LanguageModel::kBoundary;
}

std::string Composition::commitLeading(int maxTokens) {
    std::string committed;
    while (count() > maxTokens && !walk_.empty()) {
        const WalkNode first = walk_.front();
        committed += first.text;
        for (int k = 0; k < first.length; ++k) remove(0);
        cursor_ = std::max(0, cursor_ - first.length);
        walk_.erase(walk_.begin());
        for (auto& node : walk_) node.start -= first.length;
    }
    if (!committed.empty()) rewalk();
    return committed;
}

void Composition::rewalk() {
    walk_ = decoder.decode(tokens_, pins_);
}

}  // namespace bangbang
