// 從 Tests/BangbangEngineTests/BangbangEngineTests.swift 逐一移植的 37 個單元測試。
// 測試名稱、步驟與斷言都和 Mac 版相同，是行為規格的可執行版本。
#include <gtest/gtest.h>

#include <fstream>

#include "Composition.h"
#include "Decoder.h"
#include "InputSession.h"
#include "LanguageModel.h"
#include "TestSupport.h"
#include "UserModel.h"

using namespace bangbang;
using namespace bangbang::test;
using T = KeyEvent::Type;
using Strings = std::vector<std::string>;

// MARK: - BopomofoTests

TEST(BopomofoTests, testParseAndDescribe) {
    EXPECT_EQ(Syllable::parse("ㄉㄜ˙")->description(), "ㄉㄜ˙");
    EXPECT_EQ(Syllable::parse("˙ㄉㄜ"), Syllable::parse("ㄉㄜ˙"));
    EXPECT_EQ(Syllable::parse("ㄍㄥ")->tone, 1);
    EXPECT_FALSE(Syllable::parse("ㄅㄆ").has_value());
    const Syllable s = *Syllable::parse("ㄒㄩㄢˊ");
    EXPECT_EQ(Syllable::fromCode(s.code()), s);
}

TEST(BopomofoTests, testDaChenLayout) {
    BopomofoComposer composer;
    for (const char k : std::string("su")) composer.receive(*DaChenLayout::component(k));
    EXPECT_EQ(composer.completed(3), Syllable::parse("ㄋㄧˇ"));
    EXPECT_EQ(DaChenLayout::component(' '), BopomofoComponent::tone(1));
    EXPECT_EQ(DaChenLayout::component('-'), BopomofoComponent::rime(13));  // ㄦ
}

// Windows 版改過（2026-11-23 使用者要求）：Backspace 刪掉最靠近游標的符號，不是最後輸入的。
// Mac 版的這個測試是「刪掉最後輸入的 ㄇ → ㄚ」。
TEST(BopomofoTests, testComposerReplacesSlotAndBackspaces) {
    BopomofoComposer composer;
    composer.receive(BopomofoComponent::consonant(1));  // ㄅ
    composer.receive(BopomofoComponent::rime(1));       // ㄚ
    composer.receive(BopomofoComponent::consonant(3));  // ㄇ 取代 ㄅ
    EXPECT_EQ(composer.display(), "ㄇㄚ");
    composer.backspace();  // 刪掉最靠近游標的 ㄚ
    EXPECT_EQ(composer.display(), "ㄇ");
    composer.backspace();
    EXPECT_TRUE(composer.isEmpty());
}

// MARK: - InputSessionTests

TEST(InputSessionTests, testTypingAndEnterCommits) {
    InputSession session(makeLM());
    type(session, "su3cl3");
    EXPECT_EQ(session.markedText(), "你好");
    EXPECT_EQ(session.handle(key(T::Enter)).commit, "你好");
    EXPECT_FALSE(session.isComposing());
}

TEST(InputSessionTests, testSpaceWhileSpellingIsFirstTone) {
    InputSession session(makeLM());
    const auto outputs = type(session, "e/ vup ");
    EXPECT_TRUE(std::all_of(outputs.begin(), outputs.end(), [](const SessionOutput& o) { return o.commit.empty(); }));
    EXPECT_EQ(session.markedText(), "更新");
}

/// 需求 7c：組字中、沒有待拼注音時按空白 → 在組字區加半形空白，按 Enter 才送出。
TEST(InputSessionTests, testSpaceAfterCompositionStaysInComposition) {
    InputSession session(makeLM());
    type(session, "su3cl3");
    const auto out = session.handle(key(T::Space));
    EXPECT_EQ(out.commit, "");
    EXPECT_TRUE(out.handled);
    EXPECT_EQ(session.markedText(), "你好 ");
    type(session, "su3");
    EXPECT_EQ(session.markedText(), "你好 你");
    EXPECT_EQ(session.handle(key(T::Enter)).commit, "你好 你");
    EXPECT_FALSE(session.isComposing());
}

TEST(InputSessionTests, testSpaceWithNothingComposingPassesThrough) {
    InputSession session(makeLM());
    EXPECT_FALSE(session.handle(key(T::Space)).handled);
}

/// 需求 7a：Shift + 字母 → 半形英文。
TEST(InputSessionTests, testShiftLetterIsHalfWidth) {
    InputSession session(makeLM());
    EXPECT_EQ(session.handle(ch('a', true)).commit, "A");
    type(session, "su3");
    session.handle(ch('b', true));
    EXPECT_EQ(session.markedText(), "你B");
}

/// 全形標點不論何時都進入組字區，按 Enter 才送出（同微軟新注音）。
TEST(InputSessionTests, testFullWidthPunctuationEntersComposition) {
    InputSession session(makeLM());
    const auto out = session.handle(ch(',', true));
    EXPECT_TRUE(out.handled);
    EXPECT_EQ(out.commit, "");
    EXPECT_EQ(session.markedText(), "，");
    session.handle(ch('.', true));
    session.handle(ch('\'', false));
    session.handle(ch('[', false));
    EXPECT_EQ(session.handle(key(T::Enter)).commit, "，。、「");
    EXPECT_FALSE(session.isComposing());
}

/// 符號按 ↓：列出全形／半形與同一個鍵上的另一個符號，例如「：」也能換成「；」。
TEST(InputSessionTests, testSymbolCandidates) {
    InputSession session(makeLM());
    session.handle(ch(';', true));
    EXPECT_TRUE(session.handle(key(T::Down)).handled);
    EXPECT_EQ(texts(session.candidateList()->items), (Strings{"：", ":", "；", ";"}));
    session.handle(ch('3'));
    EXPECT_EQ(session.markedText(), "；");
    EXPECT_EQ(session.handle(key(T::Enter)).commit, "；");
}

TEST(InputSessionTests, testSymbolCandidatesInsideSentence) {
    InputSession session(makeLM());
    type(session, "su3");
    session.handle(ch(',', true));
    type(session, "cl3");
    EXPECT_EQ(session.markedText(), "你，好");
    session.handle(key(T::Left));
    session.handle(key(T::Left));  // 游標移到「，」前面
    session.handle(key(T::Down));
    const auto items = session.candidateList()->items;
    EXPECT_EQ(items.front().text, "，");
    EXPECT_TRUE(std::any_of(items.begin(), items.end(), [](const Candidate& c) { return c.text == ","; }));
    session.handle(ch('2'));
    EXPECT_EQ(session.markedText(), "你,好");
    EXPECT_EQ(session.composition.cursor(), 2) << "選完游標移到符號後面";
}

/// 把『』的前半改成「，後半自動變成」；改後半也會帶動前半。
TEST(InputSessionTests, testBracketPairChangesTogether) {
    InputSession session(makeLM());
    session.handle(ch('[', true));  // 『
    type(session, "su3");
    session.handle(ch(']', true));  // 』
    EXPECT_EQ(session.markedText(), "『你』");
    session.handle(key(T::Home));
    session.handle(key(T::Down));
    pickNumber(session, indexOf(session.candidateList()->items, "「"));
    EXPECT_EQ(session.markedText(), "「你」");
    session.handle(key(T::End));
    session.handle(key(T::Down));
    pickNumber(session, indexOf(session.candidateList()->items, "】"));
    EXPECT_EQ(session.markedText(), "【你】");
}

/// 巢狀括號依層數配對：改最外層的前半，只改到最外層的後半。
TEST(InputSessionTests, testNestedBracketsMatchByDepth) {
    const auto lm = makeLM();
    Composition composition{Decoder(lm)};
    for (const char* t : {"『", "『", "」", "』", "』"}) composition.insert(Token::literalToken(t));
    composition.replaceLiteral(0, "「");
    EXPECT_EQ(composition.text(), "「『」』」");
    // 換成不是括號的符號時，另一半不動
    composition.replaceLiteral(1, "9");
    EXPECT_EQ(composition.text(), "「9」』」");
}

TEST(InputSessionTests, testSpaceCandidatesOfferFullWidthSpace) {
    InputSession session(makeLM());
    type(session, "su3");
    session.handle(key(T::Space));
    session.handle(key(T::Down));
    const auto items = session.candidateList()->items;
    EXPECT_EQ(texts(items), (Strings{" ", "　"}));
    Strings display;
    for (const auto& c : items) display.push_back(c.displayText());
    EXPECT_EQ(display, (Strings{"半形空白", "全形空白"}));
}

TEST(InputSessionTests, testHalfWidthSymbolCommitsDirectly) {
    InputSession session(makeLM());
    EXPECT_EQ(session.handle(ch('2', true)).commit, "@");
    EXPECT_FALSE(session.isComposing());
}

/// 目前顯示的詞排在候選字第一筆（同微軟新注音），即使有更長的詞。
TEST(InputSessionTests, testCurrentSelectionIsFirstCandidate) {
    InputSession session(makeLM());
    type(session, "m,4s06");
    EXPECT_EQ(session.markedText(), "越南");
    session.handle(key(T::Down));
    EXPECT_EQ(texts(session.candidateList()->items), (Strings{"南", "難"})) << "目前的「南」排第一";
    session.handle(ch('2'));  // 最後一字改選「難」
    EXPECT_EQ(session.markedText(), "越難");
    session.handle(key(T::Home));
    session.handle(key(T::Down));
    const auto list = *session.candidateList();
    EXPECT_EQ(list.items.front().text, "越");
    EXPECT_EQ(list.highlighted, 0);
    EXPECT_TRUE(std::any_of(list.items.begin(), list.items.end(), [](const Candidate& c) { return c.text == "越南"; }));
}

/// 需求 7b：游標在字「前面」按 ↓，列出從游標開始的詞。
TEST(InputSessionTests, testCandidatesStartAtCursorLikeMicrosoft) {
    InputSession session(makeLM());
    type(session, "su3cl3");
    session.handle(key(T::Home));
    EXPECT_TRUE(session.handle(key(T::Down)).handled);
    const auto items = session.candidateList()->items;
    EXPECT_EQ(texts(items), (Strings{"你好", "你", "妳"}));
    EXPECT_TRUE(std::all_of(items.begin(), items.end(), [](const Candidate& c) { return c.start == 0; }));
    // 選「妳」（第 3 個）
    session.handle(ch('3'));
    EXPECT_FALSE(session.candidateList().has_value());
    EXPECT_EQ(session.markedText(), "妳好");
    EXPECT_EQ(session.composition.cursor(), 1) << "選完游標移到該詞後面";
}

/// 游標在最後時只列出最後一個字。
TEST(InputSessionTests, testCandidatesAtEndListOnlyLastCharacter) {
    InputSession session(makeLM());
    type(session, "su3cl3");
    session.handle(key(T::Down));
    EXPECT_EQ(texts(session.candidateList()->items), (Strings{"好"}));
    session.handle(key(T::Escape));
    EXPECT_FALSE(session.candidateList().has_value());
    EXPECT_EQ(session.markedText(), "你好");
}

/// 改了「文」之後，前面的「中」不能跟著變（沒有固定的話會變成「鐘聞」）。
TEST(InputSessionTests, testSelectionKeepsPrecedingText) {
    const auto lm = makeLM({{"鐘", "聞", -1.0f}});
    InputSession session(lm);
    type(session, "5j/ jp6");
    EXPECT_EQ(session.markedText(), "中文");
    session.handle(key(T::Down));
    const auto items = session.candidateList()->items;
    EXPECT_EQ(texts(items), (Strings{"文", "聞"}));
    EXPECT_EQ(items[session.candidateList()->highlighted].text, "文") << "預設反白目前的字";
    session.handle(ch('2'));
    EXPECT_EQ(session.markedText(), "中聞");
    // 沒有固定前面的字時，同樣的選擇會讓整句變成「鐘聞」
    const auto free = Decoder(lm).decode(session.composition.tokens(), {Pin{1, 1, 17}});
    EXPECT_EQ(joined(free), "鐘聞");
}

TEST(InputSessionTests, testCursorMovementAndInsertion) {
    InputSession session(makeLM());
    type(session, "su3cl3");
    session.handle(key(T::Left));
    EXPECT_EQ(session.markedCursor(), 1);
    type(session, "cl3");
    EXPECT_EQ(session.composition.count(), 3);
    EXPECT_EQ(session.markedCursor(), 2);
    EXPECT_EQ(session.handle(key(T::Home)).handled, true);
    EXPECT_TRUE(session.handle(key(T::Left)).beep) << "已在最前面";
}

TEST(InputSessionTests, testBackspaceAndEscape) {
    InputSession session(makeLM());
    type(session, "su");
    EXPECT_EQ(session.markedText(), "ㄋㄧ");
    session.handle(key(T::Backspace));
    EXPECT_EQ(session.markedText(), "ㄋ");
    session.handle(key(T::Escape));
    EXPECT_FALSE(session.isComposing());
    type(session, "su3cl3");
    session.handle(key(T::Backspace));
    EXPECT_EQ(session.markedText(), "你");
    session.handle(key(T::Escape));
    EXPECT_FALSE(session.isComposing());
}

TEST(InputSessionTests, testInvalidSyllableIsRejected) {
    InputSession session(makeLM());
    const auto out = type(session, "183").back();  // ㄅㄚˇ 不在迷你詞庫
    EXPECT_TRUE(out.beep);
    EXPECT_EQ(session.markedText(), "ㄅㄚ");
}

TEST(InputSessionTests, testToneKeyWithoutBopomofoDoesNothing) {
    InputSession session(makeLM());
    const auto out = session.handle(ch('3'));
    EXPECT_TRUE(out.handled);
    EXPECT_EQ(out.commit, "");
}

TEST(InputSessionTests, testLongCompositionCommitsLeadingWords) {
    InputSession session(makeLM());
    session.maxCompositionLength = 3;
    const auto outputs = type(session, "su3cl3su3cl3");
    std::string committed;
    for (const auto& o : outputs) committed += o.commit;
    EXPECT_EQ(committed, "你好");
    EXPECT_EQ(session.markedText(), "你好");
}

TEST(InputSessionTests, testUserChoiceIsRemembered) {
    const auto lm = makeLM();
    UserModel memory(lm, std::nullopt);
    InputSession session(lm, &memory);
    type(session, "su3");
    EXPECT_EQ(session.markedText(), "你");
    session.handle(key(T::Down));
    pickNumber(session, indexOf(session.candidateList()->items, "妳"));
    session.handle(key(T::Enter));
    type(session, "su3");
    EXPECT_EQ(session.markedText(), "妳");
}

// MARK: - UserModelTests
// 選字記憶：同一格選最多次的勝出，平手看最近一次，每個字詞最多累計 3 次。

namespace {

/// 輸入法連同選字記憶（記憶的壽命要比 InputSession 長）。
struct MemorySession {
    std::shared_ptr<const LanguageModel> lm;
    std::unique_ptr<UserModel> memory;
    std::unique_ptr<InputSession> session;

    explicit MemorySession(std::shared_ptr<const LanguageModel> model = makeLM(), std::optional<std::filesystem::path> file = std::nullopt)
        : lm(std::move(model)),
          memory(std::make_unique<UserModel>(lm, std::move(file))),
          session(std::make_unique<InputSession>(lm, memory.get())) {}
};

/// 打 ㄋㄧˇ、用 ↓ 選指定的字、按 Enter 送出，回傳送出的文字。
std::string pick(InputSession& session, const std::string& text) {
    type(session, "su3");
    session.handle(key(T::Down));
    pickNumber(session, indexOf(session.candidateList()->items, text));
    return session.handle(key(T::Enter)).commit;
}

/// 不選字、直接打 ㄋㄧˇ 時預設出現的字。
std::string defaultChoice(InputSession& session) {
    type(session, "su3");
    return session.handle(key(T::Enter)).commit;
}

}  // namespace

TEST(UserModelTests, testCorrectionTakesEffectImmediately) {
    MemorySession m;
    auto& session = *m.session;
    pick(session, "妳");
    EXPECT_EQ(defaultChoice(session), "妳");
    pick(session, "你");  // 平手，最近選的是「你」
    EXPECT_EQ(defaultChoice(session), "你");
}

TEST(UserModelTests, testMostPickedWins) {
    MemorySession m;
    auto& session = *m.session;
    pick(session, "妳");
    pick(session, "妳");
    pick(session, "你");
    EXPECT_EQ(defaultChoice(session), "妳") << "2 次贏過 1 次";
    pick(session, "你");
    EXPECT_EQ(defaultChoice(session), "你") << "2 比 2 平手，最近選的是「你」";
}

/// 次數只差一次，也要贏過常用程度差很多的字。
TEST(UserModelTests, testUserChoiceBeatsMuchMoreCommonWord) {
    MemorySession m;
    auto& session = *m.session;
    pick(session, "你");
    pick(session, "妳");
    pick(session, "妳");
    EXPECT_EQ(defaultChoice(session), "妳");
    pick(session, "你");
    EXPECT_EQ(defaultChoice(session), "你");
    pick(session, "妳");  // 平手時看最近，不看哪個字比較常用
    EXPECT_EQ(defaultChoice(session), "妳");
}

/// 選很多次也最多算 3 次：改選 3 次就能換過來。
TEST(UserModelTests, testCountIsCappedSoOldHabitsCanChange) {
    MemorySession m;
    auto& session = *m.session;
    for (int i = 0; i < 6; ++i) pick(session, "妳");
    pick(session, "你");
    pick(session, "你");
    EXPECT_EQ(defaultChoice(session), "妳");
    pick(session, "你");
    EXPECT_EQ(defaultChoice(session), "你");
}

/// 不同的前一個詞是不同的格，互不影響。
TEST(UserModelTests, testContextsAreSeparate) {
    MemorySession m;
    auto& session = *m.session;
    pick(session, "妳");       // 句首選「妳」
    type(session, "cl3su3");  // 「好」後面的 ㄋㄧˇ
    EXPECT_EQ(session.markedText(), "好你");
}

/// 舊版記憶檔（上下文記錄沒有讀音）要能照常使用並轉成新格式。
TEST(UserModelTests, testLoadsOldFormat) {
    TempFile file("bangbang-memory", ".json");
    {
        std::ofstream out(file.path, std::ios::binary);
        out << R"({"contextual":{"<s>\t妳":{"count":5,"lastUsed":1}},"unigram":{"妳\tㄋㄧˇ":{"count":5,"lastUsed":1}}})";
    }
    MemorySession m(makeLM(), file.path);
    auto& session = *m.session;
    EXPECT_EQ(defaultChoice(session), "妳");
    pick(session, "你");
    pick(session, "你");
    pick(session, "你");  // 舊的 5 次以 3 次計，改選 3 次就換過來
    EXPECT_EQ(defaultChoice(session), "你");
}

// MARK: - DecoderTests

TEST(DecoderTests, testUnigramOnly) {
    const Decoder decoder(makeLM());
    const auto walk = decoder.decode(syllableTokens("ㄩㄝˋ"));
    EXPECT_EQ(texts(walk), (Strings{"月"}));
}

TEST(DecoderTests, testBigramContextChangesChoice) {
    const auto lm = makeLM({{"越", "更新", -1.0f}});
    const Decoder decoder(lm);
    const auto walk = decoder.decode(syllableTokens("ㄩㄝˋ ㄍㄥ ㄒㄧㄣ"));
    EXPECT_EQ(joined(walk), "越更新");
}

TEST(DecoderTests, testSkipPairChangesChoice) {
    // 沒有隔詞搭配時：月 更新 越南 用；有「越 ＿ 越」時：越 更新 越 難 用
    const auto reading = syllableTokens("ㄩㄝˋ ㄍㄥ ㄒㄧㄣ ㄩㄝˋ ㄋㄢˊ ㄩㄥˋ");
    EXPECT_EQ(joined(Decoder(makeLM()).decode(reading)), "月更新越南用");
    const auto lm = makeLM({{"越", "難", -1.0f}}, {{"越", "越", 4.0f}});
    EXPECT_EQ(joined(Decoder(lm).decode(reading)), "越更新越難用");
}

TEST(DecoderTests, testPinnedWordIsKept) {
    const Decoder decoder(makeLM());
    const auto walk = decoder.decode(syllableTokens("ㄋㄧˇ ㄏㄠˇ"), {Pin{0, 1, 2}});  // 妳
    EXPECT_EQ(joined(walk), "妳好");
}

TEST(DecoderTests, testPunctuationResetsContext) {
    const Decoder decoder(makeLM());
    auto tokens = syllableTokens("ㄋㄧˇ ㄏㄠˇ");
    tokens.push_back(Token::literalToken("，"));
    tokens.push_back(Token::syllableToken(*Syllable::parse("ㄩㄝˋ")));
    EXPECT_EQ(joined(decoder.decode(tokens)), "你好，月");
}

// MARK: - ModelFileTests

TEST(ModelFileTests, testWriteAndLoadRoundTrip) {
    const auto lm = makeLM({{"越", "更新", -1.0f}, {"你", "好", -0.5f}}, {{"越", "越", 2.0f}});
    TempFile file("bangbang-test", ".lm");
    lm->write(file.path);
    {
        const auto loaded = LanguageModel::load(file.path);
        ASSERT_EQ(loaded->words().size(), lm->words().size());
        for (std::size_t i = 0; i < lm->words().size(); ++i) EXPECT_EQ(loaded->words()[i], lm->words()[i]);
        EXPECT_EQ(loaded->lexicon(), lm->lexicon());
        EXPECT_EQ(loaded->logProb(5, 7), -1.0f);
        EXPECT_EQ(loaded->logProb(1, 3), -0.5f);
        EXPECT_EQ(loaded->logProb(6, 7), lm->logProb(6, 7));
        EXPECT_EQ(loaded->skip(5, 5), 2.0f);
    }  // 先釋放對應，檔案才刪得掉
}
