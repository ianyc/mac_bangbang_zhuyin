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
        /// 直接放進組字區的半形字元 ch（英文模式下接著組字區打英文時，見 InputSession::englishModeKey）。
        Literal,
    };
    Type type = Type::Character;
    char ch = 0;
    bool shift = false;
    /// 按著 Ctrl（Windows 配置的 Ctrl＋標點）。其他 Ctrl 組合鍵由外殼直接交給 App，不會送進來。
    bool ctrl = false;

    static KeyEvent character(char c, bool shift = false) { return {Type::Character, c, shift, false}; }
    static KeyEvent control(char c) { return {Type::Character, c, false, true}; }
    static KeyEvent numpad(char c) { return {Type::Numpad, c, false, false}; }
    static KeyEvent literal(char c) { return {Type::Literal, c, false, false}; }
    static KeyEvent of(Type t) { return {t, 0, false, false}; }

    friend bool operator==(const KeyEvent&, const KeyEvent&) = default;
};

/// 美式鍵盤上這個鍵在英文模式下打出的字元：字母依 Shift 與 Caps Lock 決定大小寫，其他鍵按著 Shift 是上排的符號。
char englishCharacter(char key, bool shift, bool capsLock);

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

    /// 標點配置：預設是 Mac 版的（移植的測試照 Mac 版的規格）；Windows 的輸入法用 Punctuation::Style::Windows。
    Punctuation::Style punctuationStyle() const { return punctuationStyle_; }
    void setPunctuationStyle(Punctuation::Style style) {
        punctuationStyle_ = style;
        composition.decoder.punctuationStyle = style;
    }

    bool isComposing() const { return !composer_.isEmpty() || !composition.isEmpty(); }
    /// 組字區裡有中文（正在拼的注音或已經選字的音節）。
    bool containsChinese() const;
    /// 英文模式下、組字區裡還有字時，這個鍵要交給 handle 的樣子；回傳 nullopt 表示先送出組字區，再把鍵交給程式。
    /// - 組字區有中文（從中文模式切過來，組字區留著）：英文也打在組字區。字母、數字、符號、空白變成半形字元
    ///   （KeyEvent::Literal），選字與編輯的鍵照常，按 Enter 或組字區滿了才送出（2026-11-23 使用者要求）。
    /// - 只有 Ctrl＋標點打出的全形標點：選字、編輯的鍵照常，其他鍵先送出組字區。
    std::optional<KeyEvent> englishModeKey(const KeyEvent& key, bool capsLock) const;
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

    /// Windows 版新增（Mac 版沒有）：給 UI-less 模式（程式自己畫候選字，再透過
    /// ITfCandidateListUIElementBehavior 操作）與滑鼠點選用。index 是整個候選清單裡的位置。
    /// 候選窗沒有開或 index 超出範圍時不做任何事，回傳 false。
    bool highlightCandidate(int index);
    /// 選第 index 個候選，效果同在候選窗按對應的數字鍵（會學習、會移動游標）。
    bool chooseCandidate(int index);

private:
    const LanguageModel& lm() const { return composition.decoder.lm(); }
    SessionOutput handleCharacter(char c, bool shift, bool ctrl);
    SessionOutput completeSyllable(std::uint8_t tone);
    SessionOutput insertLiteral(const std::string& text);
    bool openCandidates();
    std::optional<SessionOutput> handleCandidateKey(const KeyEvent& key);
    void choose(const Candidate& candidate);
    bool characterWouldBeHandled(char c, bool shift, bool ctrl) const;
    static bool candidateKeyWouldBeHandled(const KeyEvent& key);

    BopomofoComposer composer_;
    std::optional<CandidateList> candidateList_;
    Punctuation::Style punctuationStyle_ = Punctuation::Style::Mac;
};

}  // namespace bangbang
