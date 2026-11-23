#pragma once

// 對應 Sources/BangbangEngine/Bopomofo.swift：注音音節、標準大千排列、拼注音。

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace bangbang {

/// 一個注音音節：聲母、介音、韻母、聲調。0 代表該位置沒有符號。
struct Syllable {
    std::uint8_t consonant = 0;  // 1...21
    std::uint8_t medial = 0;     // 1...3
    std::uint8_t rime = 0;       // 1...13
    std::uint8_t tone = 0;       // 1...5，0 表示尚未輸入聲調

    /// 解析「ㄉㄜ˙」「ㄍㄥ」這類字串。輕聲符號放在前面或後面都可以。無法解析時回傳 nullopt。
    static std::optional<Syllable> parse(std::string_view text);
    static Syllable fromCode(std::uint16_t code);
    /// 解析以空白（或 -）分隔的多個音節，例如「ㄍㄥ ㄒㄧㄣ」。
    static std::optional<std::vector<Syllable>> parseReading(std::string_view text);

    /// 壓縮成 16 位元整數，作為詞庫索引用：聲母 << 9 | 介音 << 7 | 韻母 << 3 | 聲調。
    std::uint16_t code() const;
    bool isEmpty() const { return consonant == 0 && medial == 0 && rime == 0; }
    /// 顯示用字串（一聲不加符號）。
    std::string description() const;

    friend bool operator==(const Syllable&, const Syllable&) = default;
    friend bool operator<(const Syllable& a, const Syllable& b) { return a.code() < b.code(); }
};

/// 鍵盤上一個注音鍵代表的符號。
struct BopomofoComponent {
    enum class Kind : std::uint8_t { Consonant, Medial, Rime, Tone };
    Kind kind = Kind::Consonant;
    std::uint8_t value = 0;

    static BopomofoComponent consonant(std::uint8_t v) { return {Kind::Consonant, v}; }
    static BopomofoComponent medial(std::uint8_t v) { return {Kind::Medial, v}; }
    static BopomofoComponent rime(std::uint8_t v) { return {Kind::Rime, v}; }
    static BopomofoComponent tone(std::uint8_t v) { return {Kind::Tone, v}; }

    friend bool operator==(const BopomofoComponent&, const BopomofoComponent&) = default;
};

/// 標準大千排列：以美式鍵盤「未按 Shift」時的字元對應注音符號。
namespace DaChenLayout {
std::optional<BopomofoComponent> component(char key);
}

/// 正在拼的注音（還沒按聲調鍵的那一個字）。
class BopomofoComposer {
public:
    const Syllable& syllable() const { return syllable_; }
    bool isEmpty() const { return syllable_.isEmpty(); }
    /// 顯示用：已輸入的符號（不含聲調）。
    std::string display() const { return syllable_.description(); }

    /// 放入一個聲母、介音或韻母；同一位置再按一次會取代。
    void receive(BopomofoComponent component);
    /// 刪掉最靠近游標（顯示在最後面）的那個符號：韻母 → 介音 → 聲母（Mac 版是刪掉最後輸入的）。
    void backspace();
    void clear();
    /// 加上聲調，得到完整音節。
    std::optional<Syllable> completed(std::uint8_t tone) const;

    friend bool operator==(const BopomofoComposer&, const BopomofoComposer&) = default;

private:
    Syllable syllable_;
};

}  // namespace bangbang
