// Windows 版新增的測試（Mac 版沒有對應項目）：
// - wouldHandle 與 handle 的判斷一致（TSF 會先用 OnTestKeyDown 問要不要吃這個鍵）。
// - BMP 以外的字元在 UTF-16 的位置換算。
// - 記憶檔與 Mac 版互相讀得懂。
// - 多個程式同時讀寫同一個記憶檔（鎖檔、合併、重新載入）。
// - 壞掉的模型檔在載入時就被拒絕，不會讓宿主程式當掉。
#include <gtest/gtest.h>

#include <algorithm>
#include <chrono>
#include <fstream>
#include <functional>
#include <sstream>
#include <stdexcept>

#include <nlohmann/json.hpp>

#include "InputSession.h"
#include "LanguageModel.h"
#include "SharedFile.h"
#include "ShiftTapDetector.h"
#include "TestSupport.h"
#include "UserModel.h"
#include "Utf8.h"

using namespace bangbang;
using namespace bangbang::test;
using T = KeyEvent::Type;

namespace {

std::string describe(const KeyEvent& k) {
    static const char* names[] = {"Character", "Numpad", "Space", "Enter", "Backspace", "ForwardDelete", "Escape", "Tab",
                                  "Left", "Right", "Up", "Down", "Home", "End", "PageUp", "PageDown", "Literal"};
    std::ostringstream s;
    s << names[static_cast<int>(k.type)];
    if (k.type == T::Character || k.type == T::Numpad) s << " '" << k.ch << "'" << (k.shift ? " +Shift" : "") << (k.ctrl ? " +Ctrl" : "");
    return s.str();
}

std::vector<KeyEvent> allKeys() {
    std::vector<KeyEvent> keys;
    for (const char c : std::string("`1234567890-=qwertyuiop[]\\asdfghjkl;'zxcvbnm,./")) {
        keys.push_back(ch(c, false));
        keys.push_back(ch(c, true));
        keys.push_back(KeyEvent::control(c));
        keys.push_back(KeyEvent{KeyEvent::Type::Character, c, true, true});  // Ctrl+Shift
    }
    for (const char c : std::string("0123456789.*+-/=")) keys.push_back(KeyEvent::numpad(c));
    for (const T t : {T::Space, T::Enter, T::Backspace, T::ForwardDelete, T::Escape, T::Tab, T::Left, T::Right, T::Up, T::Down,
                      T::Home, T::End, T::PageUp, T::PageDown}) {
        keys.push_back(key(t));
    }
    return keys;
}

std::string readFile(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}

void writeFile(const std::filesystem::path& path, std::string_view content) {
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    out.write(content.data(), static_cast<std::streamsize>(content.size()));
}

nlohmann::json readJson(const std::filesystem::path& path) {
    return nlohmann::json::parse(readFile(path));
}

/// 打字後在候選窗選 word，再按 Enter 送出（會學習這次的選字）。
std::string typeAndPick(InputSession& session, std::string_view keys, const std::string& word) {
    type(session, keys);
    session.handle(key(T::Down));
    if (!session.candidateList()) return "（沒有候選窗）";
    pickNumber(session, indexOf(session.candidateList()->items, word));
    return session.handle(key(T::Enter)).commit;
}

/// 打字後直接按 Enter 送出預設的結果。
std::string typeAndCommit(InputSession& session, std::string_view keys) {
    type(session, keys);
    return session.handle(key(T::Enter)).commit;
}

}  // namespace

// MARK: - wouldHandle

/// 各種狀態下的每一個按鍵，wouldHandle 的答案都必須和 handle 實際的 handled 相同（Mac 與 Windows 兩種標點配置）。
TEST(SessionConsistencyTests, WouldHandleMatchesHandleInEveryState) {
    const auto lm = makeLM();
    /// 打一個會進組字區的全形標點：Mac 配置是 Shift+,，Windows 配置是 Ctrl+,。
    const auto fullWidth = [](InputSession& s, char c) {
        s.handle(s.punctuationStyle() == Punctuation::Style::Windows ? KeyEvent::control(c) : ch(c, true));
    };
    struct Setup {
        const char* name;
        std::function<void(InputSession&)> run;
        bool expectCandidates;
    };
    const std::vector<Setup> setups = {
        {"沒有組字", [](InputSession&) {}, false},
        {"拼注音中", [](InputSession& s) { type(s, "su"); }, false},
        {"組字中、游標在最後", [](InputSession& s) { type(s, "su3cl3"); }, false},
        {"組字中、游標在最前", [](InputSession& s) { type(s, "su3cl3"); s.handle(key(T::Home)); }, false},
        {"組字中又在拼注音", [](InputSession& s) { type(s, "su3c"); }, false},
        {"只有全形標點", [&](InputSession& s) { fullWidth(s, ','); }, false},
        {"候選窗：字詞", [](InputSession& s) { type(s, "su3cl3"); s.handle(key(T::Home)); s.handle(key(T::Down)); }, true},
        {"候選窗：符號", [&](InputSession& s) { fullWidth(s, ';'); s.handle(key(T::Down)); }, true},
    };
    int checked = 0;
    for (const auto style : {Punctuation::Style::Mac, Punctuation::Style::Windows}) {
        for (const auto& setup : setups) {
            for (const auto& k : allKeys()) {
                InputSession session(lm);
                session.setPunctuationStyle(style);
                setup.run(session);
                ASSERT_EQ(session.candidateList().has_value(), setup.expectCandidates) << setup.name;
                const bool predicted = session.wouldHandle(k);
                const bool actual = session.handle(k).handled;
                EXPECT_EQ(predicted, actual) << (style == Punctuation::Style::Windows ? "Windows 配置，" : "Mac 配置，") << setup.name
                                             << "：" << describe(k);
                ++checked;
            }
        }
    }
    EXPECT_GT(checked, 3400);
}

// MARK: - 標點：Windows 配置（2026-11-23 使用者指定，比照微軟注音；對照表見 windows/README.md）

namespace {

using Strings = std::vector<std::string>;

std::unique_ptr<InputSession> windowsSession(const std::shared_ptr<const LanguageModel>& lm) {
    auto session = std::make_unique<InputSession>(lm);
    session->setPunctuationStyle(Punctuation::Style::Windows);
    return session;
}

}  // namespace

/// 中文模式下 Shift＋標點與 ` [ ] ' 打出半形符號；沒有組字時直接送出，不出現候選（使用者清單的 c～i、l～n、p、q、s）。
TEST(WindowsPunctuationTests, ShiftedAndBracketKeysAreHalfWidth) {
    const auto lm = makeLM();
    const std::vector<std::pair<KeyEvent, std::string>> cases = {
        {ch(',', true), "<"}, {ch('.', true), ">"}, {ch('1', true), "!"}, {ch('`'), "`"}, {ch('`', true), "~"},
        {ch('9', true), "("}, {ch('0', true), ")"}, {ch('['), "["}, {ch(']'), "]"}, {ch(';', true), ":"},
        {ch('\''), "'"}, {ch('\'', true), "\""}, {ch('/', true), "?"},
    };
    for (const auto& [k, text] : cases) {
        auto session = windowsSession(lm);
        const auto out = session->handle(k);
        EXPECT_TRUE(out.handled) << text;
        EXPECT_EQ(out.commit, text) << "沒有組字時直接送出";
        EXPECT_FALSE(session->isComposing()) << text;
    }
}

/// Ctrl＋標點打出全形標點，進組字區（使用者清單的 a、b、j、k、o、r）；其他 Ctrl 組合鍵交給程式。
TEST(WindowsPunctuationTests, ControlKeysTypeFullWidth) {
    const auto lm = makeLM();
    auto session = windowsSession(lm);
    for (const char c : std::string(",.;'[]")) EXPECT_TRUE(session->handle(KeyEvent::control(c)).handled) << c;
    EXPECT_EQ(session->markedText(), "，。；、【】");
    EXPECT_EQ(session->handle(key(T::Enter)).commit, "，。；、【】");
    EXPECT_FALSE(session->wouldHandle(KeyEvent::control('c')));
    EXPECT_FALSE(session->handle(KeyEvent::control('c')).handled);
    InputSession mac(lm);
    EXPECT_FALSE(mac.wouldHandle(KeyEvent::control(','))) << "Mac 配置沒有 Ctrl＋標點";
}

/// 候選順序：「，」的第二個是「、」；「【」之後是「「」「『」，「】」之後是「」」「』」。
TEST(WindowsPunctuationTests, ControlPunctuationCandidates) {
    const auto lm = makeLM();
    const auto candidates = [&](char c) {
        auto session = windowsSession(lm);
        session->handle(KeyEvent::control(c));
        EXPECT_TRUE(session->handle(key(T::Down)).handled);
        return session->candidateList() ? texts(session->candidateList()->items) : Strings{};
    };
    const auto firstThree = [&](char c) {
        const Strings all = candidates(c);
        return Strings(all.begin(), all.begin() + std::min<std::size_t>(3, all.size()));
    };
    EXPECT_EQ(firstThree(','), (Strings{"，", "、", ","}));
    EXPECT_EQ(firstThree('['), (Strings{"【", "「", "『"}));
    EXPECT_EQ(firstThree(']'), (Strings{"】", "」", "』"}));
    EXPECT_EQ(candidates('.').front(), "。");
    EXPECT_EQ(candidates(';').front(), "；");
    EXPECT_EQ(candidates('\'').front(), "、");
}

/// 組字中打半形標點：和 Shift＋字母一樣進組字區，跟著句子一起送出。
TEST(WindowsPunctuationTests, HalfWidthInsideComposition) {
    auto session = windowsSession(makeLM());
    type(*session, "su3");
    session->handle(ch(',', true));
    type(*session, "cl3");
    EXPECT_EQ(session->markedText(), "你<好");
    EXPECT_EQ(session->handle(key(T::Enter)).commit, "你<好");
}

// MARK: - 英文模式接著組字區打（2026-11-23 使用者要求：單按 Shift 不送出組字區）

/// 英文模式下打出的字元：字母看 Shift 與 Caps Lock，其他鍵按 Shift 是美式鍵盤上排的符號。
TEST(EnglishModeTests, EnglishCharacters) {
    EXPECT_EQ(englishCharacter('a', false, false), 'a');
    EXPECT_EQ(englishCharacter('a', true, false), 'A');
    EXPECT_EQ(englishCharacter('a', false, true), 'A');
    EXPECT_EQ(englishCharacter('a', true, true), 'a');
    EXPECT_EQ(englishCharacter('1', true, false), '!');
    EXPECT_EQ(englishCharacter('[', true, false), '{');
    EXPECT_EQ(englishCharacter('/', true, true), '?');
    EXPECT_EQ(englishCharacter(';', false, true), ';');
}

/// 組字區有中文時：英文、空白、數字、符號都放進組字區，按 Enter 才送出；選字、編輯的鍵照常。
TEST(EnglishModeTests, TypingContinuesInsideTheComposition) {
    auto session = windowsSession(makeLM());
    type(*session, "su3");
    ASSERT_TRUE(session->containsChinese());
    for (const KeyEvent& k : {ch('o'), ch('k'), key(T::Space), ch('1'), ch('1', true), ch('[', true)}) {
        const auto mapped = session->englishModeKey(k, false);
        ASSERT_TRUE(mapped.has_value());
        EXPECT_EQ(mapped->type, T::Literal);
        EXPECT_TRUE(session->handle(*mapped).handled);
    }
    EXPECT_EQ(session->markedText(), "你ok 1!{");
    EXPECT_EQ(session->englishModeKey(key(T::Backspace), false), key(T::Backspace));
    EXPECT_EQ(session->englishModeKey(key(T::Enter), false), key(T::Enter));
    EXPECT_EQ(session->handle(key(T::Enter)).commit, "你ok 1!{");
}

/// 組字區只有 Ctrl＋標點打出的全形標點時（沒有中文）：打英文會先送出組字區，按鍵交給程式。
TEST(EnglishModeTests, PunctuationOnlyCompositionIsCommittedFirst) {
    auto session = windowsSession(makeLM());
    session->handle(KeyEvent::control(','));
    ASSERT_TRUE(session->isComposing());
    EXPECT_FALSE(session->containsChinese());
    EXPECT_FALSE(session->englishModeKey(ch('a'), false).has_value());
    EXPECT_FALSE(session->englishModeKey(key(T::Space), false).has_value());
    EXPECT_EQ(session->englishModeKey(key(T::Down), false), key(T::Down));
}

/// 組字區滿了（Windows 版 30 格）會自動送出最前面的詞，英文模式接著打也一樣。
TEST(EnglishModeTests, FullCompositionCommitsLeadingWords) {
    auto session = windowsSession(makeLM());
    session->maxCompositionLength = 3;
    type(*session, "su3cl3");  // 你好
    const auto a = session->englishModeKey(ch('a'), false);
    ASSERT_TRUE(a.has_value());
    std::string committed = session->handle(*a).commit;
    EXPECT_EQ(committed, "") << "3 格還沒超過";
    const auto b = session->englishModeKey(ch('b'), false);
    ASSERT_TRUE(b.has_value());
    committed += session->handle(*b).commit;
    EXPECT_FALSE(committed.empty()) << "超過 3 格時送出最前面的詞";
    EXPECT_EQ(committed + session->markedText(), "你好ab");
}

// MARK: - 拼注音時的 Backspace

/// 刪掉最靠近游標的符號：ㄏㄨ 再打 ㄈ 變成 ㄈㄨ，Backspace 刪的是 ㄨ（使用者回報，2026-11-23；Mac 版刪 ㄈ）。
TEST(BackspaceTests, DeletesComponentNearestTheCursor) {
    InputSession session(makeLM());
    type(session, "cjz");  // ㄏ ㄨ，再打 ㄈ 取代 ㄏ
    EXPECT_EQ(session.markedText(), "ㄈㄨ");
    session.handle(key(T::Backspace));
    EXPECT_EQ(session.markedText(), "ㄈ");
    type(session, "u,");  // ㄧ ㄝ
    EXPECT_EQ(session.markedText(), "ㄈㄧㄝ");
    session.handle(key(T::Backspace));
    EXPECT_EQ(session.markedText(), "ㄈㄧ");
    session.handle(key(T::Backspace));
    session.handle(key(T::Backspace));
    EXPECT_FALSE(session.isComposing());
}

// MARK: - UI-less 與滑鼠點選用的候選操作

/// highlightCandidate／chooseCandidate 的結果，要和用方向鍵、數字鍵操作完全一樣。
TEST(CandidateApiTests, MatchesKeyboardOperation) {
    const auto lm = makeLM();
    // 用按鍵：游標移到最前，↓ 開候選，↓ 反白第 2 個，Enter 選它。
    InputSession byKeys(lm);
    type(byKeys, "5j/ jp6");  // ㄓㄨㄥ ㄨㄣˊ → 中文
    byKeys.handle(key(T::Home));
    byKeys.handle(key(T::Down));
    ASSERT_TRUE(byKeys.candidateList().has_value());
    byKeys.handle(key(T::Down));
    byKeys.handle(key(T::Enter));

    InputSession byApi(lm);
    type(byApi, "5j/ jp6");
    byApi.handle(key(T::Home));
    byApi.handle(key(T::Down));
    ASSERT_TRUE(byApi.highlightCandidate(1));
    EXPECT_EQ(byApi.candidateList()->highlighted, 1);
    ASSERT_TRUE(byApi.chooseCandidate(byApi.candidateList()->highlighted));
    EXPECT_FALSE(byApi.candidateList().has_value());

    EXPECT_EQ(byApi.markedText(), byKeys.markedText());
    EXPECT_EQ(byApi.markedCursor(), byKeys.markedCursor());
}

/// 候選窗沒開、或位置超出範圍時，不改變任何狀態。
TEST(CandidateApiTests, IgnoresInvalidRequests) {
    const auto lm = makeLM();
    InputSession session(lm);
    type(session, "5j/ jp6");
    EXPECT_FALSE(session.highlightCandidate(0));
    EXPECT_FALSE(session.chooseCandidate(0));
    session.handle(key(T::Down));
    ASSERT_TRUE(session.candidateList().has_value());
    const int count = static_cast<int>(session.candidateList()->items.size());
    EXPECT_FALSE(session.highlightCandidate(-1));
    EXPECT_FALSE(session.highlightCandidate(count));
    EXPECT_FALSE(session.chooseCandidate(count));
    EXPECT_EQ(session.candidateList()->highlighted, 0);
    EXPECT_EQ(session.markedText(), "中文");
}

// MARK: - 單按 Shift（中英切換）

namespace {
constexpr unsigned kShift = ShiftTapDetector::kShift;
constexpr unsigned kKeyA = 0x41;
constexpr unsigned kControl = 0x11;
}  // namespace

TEST(ShiftTapTests, ShiftAloneIsATap) {
    ShiftTapDetector d;
    d.keyDown(kShift);
    d.keyDown(kShift);  // 按住不放的自動重複
    EXPECT_TRUE(d.keyUp(kShift));
    // 放開之後回到初始狀態：同一個放開事件再送一次（訊息掛鉤和 TSF 各一次）不會再切一次。
    EXPECT_FALSE(d.keyUp(kShift));
}

TEST(ShiftTapTests, ShiftWithOtherKeyIsNotATap) {
    ShiftTapDetector d;
    d.keyDown(kShift);
    d.keyDown(kKeyA);  // Shift＋A
    d.keyUp(kKeyA);
    EXPECT_FALSE(d.keyUp(kShift));
    // Ctrl＋Shift（切換鍵盤配置的快速鍵）也不算。
    d.keyDown(kControl);
    d.keyDown(kShift);
    d.keyUp(kShift);
    d.keyDown(kShift);
    d.keyDown(kControl);
    EXPECT_FALSE(d.keyUp(kShift));
}

TEST(ShiftTapTests, MouseClickWhileHoldingShiftIsNotATap) {
    ShiftTapDetector d;
    d.keyDown(kShift);
    d.mouseDown();  // Shift＋點選來選取文字
    EXPECT_FALSE(d.keyUp(kShift));
    // 沒按 Shift 時點滑鼠，不影響下一次單按。
    d.mouseDown();
    d.keyDown(kShift);
    EXPECT_TRUE(d.keyUp(kShift));
}

TEST(ShiftTapTests, LeftAndRightShiftCount) {
    ShiftTapDetector d;
    d.keyDown(ShiftTapDetector::kLeftShift);
    EXPECT_TRUE(d.keyUp(ShiftTapDetector::kLeftShift));
    d.keyDown(kShift);  // 同一次按下可能分別以 VK_SHIFT 與 VK_RSHIFT 送來
    d.keyDown(ShiftTapDetector::kRightShift);
    EXPECT_TRUE(d.keyUp(ShiftTapDetector::kRightShift));
    EXPECT_FALSE(d.keyUp(kShift));
}

TEST(ShiftTapTests, ResetForgetsPendingShift) {
    ShiftTapDetector d;
    d.keyDown(kShift);
    d.reset();  // 例如焦點換到別的程式
    EXPECT_FALSE(d.keyUp(kShift));
}

// MARK: - BMP 以外的字元

/// 「𨑨」在 UTF-16 佔 2 個單位：組字區仍是一格一字，外殼用 utf16Offset 換算游標位置。
TEST(NonBmpTests, MarkedTextAndUtf16Offsets) {
    const std::vector<std::string> words = {"<s>", "𨑨", "迌", "𨑨迌", "好"};
    std::map<ReadingKey, std::vector<LexiconEntry>> lexicon;
    lexicon[ReadingKey::of(*Syllable::parseReading("ㄓˋ"))] = {{1, 0}};
    lexicon[ReadingKey::of(*Syllable::parseReading("ㄊㄨˋ"))] = {{2, 0}};
    lexicon[ReadingKey::of(*Syllable::parseReading("ㄓˋ ㄊㄨˋ"))] = {{3, 0}};
    lexicon[ReadingKey::of(*Syllable::parseReading("ㄏㄠˇ"))] = {{4, 0}};
    const auto lm = LanguageModel::build(words, {-30, -5, -5, -3, -5}, std::vector<float>(5, std::log(0.1f)),
                                         std::vector<std::uint32_t>(6, 0), {}, {}, lexicon);
    InputSession session(lm);
    type(session, "54wj4");  // ㄓˋ ㄊㄨˋ
    EXPECT_EQ(session.markedText(), "𨑨迌");
    EXPECT_EQ(session.markedCursor(), 2);
    EXPECT_EQ(utf8::utf16Offset(session.markedText(), session.markedCursor()), 3u);
    session.handle(key(T::Left));
    EXPECT_EQ(utf8::utf16Offset(session.markedText(), session.markedCursor()), 2u);
    type(session, "cl3");  // 在「𨑨」後面插入「好」
    EXPECT_EQ(session.markedText(), "𨑨好迌");
    EXPECT_EQ(session.composition.textBeforeCursor(), "𨑨好");
    EXPECT_EQ(utf8::utf16Offset(session.markedText(), session.markedCursor()), 3u);
    EXPECT_EQ(utf8::toWide(session.markedText()).size(), 4u);
    EXPECT_EQ(session.handle(key(T::Enter)).commit, "𨑨好迌");
}

// MARK: - 記憶檔與 Mac 版互通

/// Mac 版（Swift JSONEncoder，prettyPrinted＋sortedKeys）寫出的記憶檔可以直接讀。
TEST(UserModelFileTests, ReadsMacFormatFile) {
    TempDir dir("bangbang-mac-memory");
    const auto path = dir.path / "user-model.json";
    writeFile(path, R"({
  "contextual" : {
    "<s>\t妳\tㄋㄧˇ" : {
      "count" : 2,
      "lastUsed" : 1763100000.25
    },
    "a\/b\tㄅ\tㄅㄚ" : {
      "count" : 1,
      "lastUsed" : 1763100000
    }
  },
  "unigram" : {
    "妳\tㄋㄧˇ" : {
      "count" : 2,
      "lastUsed" : 1763100000.25
    }
  },
  "version" : 2
}
)");
    const auto lm = makeLM();
    UserModel memory(lm, path);
    InputSession session(lm, &memory);
    type(session, "su3");
    EXPECT_EQ(session.handle(key(T::Enter)).commit, "妳");
}

/// 寫出的記憶檔是 Mac 版讀得懂的格式：第 2 版、鍵是文字、讀音以空白分隔、一聲不加符號。
TEST(UserModelFileTests, WritesFormatMacCanRead) {
    TempDir dir("bangbang-memory-out");
    const auto path = dir.path / "user-model.json";
    const auto lm = makeLM();
    {
        UserModel memory(lm, path);
        InputSession session(lm, &memory);
        type(session, "su3");  // 句首的 ㄋㄧˇ 選「妳」
        session.handle(key(T::Down));
        pickNumber(session, indexOf(session.candidateList()->items, "妳"));
        EXPECT_EQ(session.handle(key(T::Enter)).commit, "妳");
        type(session, "5j/ jp6");  // 中文 → 把「文」改成「聞」
        session.handle(key(T::Down));
        pickNumber(session, indexOf(session.candidateList()->items, "聞"));
        EXPECT_EQ(session.handle(key(T::Enter)).commit, "中聞");
    }

    const std::string text = readFile(path);
    ASSERT_FALSE(text.empty());
    EXPECT_EQ(text.front(), '{') << "不能有 BOM";
    const auto root = nlohmann::json::parse(text);
    EXPECT_EQ(root.at("version"), 2);
    const auto& contextual = root.at("contextual");
    ASSERT_TRUE(contextual.contains("<s>\t妳\tㄋㄧˇ")) << contextual.dump();
    ASSERT_TRUE(contextual.contains("中\t聞\tㄨㄣˊ")) << contextual.dump();
    EXPECT_EQ(contextual.at("中\t聞\tㄨㄣˊ").at("count"), 1);
    EXPECT_TRUE(contextual.at("中\t聞\tㄨㄣˊ").at("lastUsed").is_number());
    EXPECT_TRUE(root.at("unigram").contains("妳\tㄋㄧˇ"));
    EXPECT_TRUE(root.at("unigram").contains("聞\tㄨㄣˊ"));

    // 重新載入後記憶仍在
    UserModel reloaded(lm, path);
    InputSession again(lm, &reloaded);
    type(again, "su3");
    EXPECT_EQ(again.handle(key(T::Enter)).commit, "妳");
}

TEST(UserModelFileTests, CorruptFileIsIgnored) {
    TempDir dir("bangbang-memory-bad");
    const auto path = dir.path / "user-model.json";
    writeFile(path, "{\"contextual\": {\"<s>\t");
    const auto lm = makeLM();
    UserModel memory(lm, path);
    InputSession session(lm, &memory);
    type(session, "su3");
    EXPECT_EQ(session.handle(key(T::Enter)).commit, "你");
}

/// 學習時，壞掉的記憶檔改名成 .corrupt 保留（可能是手動改到一半），再寫入新的記憶檔。
TEST(UserModelFileTests, CorruptFileIsSetAsideWhenLearning) {
    TempDir dir("bangbang-memory-bad");
    const auto path = dir.path / "user-model.json";
    writeFile(path, "{\"contextual\": {\"<s>\t");
    const auto lm = makeLM();
    UserModel memory(lm, path);
    InputSession session(lm, &memory);
    EXPECT_EQ(typeAndPick(session, "su3", "妳"), "妳");
    EXPECT_EQ(readFile(dir.path / "user-model.json.corrupt"), "{\"contextual\": {\"<s>\t");
    EXPECT_TRUE(readJson(path).at("contextual").contains("<s>\t妳\tㄋㄧˇ"));
}

// MARK: - 多個程式共用記憶檔（每個 UserModel 代表一個程式）

/// A 程式學到的，B 程式下次組字就用得到；B 改選之後，A 也跟著改。
TEST(SharedUserModelTests, LearningReachesOtherPrograms) {
    TempDir dir("bangbang-shared");
    const auto path = dir.path / "user-model.json";
    const auto lm = makeLM();
    UserModel a(lm, path), b(lm, path);
    InputSession sa(lm, &a), sb(lm, &b);
    EXPECT_EQ(typeAndPick(sa, "su3", "妳"), "妳");
    EXPECT_TRUE(b.refresh());
    EXPECT_EQ(typeAndCommit(sb, "su3"), "妳");
    EXPECT_EQ(typeAndPick(sb, "su3", "你"), "你");
    EXPECT_TRUE(a.refresh());
    EXPECT_EQ(typeAndCommit(sa, "su3"), "你");
    EXPECT_FALSE(a.refresh()) << "檔案沒變時不用重新載入";
    EXPECT_FALSE(b.refresh()) << "自己寫的檔案不用重新載入";
}

/// 兩個程式各自學習、中間沒有重新載入：寫入時會先合併，誰的記憶都不會被蓋掉。
TEST(SharedUserModelTests, SimultaneousLearningIsMerged) {
    TempDir dir("bangbang-shared");
    const auto path = dir.path / "user-model.json";
    const auto lm = makeLM();
    UserModel a(lm, path), b(lm, path);
    InputSession sa(lm, &a), sb(lm, &b);
    EXPECT_EQ(typeAndPick(sa, "su3", "妳"), "妳");
    EXPECT_EQ(typeAndPick(sb, "m,4", "越"), "越");  // 句首的 ㄩㄝˋ 預設是「月」；B 還不知道 A 學了什麼
    const auto root = readJson(path);
    EXPECT_TRUE(root.at("contextual").contains("<s>\t妳\tㄋㄧˇ")) << root.dump();
    EXPECT_TRUE(root.at("contextual").contains("<s>\t越\tㄩㄝˋ")) << root.dump();
    EXPECT_EQ(typeAndCommit(sb, "su3"), "妳") << "B 寫入時已經合併了 A 的記憶";
    EXPECT_TRUE(a.refresh());
    EXPECT_EQ(typeAndCommit(sa, "m,4"), "越");
}

/// 兩個程式從同一個狀態各選一次同一個詞：檔案裡是 2 次，不是 1 次。
TEST(SharedUserModelTests, CountsAddUpAcrossPrograms) {
    TempDir dir("bangbang-shared");
    const auto path = dir.path / "user-model.json";
    const auto lm = makeLM();
    UserModel a(lm, path), b(lm, path);
    InputSession sa(lm, &a), sb(lm, &b);
    typeAndPick(sa, "su3", "妳");
    typeAndPick(sb, "su3", "妳");
    const auto root = readJson(path);
    EXPECT_EQ(root.at("unigram").at("妳\tㄋㄧˇ").at("count"), 2);
    EXPECT_EQ(root.at("contextual").at("<s>\t妳\tㄋㄧˇ").at("count"), 2);
}

/// 一個程式清除記憶、或使用者刪掉記憶檔：其他程式下次組字時也跟著忘掉。
TEST(SharedUserModelTests, ForgettingReachesOtherPrograms) {
    TempDir dir("bangbang-shared");
    const auto path = dir.path / "user-model.json";
    const auto lm = makeLM();
    UserModel a(lm, path), b(lm, path);
    InputSession sa(lm, &a), sb(lm, &b);
    typeAndPick(sa, "su3", "妳");
    EXPECT_TRUE(b.refresh());
    EXPECT_EQ(typeAndCommit(sb, "su3"), "妳");
    a.reset();
    EXPECT_TRUE(b.refresh());
    EXPECT_EQ(typeAndCommit(sb, "su3"), "你");
    typeAndPick(sb, "su3", "妳");
    EXPECT_TRUE(a.refresh());
    EXPECT_EQ(typeAndCommit(sa, "su3"), "妳");
    std::filesystem::remove(path);
    EXPECT_TRUE(a.refresh());
    EXPECT_EQ(typeAndCommit(sa, "su3"), "你");
}

/// 別的程式拿著鎖不放：學習不會卡住，變更先留在記憶體，下次學習時一起寫進去。
TEST(SharedUserModelTests, BusyLockDoesNotBlockOrLoseLearning) {
    using namespace std::chrono_literals;
    TempDir dir("bangbang-shared");
    const auto path = dir.path / "user-model.json";
    const auto lm = makeLM();
    UserModel a(lm, path);
    InputSession sa(lm, &a);
    {
        const auto held = FileLock::acquire(dir.path / "user-model.json.lock", 0ms);
        ASSERT_TRUE(held);
        const auto start = std::chrono::steady_clock::now();
        EXPECT_EQ(typeAndPick(sa, "su3", "妳"), "妳");
        EXPECT_LT(std::chrono::steady_clock::now() - start, 2s);
        EXPECT_EQ(a.unsavedChanges(), 1u);
        EXPECT_FALSE(std::filesystem::exists(path));
        EXPECT_EQ(typeAndCommit(sa, "su3"), "妳") << "記憶體裡已經生效";
    }
    EXPECT_EQ(typeAndPick(sa, "5j/ jp6", "聞"), "中聞");
    EXPECT_EQ(a.unsavedChanges(), 0u);
    const auto root = readJson(path);
    EXPECT_TRUE(root.at("contextual").contains("<s>\t妳\tㄋㄧˇ"));
    EXPECT_TRUE(root.at("contextual").contains("中\t聞\tㄨㄣˊ"));
}

/// 還沒寫進去的變更，重新載入別的程式的記憶之後仍然保留，之後也會寫進去。
TEST(SharedUserModelTests, UnsavedChangesSurviveRefresh) {
    using namespace std::chrono_literals;
    TempDir dir("bangbang-shared");
    const auto path = dir.path / "user-model.json";
    const auto lm = makeLM();
    UserModel a(lm, path), b(lm, path);
    InputSession sa(lm, &a), sb(lm, &b);
    {
        const auto held = FileLock::acquire(dir.path / "user-model.json.lock", 0ms);
        ASSERT_TRUE(held);
        typeAndPick(sa, "su3", "妳");
        ASSERT_EQ(a.unsavedChanges(), 1u);
    }
    typeAndPick(sb, "m,4", "越");
    EXPECT_TRUE(a.refresh());
    EXPECT_EQ(typeAndCommit(sa, "m,4"), "越");
    EXPECT_EQ(typeAndCommit(sa, "su3"), "妳");
    EXPECT_TRUE(a.flush());
    EXPECT_TRUE(b.refresh());
    EXPECT_EQ(typeAndCommit(sb, "su3"), "妳");
}

/// 寫到一半當掉的程式留下的暫存檔，下次寫入時清掉。
TEST(SharedUserModelTests, StaleTempFilesAreRemoved) {
    TempDir dir("bangbang-shared");
    const auto path = dir.path / "user-model.json";
    writeFile(dir.path / "user-model.json.999-1.tmp", "half written");
    const auto lm = makeLM();
    UserModel a(lm, path);
    InputSession sa(lm, &a);
    typeAndPick(sa, "su3", "妳");
    std::vector<std::string> names;
    for (const auto& entry : std::filesystem::directory_iterator(dir.path)) names.push_back(entry.path().filename().string());
    std::sort(names.begin(), names.end());
    EXPECT_EQ(names, (std::vector<std::string>{"user-model.json", "user-model.json.lock"}));
}

namespace {

constexpr const wchar_t* kChildFileVariable = L"BANGBANG_TEST_SHARED_FILE";
constexpr const wchar_t* kChildIndexVariable = L"BANGBANG_TEST_CHILD_INDEX";
constexpr int kChildPrograms = 4;
constexpr int kChildRounds = 2;
const char* const kChildPrevWords[kChildPrograms] = {"你", "好", "中", "更"};

/// 迷你模型裡每個讀音的每個詞。
std::vector<std::pair<std::string, std::string>> everyPick() {
    return {{"你", "ㄋㄧˇ"}, {"妳", "ㄋㄧˇ"}, {"好", "ㄏㄠˇ"}, {"你好", "ㄋㄧˇ ㄏㄠˇ"}, {"越", "ㄩㄝˋ"}, {"月", "ㄩㄝˋ"},
            {"更新", "ㄍㄥ ㄒㄧㄣ"}, {"難", "ㄋㄢˊ"}, {"南", "ㄋㄢˊ"}, {"用", "ㄩㄥˋ"}, {"越南", "ㄩㄝˋ ㄋㄢˊ"}, {"更", "ㄍㄥ"},
            {"新", "ㄒㄧㄣ"}, {"中", "ㄓㄨㄥ"}, {"鐘", "ㄓㄨㄥ"}, {"文", "ㄨㄣˊ"}, {"聞", "ㄨㄣˊ"}, {"中文", "ㄓㄨㄥ ㄨㄣˊ"}};
}

WordId wordId(const LanguageModel& lm, std::string_view text) {
    const auto& words = lm.words();
    for (std::size_t i = 0; i < words.size(); ++i) {
        if (words[i] == text) return static_cast<WordId>(i);
    }
    throw std::runtime_error("迷你模型裡沒有這個詞");
}

std::wstring environment(const wchar_t* name) {
    wchar_t buffer[MAX_PATH] = {};
    const DWORD n = GetEnvironmentVariableW(name, buffer, MAX_PATH);
    return n > 0 && n < MAX_PATH ? std::wstring(buffer, n) : std::wstring();
}

}  // namespace

/// ParallelProgramsKeepEveryPick 啟動的子程序：用自己的「前一個詞」，把每個詞各學 kChildRounds 次。
TEST(SharedUserModelChild, DISABLED_LearnEveryPick) {
    const std::wstring file = environment(kChildFileVariable);
    const std::wstring index = environment(kChildIndexVariable);
    if (file.empty() || index.empty()) GTEST_SKIP() << "只在 ParallelProgramsKeepEveryPick 裡執行";
    const auto lm = makeLM();
    UserModel memory(lm, std::filesystem::path(file));
    const WordId prev = wordId(*lm, kChildPrevWords[std::stoi(index)]);
    for (int round = 0; round < kChildRounds; ++round) {
        for (const auto& [word, reading] : everyPick()) {
            memory.learn(prev, wordId(*lm, word), ReadingKey::of(*Syllable::parseReading(reading)));
        }
    }
    for (int attempt = 0; attempt < 50 && !memory.flush(); ++attempt) Sleep(20);
    EXPECT_EQ(memory.unsavedChanges(), 0u);
}

/// 四個程式同時大量學習：每一筆都留在記憶檔裡，次數也正確累加，檔案沒有壞掉。
TEST(SharedUserModelTests, ParallelProgramsKeepEveryPick) {
    TempDir dir("bangbang-parallel");
    const auto path = dir.path / "user-model.json";
    wchar_t exe[MAX_PATH] = {};
    ASSERT_GT(GetModuleFileNameW(nullptr, exe, MAX_PATH), 0u);
    std::vector<PROCESS_INFORMATION> children;
    SetEnvironmentVariableW(kChildFileVariable, path.c_str());
    for (int i = 0; i < kChildPrograms; ++i) {
        SetEnvironmentVariableW(kChildIndexVariable, std::to_wstring(i).c_str());
        std::wstring command = L"\"" + std::wstring(exe) +
                               L"\" --gtest_also_run_disabled_tests --gtest_filter=SharedUserModelChild.DISABLED_LearnEveryPick";
        STARTUPINFOW startup{};
        startup.cb = sizeof(startup);
        PROCESS_INFORMATION process{};
        if (CreateProcessW(nullptr, command.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &startup, &process)) {
            children.push_back(process);
        } else {
            ADD_FAILURE() << "無法啟動子程序，錯誤 " << GetLastError();
        }
    }
    SetEnvironmentVariableW(kChildFileVariable, nullptr);
    SetEnvironmentVariableW(kChildIndexVariable, nullptr);
    for (const auto& process : children) {
        EXPECT_EQ(WaitForSingleObject(process.hProcess, 120000), WAIT_OBJECT_0);
        DWORD code = 1;
        GetExitCodeProcess(process.hProcess, &code);
        EXPECT_EQ(code, 0u) << "子程序的測試失敗";
        CloseHandle(process.hThread);
        CloseHandle(process.hProcess);
    }
    ASSERT_EQ(children.size(), static_cast<std::size_t>(kChildPrograms));

    const auto root = readJson(path);
    const auto picks = everyPick();
    const auto& contextual = root.at("contextual");
    EXPECT_EQ(contextual.size(), kChildPrograms * picks.size());
    for (const char* prev : kChildPrevWords) {
        for (const auto& [word, reading] : picks) {
            const std::string key = std::string(prev) + "\t" + word + "\t" + reading;
            ASSERT_TRUE(contextual.contains(key)) << key;
            EXPECT_EQ(contextual.at(key).at("count"), kChildRounds) << key;
        }
    }
    for (const auto& [word, reading] : picks) {
        EXPECT_EQ(root.at("unigram").at(word + "\t" + reading).at("count"), std::min(kChildPrograms * kChildRounds, UserModel::kMaxCount));
    }
}

// MARK: - 共用檔案的基本操作

/// 別的程式正在讀舊檔時也能取代：讀的人讀完舊的內容，之後再開就是新的。
TEST(SharedFileTests, ReplaceWhileAnotherProgramReads) {
    TempDir dir("bangbang-file");
    const auto path = dir.path / "data.json";
    ASSERT_TRUE(replaceSharedFile(path, "old content"));
    const HANDLE reader = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
                                      OPEN_EXISTING, 0, nullptr);
    ASSERT_NE(reader, INVALID_HANDLE_VALUE);
    EXPECT_TRUE(replaceSharedFile(path, "new content"));
    char buffer[32] = {};
    DWORD n = 0;
    EXPECT_TRUE(ReadFile(reader, buffer, sizeof(buffer), &n, nullptr));
    EXPECT_EQ(std::string(buffer, n), "old content");
    CloseHandle(reader);
    EXPECT_EQ(readFile(path), "new content");
}

/// 每次取代都是新的版本，就算內容、大小完全一樣（修改時間的精確度不夠也不怕）。
TEST(SharedFileTests, EveryReplaceIsANewVersion) {
    TempDir dir("bangbang-file");
    const auto path = dir.path / "data.json";
    EXPECT_EQ(fileStamp(path), FileStamp{});
    const auto first = replaceSharedFile(path, "same");
    const auto second = replaceSharedFile(path, "same");
    ASSERT_TRUE(first && second);
    EXPECT_TRUE(first->exists);
    EXPECT_NE(*first, *second);
    EXPECT_EQ(fileStamp(path), second);
    std::string data;
    FileStamp stamp;
    EXPECT_EQ(readSharedFile(path, data, stamp), ReadResult::Ok);
    EXPECT_EQ(data, "same");
    EXPECT_EQ(stamp, *second);
    EXPECT_EQ(readSharedFile(dir.path / "missing.json", data, stamp), ReadResult::Missing);
}

TEST(SharedFileTests, LockIsExclusive) {
    using namespace std::chrono_literals;
    TempDir dir("bangbang-file");
    const auto lockPath = dir.path / "data.lock";
    {
        const auto first = FileLock::acquire(lockPath, 0ms);
        ASSERT_TRUE(first);
        EXPECT_FALSE(FileLock::acquire(lockPath, 30ms));
    }
    EXPECT_TRUE(FileLock::acquire(lockPath, 0ms)) << "解構時解鎖";
}

TEST(SharedFileTests, FolderIsCreatedAndChecked) {
    TempDir dir("bangbang-file");
    const auto folder = dir.path / "BangbangZhuyin";
    EXPECT_EQ(checkSharedFolder(folder, false), FolderState::Missing);
    EXPECT_EQ(checkSharedFolder(folder, true), FolderState::Ready);
    EXPECT_TRUE(std::filesystem::is_directory(folder));
    EXPECT_EQ(checkSharedFolder(folder, false), FolderState::Ready);
    writeFile(dir.path / "not-a-folder", "x");
    EXPECT_EQ(checkSharedFolder(dir.path / "not-a-folder", true), FolderState::Failed);
}

TEST(UserModelFileTests, MissingFileStartsEmpty) {
    const auto lm = makeLM();
    UserModel memory(lm, std::filesystem::temp_directory_path() / "bangbang-does-not-exist" / "user-model.json");
    InputSession session(lm, &memory);
    type(session, "su3");
    EXPECT_EQ(session.handle(key(T::Enter)).commit, "你");
}

// MARK: - 模型檔

TEST(ModelLoadTests, RejectsCorruptFiles) {
    TempFile file("bangbang-bad", ".lm");
    writeFile(file.path, "not a model file at all, just some text that is long enough");
    EXPECT_THROW(LanguageModel::load(file.path), ModelError);

    // 正確的檔頭但內容被截斷
    TempFile good("bangbang-good", ".lm");
    makeLM()->write(good.path);
    const std::string bytes = readFile(good.path);
    writeFile(file.path, std::string_view(bytes).substr(0, bytes.size() / 2));
    EXPECT_THROW(LanguageModel::load(file.path), ModelError);

    writeFile(file.path, "");
    EXPECT_THROW(LanguageModel::load(file.path), ModelError);
    EXPECT_THROW(LanguageModel::load(file.path.parent_path() / "bangbang-missing.lm"), ModelError);
}
