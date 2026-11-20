// 用真正的模型檔（build/bangbang-v3.lm）跑設計文件 8.9 驗收清單裡屬於引擎的項目。
// 模型檔不在 git 裡；找不到時這些測試會略過。
#include <gtest/gtest.h>

#include <filesystem>

#include "InputSession.h"
#include "LanguageModel.h"
#include "TestSupport.h"
#include "UserModel.h"

using namespace bangbang;
using namespace bangbang::test;
using T = KeyEvent::Type;
using Strings = std::vector<std::string>;

namespace {

std::shared_ptr<const LanguageModel> realModel() {
    static const std::shared_ptr<const LanguageModel> lm = []() -> std::shared_ptr<const LanguageModel> {
        const std::filesystem::path path(BANGBANG_MODEL_PATH);
        if (!std::filesystem::exists(path)) return nullptr;
        return LanguageModel::load(path);
    }();
    return lm;
}

#define REQUIRE_MODEL()                                                         \
    const auto lm = realModel();                                                \
    if (!lm) GTEST_SKIP() << "找不到模型檔 build/bangbang-v3.lm，略過真模型測試"

std::string typeAndShow(InputSession& session, std::string_view keys) {
    type(session, keys);
    return session.markedText();
}

}  // namespace

TEST(RealModelTests, ModelHeaderMatchesMacBuild) {
    REQUIRE_MODEL();
    EXPECT_EQ(lm->words().size(), 161860u);
    EXPECT_EQ(lm->bigramCount(), 9781286u);
    EXPECT_EQ(lm->maxPhraseLength(), 9);
    EXPECT_EQ(lm->words()[0], "<s>");
}

/// 打「越更新越難用」得到「越更新越難用」，而不是「月更新越南用」。
TEST(RealModelTests, YueGengXinYueNanYong) {
    REQUIRE_MODEL();
    InputSession session(lm);
    EXPECT_EQ(typeAndShow(session, "m,4e/ vup m,4s06m/4"), "越更新越難用");
    EXPECT_EQ(joined(Decoder(lm).decode(syllableTokens("ㄩㄝˋ ㄍㄥ ㄒㄧㄣ ㄩㄝˋ ㄋㄢˊ ㄩㄥˋ"))), "越更新越難用");
}

/// 打「中文輸入法」：游標在最後按 ↓ 只出現「法」的候選；游標在「輸」前面按 ↓ 出現「輸入法」等。
TEST(RealModelTests, CandidatesDependOnCursorPosition) {
    REQUIRE_MODEL();
    InputSession session(lm);
    ASSERT_EQ(typeAndShow(session, "5j/ jp6gj bj4z83"), "中文輸入法");
    session.handle(key(T::Down));
    const auto atEnd = session.candidateList()->items;
    EXPECT_EQ(atEnd.front().text, "法");
    EXPECT_TRUE(std::all_of(atEnd.begin(), atEnd.end(), [](const Candidate& c) { return c.start == 4 && c.length == 1; }));
    session.handle(key(T::Escape));
    session.handle(key(T::Home));
    session.handle(key(T::Right));
    session.handle(key(T::Right));  // 游標在「輸」前面
    session.handle(key(T::Down));
    const auto items = session.candidateList()->items;
    // 詞庫裡「中文輸入法」是一個五字詞，整句就是這一個詞；目前顯示的「輸」是長詞的一部分，
    // 依規格 3.4 把目前顯示的那個字排第一，其餘照「長詞在前」。
    EXPECT_EQ(joined(session.composition.walk()), "中文輸入法");
    EXPECT_EQ(session.composition.walk().size(), 1u);
    EXPECT_EQ(items.front().text, "輸") << "目前顯示的字排第一";
    EXPECT_EQ(session.candidateList()->highlighted, 0);
    EXPECT_EQ(items[1].text, "輸入法");
    EXPECT_EQ(items[2].text, "輸入");
    EXPECT_TRUE(std::all_of(items.begin(), items.end(), [](const Candidate& c) { return c.start == 2; }));
    for (std::size_t i = 2; i < items.size(); ++i) EXPECT_GE(items[i - 1].length, items[i].length) << "長詞在前";
}

/// 打「中文」後把「文」改成「聞」，「中」不變。
TEST(RealModelTests, SelectionKeepsPrecedingText) {
    REQUIRE_MODEL();
    InputSession session(lm);
    ASSERT_EQ(typeAndShow(session, "5j/ jp6"), "中文");
    session.handle(key(T::Down));
    pickNumber(session, indexOf(session.candidateList()->items, "聞"));
    EXPECT_EQ(session.markedText(), "中聞");
}

/// 組字中按空白：加半形空白、不送出；Enter 才送出。沒有組字時打「，」：進入組字區。
TEST(RealModelTests, SpaceAndPunctuationStayInComposition) {
    REQUIRE_MODEL();
    InputSession session(lm);
    type(session, "5j/ jp6");
    const auto space = session.handle(key(T::Space));
    EXPECT_TRUE(space.handled);
    EXPECT_EQ(space.commit, "");
    EXPECT_EQ(session.markedText(), "中文 ");
    EXPECT_EQ(session.handle(key(T::Enter)).commit, "中文 ");

    const auto comma = session.handle(ch(',', true));
    EXPECT_EQ(comma.commit, "");
    EXPECT_EQ(session.markedText(), "，");
    EXPECT_EQ(session.handle(key(T::Enter)).commit, "，");
    EXPECT_EQ(session.handle(ch('a', true)).commit, "A") << "Shift+A 輸出半形 A";
}

/// 「：」上按 ↓ 有「: ； ;」；把「『』」的前半改成「「」，後半自動變「」」。
TEST(RealModelTests, SymbolCandidatesAndBracketPairs) {
    REQUIRE_MODEL();
    InputSession session(lm);
    session.handle(ch(';', true));
    session.handle(key(T::Down));
    EXPECT_EQ(texts(session.candidateList()->items), (Strings{"：", ":", "；", ";"}));
    session.handle(key(T::Escape));
    session.handle(key(T::Escape));
    session.handle(ch('[', true));
    type(session, "5j/ ");
    session.handle(ch(']', true));
    session.handle(key(T::Home));
    session.handle(key(T::Down));
    pickNumber(session, indexOf(session.candidateList()->items, "「"));
    EXPECT_EQ(session.markedText(), "「中」");
}

/// 在「我愛」後選「妳」1 次 → 預設「妳」；再選「你」1 次 → 預設「你」。
TEST(RealModelTests, MemoryFollowsLatestTie) {
    REQUIRE_MODEL();
    UserModel memory(lm, std::nullopt);
    InputSession session(lm, &memory);
    ASSERT_EQ(typeAndShow(session, "ji394su3"), "我愛你");
    session.handle(key(T::Down));
    pickNumber(session, indexOf(session.candidateList()->items, "妳"));
    EXPECT_EQ(session.handle(key(T::Enter)).commit, "我愛妳");
    EXPECT_EQ(typeAndShow(session, "ji394su3"), "我愛妳");
    session.handle(key(T::Down));
    pickNumber(session, indexOf(session.candidateList()->items, "你"));
    EXPECT_EQ(session.handle(key(T::Enter)).commit, "我愛你");
    EXPECT_EQ(typeAndShow(session, "ji394su3"), "我愛你");
}
