// Windows 版新增的測試（Mac 版沒有對應項目）：
// - wouldHandle 與 handle 的判斷一致（TSF 會先用 OnTestKeyDown 問要不要吃這個鍵）。
// - BMP 以外的字元在 UTF-16 的位置換算。
// - 記憶檔與 Mac 版互相讀得懂。
// - 壞掉的模型檔在載入時就被拒絕，不會讓宿主程式當掉。
#include <gtest/gtest.h>

#include <fstream>
#include <functional>
#include <sstream>

#include <nlohmann/json.hpp>

#include "InputSession.h"
#include "LanguageModel.h"
#include "TestSupport.h"
#include "UserModel.h"
#include "Utf8.h"

using namespace bangbang;
using namespace bangbang::test;
using T = KeyEvent::Type;

namespace {

std::string describe(const KeyEvent& k) {
    static const char* names[] = {"Character", "Numpad", "Space", "Enter", "Backspace", "ForwardDelete", "Escape", "Tab",
                                  "Left", "Right", "Up", "Down", "Home", "End", "PageUp", "PageDown"};
    std::ostringstream s;
    s << names[static_cast<int>(k.type)];
    if (k.type == T::Character || k.type == T::Numpad) s << " '" << k.ch << "'" << (k.shift ? " +Shift" : "");
    return s.str();
}

std::vector<KeyEvent> allKeys() {
    std::vector<KeyEvent> keys;
    for (const char c : std::string("`1234567890-=qwertyuiop[]\\asdfghjkl;'zxcvbnm,./")) {
        keys.push_back(ch(c, false));
        keys.push_back(ch(c, true));
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

}  // namespace

// MARK: - wouldHandle

/// 各種狀態下的每一個按鍵，wouldHandle 的答案都必須和 handle 實際的 handled 相同。
TEST(SessionConsistencyTests, WouldHandleMatchesHandleInEveryState) {
    const auto lm = makeLM();
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
        {"只有全形標點", [](InputSession& s) { s.handle(ch(',', true)); }, false},
        {"候選窗：字詞", [](InputSession& s) { type(s, "su3cl3"); s.handle(key(T::Home)); s.handle(key(T::Down)); }, true},
        {"候選窗：符號", [](InputSession& s) { s.handle(ch(';', true)); s.handle(key(T::Down)); }, true},
    };
    int checked = 0;
    for (const auto& setup : setups) {
        for (const auto& k : allKeys()) {
            InputSession session(lm);
            setup.run(session);
            ASSERT_EQ(session.candidateList().has_value(), setup.expectCandidates) << setup.name;
            const bool predicted = session.wouldHandle(k);
            const bool actual = session.handle(k).handled;
            EXPECT_EQ(predicted, actual) << setup.name << "：" << describe(k);
            ++checked;
        }
    }
    EXPECT_GT(checked, 900);
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
    TempFile file("bangbang-mac-memory", ".json");
    writeFile(file.path, R"({
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
    UserModel memory(lm, file.path);
    InputSession session(lm, &memory);
    type(session, "su3");
    EXPECT_EQ(session.handle(key(T::Enter)).commit, "妳");
}

/// 寫出的記憶檔是 Mac 版讀得懂的格式：第 2 版、鍵是文字、讀音以空白分隔、一聲不加符號。
TEST(UserModelFileTests, WritesFormatMacCanRead) {
    TempFile file("bangbang-memory-out", ".json");
    const auto lm = makeLM();
    {
        UserModel memory(lm, file.path);
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

    const std::string text = readFile(file.path);
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
    UserModel reloaded(lm, file.path);
    InputSession again(lm, &reloaded);
    type(again, "su3");
    EXPECT_EQ(again.handle(key(T::Enter)).commit, "妳");
}

TEST(UserModelFileTests, CorruptFileIsIgnored) {
    TempFile file("bangbang-memory-bad", ".json");
    writeFile(file.path, "{\"contextual\": {\"<s>\t");
    const auto lm = makeLM();
    UserModel memory(lm, file.path);
    InputSession session(lm, &memory);
    type(session, "su3");
    EXPECT_EQ(session.handle(key(T::Enter)).commit, "你");
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
