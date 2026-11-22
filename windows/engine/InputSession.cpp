#include "InputSession.h"

#include <algorithm>

#include "Punctuation.h"
#include "UserModel.h"
#include "Utf8.h"

namespace bangbang {

namespace {

bool isAsciiLetter(char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
}

char toUpperAscii(char c) {
    return c >= 'a' && c <= 'z' ? static_cast<char>(c - 'a' + 'A') : c;
}

}  // namespace

InputSession::InputSession(std::shared_ptr<const LanguageModel> lm, UserModel* model)
    : composition(Decoder(std::move(lm), model)), userModel(model) {}

std::string InputSession::markedText() const {
    return composition.textBeforeCursor() + composer_.display() + composition.textAfterCursor();
}

int InputSession::markedCursor() const {
    return composition.cursor() + static_cast<int>(utf8::length(composer_.display()));
}

std::optional<std::pair<int, int>> InputSession::candidateRange() const {
    if (!candidateList_) return std::nullopt;
    const auto& item = candidateList_->items[candidateList_->highlighted];
    return std::pair{item.start, item.start + item.length};
}

std::string InputSession::commitAll() {
    std::string text = composition.text();
    reset();
    return text;
}

void InputSession::reset() {
    composer_.clear();
    composition.clear();
    candidateList_.reset();
}

SessionOutput InputSession::handle(const KeyEvent& key) {
    using T = KeyEvent::Type;
    if (candidateList_) {
        if (auto out = handleCandidateKey(key)) return *out;
        candidateList_.reset();
    }
    switch (key.type) {
    case T::Character:
        return handleCharacter(key.ch, key.shift);
    case T::Numpad:
        return insertLiteral(std::string(1, key.ch));
    case T::Space:
        if (!composer_.isEmpty()) return completeSyllable(1);
        // 組字中按空白：在組字區放一個半形空白，按 Enter 才送出。
        if (!composition.isEmpty()) return insertLiteral(" ");
        return SessionOutput::passThrough();
    case T::Enter: {
        if (!isComposing()) return SessionOutput::passThrough();
        std::string text = composition.text();
        reset();
        return SessionOutput{true, std::move(text), false};
    }
    case T::Backspace:
        if (!composer_.isEmpty()) {
            composer_.backspace();
            return SessionOutput::consumed();
        }
        if (composition.isEmpty()) return SessionOutput::passThrough();
        return composition.deleteBackward() ? SessionOutput::consumed() : SessionOutput::rejected();
    case T::ForwardDelete:
        if (!isComposing()) return SessionOutput::passThrough();
        if (!composer_.isEmpty()) return SessionOutput::rejected();
        return composition.deleteForward() ? SessionOutput::consumed() : SessionOutput::rejected();
    case T::Escape:
        if (!composer_.isEmpty()) {
            composer_.clear();
            return SessionOutput::consumed();
        }
        if (composition.isEmpty()) return SessionOutput::passThrough();
        reset();
        return SessionOutput::consumed();
    case T::Left:
    case T::Right:
    case T::Home:
    case T::End: {
        if (!isComposing()) return SessionOutput::passThrough();
        if (!composer_.isEmpty()) return SessionOutput::rejected();
        int target = 0;
        switch (key.type) {
        case T::Left: target = composition.cursor() - 1; break;
        case T::Right: target = composition.cursor() + 1; break;
        case T::Home: target = 0; break;
        default: target = composition.count(); break;
        }
        return composition.moveCursor(target) ? SessionOutput::consumed() : SessionOutput::rejected();
    }
    case T::Down:
        if (!isComposing()) return SessionOutput::passThrough();
        if (!composer_.isEmpty()) return SessionOutput::rejected();
        return openCandidates() ? SessionOutput::consumed() : SessionOutput::rejected();
    case T::Up:
    case T::Tab:
    case T::PageUp:
    case T::PageDown:
        return isComposing() ? SessionOutput::consumed() : SessionOutput::passThrough();
    }
    return SessionOutput::passThrough();
}

bool InputSession::wouldHandle(const KeyEvent& key) const {
    using T = KeyEvent::Type;
    // 與 handle() 的判斷一一對應；InputSessionTests 會逐一比對兩者的結果。
    if (candidateList_ && candidateKeyWouldBeHandled(key)) return true;
    switch (key.type) {
    case T::Character:
        return characterWouldBeHandled(key.ch, key.shift);
    case T::Numpad:
        return true;
    case T::Space:
    case T::Backspace:
    case T::Escape:
        return !composer_.isEmpty() || !composition.isEmpty();
    case T::Enter:
    case T::ForwardDelete:
    case T::Left:
    case T::Right:
    case T::Home:
    case T::End:
    case T::Down:
    case T::Up:
    case T::Tab:
    case T::PageUp:
    case T::PageDown:
        return isComposing();
    }
    return false;
}

bool InputSession::characterWouldBeHandled(char c, bool shift) const {
    if (shift) {
        if (isAsciiLetter(c)) return true;
        if (Punctuation::shifted(c)) return true;
        return isComposing();
    }
    if (DaChenLayout::component(c)) return true;  // 注音鍵、聲調鍵都會處理（可能是嗶聲）
    if (Punctuation::unshifted(c)) return true;
    return isComposing();
}

bool InputSession::candidateKeyWouldBeHandled(const KeyEvent& key) {
    using T = KeyEvent::Type;
    switch (key.type) {
    case T::Character:
        return !key.shift && key.ch >= '1' && key.ch <= '9';
    case T::Enter:
    case T::Down:
    case T::Up:
    case T::Space:
    case T::Right:
    case T::PageDown:
    case T::Left:
    case T::PageUp:
    case T::Escape:
    case T::Backspace:
        return true;
    default:
        return false;
    }
}

// MARK: - 一般按鍵

SessionOutput InputSession::handleCharacter(char c, bool shift) {
    if (shift) {
        // Shift + 字母：半形大寫英文。
        if (isAsciiLetter(c)) return insertLiteral(std::string(1, toUpperAscii(c)));
        if (const auto p = Punctuation::shifted(c)) return insertLiteral(*p);
        return isComposing() ? SessionOutput::consumed() : SessionOutput::passThrough();
    }
    if (const auto component = DaChenLayout::component(c)) {
        if (component->kind == BopomofoComponent::Kind::Tone) {
            // 沒有注音時按聲調鍵：不做事（組字中嗶一聲）。
            if (composer_.isEmpty()) return isComposing() ? SessionOutput::rejected() : SessionOutput::consumed();
            return completeSyllable(component->value);
        }
        composer_.receive(*component);
        return SessionOutput::consumed();
    }
    if (const auto p = Punctuation::unshifted(c)) return insertLiteral(*p);
    return isComposing() ? SessionOutput::consumed() : SessionOutput::passThrough();
}

SessionOutput InputSession::completeSyllable(std::uint8_t tone) {
    auto completed = composer_.completed(tone);
    if (!completed) return SessionOutput::rejected();
    Syllable s = *completed;
    if (!lm().hasReading(s)) {
        // ㄐㄑㄒ 後面打成 ㄨ 時，自動改成 ㄩ。
        if ((s.consonant == 12 || s.consonant == 13 || s.consonant == 14) && s.medial == 2) {
            Syllable fixed = s;
            fixed.medial = 3;
            if (lm().hasReading(fixed)) s = fixed;
        }
        if (!lm().hasReading(s)) return SessionOutput::rejected();
    }
    composer_.clear();
    composition.insert(Token::syllableToken(s));
    return SessionOutput{true, composition.commitLeading(maxCompositionLength), false};
}

SessionOutput InputSession::insertLiteral(const std::string& text) {
    // 英文字母、標點：組字中一律放進組字區。沒有組字時，全形標點也進入組字區（按 Enter 才送出，
    // 同微軟新注音）；半形英數符號則直接送出。
    composer_.clear();
    if (composition.isEmpty() && utf8::isAscii(text)) return SessionOutput{true, text, false};
    composition.insert(Token::literalToken(text));
    return SessionOutput{true, composition.commitLeading(maxCompositionLength), false};
}

// MARK: - 候選字

bool InputSession::openCandidates() {
    auto items = composition.candidates();
    if (items.empty()) return false;
    // 目前顯示的詞排第一筆（同微軟新注音）；它是更長詞的一部分時，把目前顯示的那個字排第一。
    const int position = items[0].start;
    const auto shown = utf8::characters(composition.text());
    const auto& walk = composition.walk();
    const auto node = std::find_if(walk.begin(), walk.end(), [&](const WalkNode& n) { return n.start <= position && position < n.end(); });
    if (node != walk.end()) {
        auto current = std::find_if(items.begin(), items.end(), [&](const Candidate& c) {
            return c.start == node->start && c.length == node->length && c.word == node->word;
        });
        if (current == items.end() && position < static_cast<int>(shown.size())) {
            current = std::find_if(items.begin(), items.end(), [&](const Candidate& c) { return c.length == 1 && c.text == shown[position]; });
        }
        if (current != items.end()) std::rotate(items.begin(), current, current + 1);
    }
    candidateList_ = CandidateList{std::move(items), 0, candidatesPerPage};
    return true;
}

std::optional<SessionOutput> InputSession::handleCandidateKey(const KeyEvent& key) {
    using T = KeyEvent::Type;
    if (!candidateList_) return std::nullopt;
    CandidateList list = *candidateList_;
    const int count = static_cast<int>(list.items.size());
    switch (key.type) {
    case T::Character: {
        if (key.shift || key.ch < '1' || key.ch > '9') return std::nullopt;
        const int index = list.page() * list.perPage + (key.ch - '1');
        if (index >= count || index >= (list.page() + 1) * list.perPage) return SessionOutput::rejected();
        choose(list.items[index]);
        return SessionOutput::consumed();
    }
    case T::Enter:
        choose(list.items[list.highlighted]);
        return SessionOutput::consumed();
    case T::Down:
        if (list.highlighted + 1 >= count) return SessionOutput::rejected();
        list.highlighted += 1;
        break;
    case T::Up:
        if (list.highlighted <= 0) return SessionOutput::rejected();
        list.highlighted -= 1;
        break;
    case T::Space:
    case T::Right:
    case T::PageDown: {
        const int next = (list.page() + 1) * list.perPage;
        list.highlighted = next < count ? next : 0;
        break;
    }
    case T::Left:
    case T::PageUp: {
        const int prev = list.page() - 1;
        list.highlighted = (prev >= 0 ? prev : list.pageCount() - 1) * list.perPage;
        break;
    }
    case T::Escape:
    case T::Backspace:
        candidateList_.reset();
        return SessionOutput::consumed();
    default:
        return std::nullopt;
    }
    candidateList_ = std::move(list);
    return SessionOutput::consumed();
}

bool InputSession::highlightCandidate(int index) {
    if (!candidateList_ || index < 0 || index >= static_cast<int>(candidateList_->items.size())) return false;
    candidateList_->highlighted = index;
    return true;
}

bool InputSession::chooseCandidate(int index) {
    if (!candidateList_ || index < 0 || index >= static_cast<int>(candidateList_->items.size())) return false;
    choose(candidateList_->items[static_cast<std::size_t>(index)]);
    return true;
}

void InputSession::choose(const Candidate& candidateRef) {
    const Candidate candidate = candidateRef;  // 選字會清掉候選清單，先複製
    const bool wasAtEnd = composition.cursor() == composition.count();
    composition.select(candidate);
    if (candidate.word && userModel) {
        std::vector<std::uint16_t> codes;
        for (int i = candidate.start; i < candidate.start + candidate.length; ++i) {
            if (const auto s = composition.tokens()[i].syllable()) codes.push_back(s->code());
        }
        userModel->learn(composition.wordEnding(candidate.start), *candidate.word, ReadingKey(std::move(codes)));
    }
    // 選完後游標移到這個詞後面，方便繼續往後修正。
    if (!wasAtEnd) composition.moveCursor(candidate.start + candidate.length);
    candidateList_.reset();
}

}  // namespace bangbang
