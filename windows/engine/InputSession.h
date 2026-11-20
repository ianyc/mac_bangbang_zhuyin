#pragma once

// 對應 Sources/BangbangEngine/InputSession.swift：注音模式下的按鍵狀態機。

#include <algorithm>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "Bopomofo.h"
#include "Composition.h"
#include "Decoder.h"

namespace bangbang {

class UserModel;

/// 與鍵盤配置無關的按鍵事件。字元一律是美式鍵盤上「沒按 Shift」時的字元（字母為小寫）。
struct KeyEvent {
    enum class Type : std::uint8_t {
        Character, Numpad, Space, Enter, Backspace, ForwardDelete, Escape, Tab,
        Left, Right, Up, Down, Home, End, PageUp, PageDown,
    };
    Type type = Type::Character;
    char ch = 0;
    bool shift = false;

    static KeyEvent character(char c, bool shift = false) { return {Type::Character, c, shift}; }
    static KeyEvent numpad(char c) { return {Type::Numpad, c, false}; }
    static KeyEvent of(Type t) { return {t, 0, false}; }

    friend bool operator==(const KeyEvent&, const KeyEvent&) = default;
};

/// 一次按鍵處理的結果，由輸入法外殼套用到目前的 App。
struct SessionOutput {
    /// false 表示這個按鍵交還給 App 處理。
    bool handled = false;
    /// 要直接送出到 App 的文字（UTF-8）。
    std::string commit;
    bool beep = false;

    static SessionOutput passThrough() { return {false, {}, false}; }
    static SessionOutput consumed() { return {true, {}, false}; }
    static SessionOutput rejected() { return {true, {}, true}; }

    friend bool operator==(const SessionOutput&, const SessionOutput&) = default;
};

/// 候選字視窗的狀態。
struct CandidateList {
    std::vector<Candidate> items;
    int highlighted = 0;
    int perPage = 9;

    int page() const { return highlighted / perPage; }
    int pageCount() const { return std::max(1, (static_cast<int>(items.size()) + perPage - 1) / perPage); }
    /// 本頁的範圍 [first, second)。
    std::pair<int, int> pageRange() const {
        const int start = page() * perPage;
        return {start, std::min(static_cast<int>(items.size()), start + perPage)};
    }
    int highlightedInPage() const { return highlighted - page() * perPage; }

    friend bool operator==(const CandidateList&, const CandidateList&) = default;
};

/// 注音模式下的按鍵狀態機：拼注音、整句選字、游標、候選字、標點符號。
class InputSession {
public:
    /// userModel 不擁有，可以是 nullptr（不記憶）。
    explicit InputSession(std::shared_ptr<const LanguageModel> lm, UserModel* userModel = nullptr);

    Composition composition;
    UserModel* const userModel;
    /// 組字區超過這個長度，就自動送出最前面的詞。
    int maxCompositionLength = 20;
    int candidatesPerPage = 9;

    const BopomofoComposer& composer() const { return composer_; }
    const std::optional<CandidateList>& candidateList() const { return candidateList_; }

    bool isComposing() const { return !composer_.isEmpty() || !composition.isEmpty(); }
    /// 要顯示在 App 裡、帶底線的組字文字。
    std::string markedText() const;
    /// 組字文字中游標的位置（以字元，也就是碼位計）。
    int markedCursor() const;
    /// 候選字對應的組字範圍 [first, second)（以字元計），用來標示正在選哪幾個字。
    std::optional<std::pair<int, int>> candidateRange() const;

    /// 送出所有組字（切換 App、切換輸入法時用）。
    std::string commitAll();
    void reset();

    SessionOutput handle(const KeyEvent& key);
    /// 不改變任何狀態，回答 handle(key) 會不會處理這個按鍵（給 TSF 的 OnTestKeyDown 用）。
    bool wouldHandle(const KeyEvent& key) const;

private:
    const LanguageModel& lm() const { return composition.decoder.lm(); }
    SessionOutput handleCharacter(char c, bool shift);
    SessionOutput completeSyllable(std::uint8_t tone);
    SessionOutput insertLiteral(const std::string& text);
    bool openCandidates();
    std::optional<SessionOutput> handleCandidateKey(const KeyEvent& key);
    void choose(const Candidate& candidate);
    bool characterWouldBeHandled(char c, bool shift) const;
    static bool candidateKeyWouldBeHandled(const KeyEvent& key);

    BopomofoComposer composer_;
    std::optional<CandidateList> candidateList_;
};

}  // namespace bangbang
