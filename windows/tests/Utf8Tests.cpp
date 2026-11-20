#include <gtest/gtest.h>

#include "Utf8.h"

using namespace bangbang;

TEST(Utf8Tests, CountsCodePoints) {
    EXPECT_EQ(utf8::length("棒棒注音"), 4u);
    EXPECT_EQ(utf8::length("ㄉㄜ˙"), 3u);
    EXPECT_EQ(utf8::length("𨑨迌"), 2u);  // 「𨑨」在 BMP 以外
    EXPECT_EQ(utf8::length("a𰻞b"), 3u);
    EXPECT_EQ(utf8::length(""), 0u);
}

TEST(Utf8Tests, PrefixAndDropFirst) {
    EXPECT_EQ(utf8::prefix("你好嗎", 2), "你好");
    EXPECT_EQ(utf8::dropFirst("你好嗎", 2), "嗎");
    EXPECT_EQ(utf8::prefix("你好", 5), "你好");
    EXPECT_EQ(utf8::dropFirst("你好", 5), "");
    EXPECT_EQ(utf8::prefix("𨑨迌", 1), "𨑨");
}

TEST(Utf8Tests, SplitsIntoCharacters) {
    const std::vector<std::string> expected{"a", "𨑨", "迌", "，"};
    EXPECT_EQ(utf8::characters("a𨑨迌，"), expected);
}

/// TSF 的位置以 UTF-16 計算：BMP 以外的字佔 2 個單位。
TEST(Utf8Tests, Utf16OffsetsCountSurrogatePairs) {
    const std::string text = "𨑨迌好";
    EXPECT_EQ(utf8::toWide(text).size(), 4u);
    EXPECT_EQ(utf8::utf16Offset(text, 0), 0u);
    EXPECT_EQ(utf8::utf16Offset(text, 1), 2u);
    EXPECT_EQ(utf8::utf16Offset(text, 2), 3u);
    EXPECT_EQ(utf8::utf16Offset(text, 3), 4u);
    EXPECT_EQ(utf8::utf16Offset(text, 9), 4u);
}

TEST(Utf8Tests, WideRoundTrip) {
    for (const std::string text : {"", "abc", "棒棒注音", "𨑨迌", "𰻞𰻞麵", "「你」，ㄅ˙"}) {
        EXPECT_EQ(utf8::fromWide(utf8::toWide(text)), text);
    }
    EXPECT_EQ(utf8::toWide("𨑨"), std::wstring(L"\xD861\xDC68"));
}

TEST(Utf8Tests, InvalidBytesBecomeReplacementCharacter) {
    std::size_t i = 0;
    EXPECT_EQ(utf8::decode("\xFF", i), 0xFFFDu);
    EXPECT_EQ(i, 1u);
    EXPECT_EQ(utf8::length("\xE4\xBD"), 2u);  // 被截斷的 3 位元組序列
    EXPECT_EQ(utf8::fromWide(std::wstring(1, static_cast<wchar_t>(0xD800))), "\xEF\xBF\xBD");
}

TEST(Utf8Tests, AsciiCheck) {
    EXPECT_TRUE(utf8::isAscii("@A1 "));
    EXPECT_FALSE(utf8::isAscii("，"));
}
