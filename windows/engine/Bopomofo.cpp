#include "Bopomofo.h"

#include <algorithm>
#include <array>

#include "Utf8.h"

namespace bangbang {

namespace {

constexpr std::u32string_view kConsonants = U"ㄅㄆㄇㄈㄉㄊㄋㄌㄍㄎㄏㄐㄑㄒㄓㄔㄕㄖㄗㄘㄙ";
constexpr std::u32string_view kMedials = U"ㄧㄨㄩ";
constexpr std::u32string_view kRimes = U"ㄚㄛㄜㄝㄞㄟㄠㄡㄢㄣㄤㄥㄦ";
/// 聲調符號，索引 1...5 對應一聲、二聲、三聲、四聲、輕聲。一聲不寫符號。
constexpr std::array<char32_t, 6> kToneMarks = {0, 0, U'ˊ', U'ˇ', U'ˋ', U'˙'};

static_assert(kConsonants.size() == 21 && kMedials.size() == 3 && kRimes.size() == 13);

int toneIndex(char32_t ch) {
    for (std::size_t i = 2; i < kToneMarks.size(); ++i) {
        if (kToneMarks[i] == ch) return static_cast<int>(i);
    }
    return -1;
}

}  // namespace

std::optional<Syllable> Syllable::parse(std::string_view text) {
    Syllable s;
    for (std::size_t i = 0; i < text.size();) {
        const char32_t ch = utf8::decode(text, i);
        if (const auto k = kConsonants.find(ch); k != std::u32string_view::npos) {
            if (s.consonant != 0) return std::nullopt;
            s.consonant = static_cast<std::uint8_t>(k + 1);
        } else if (const auto m = kMedials.find(ch); m != std::u32string_view::npos) {
            if (s.medial != 0) return std::nullopt;
            s.medial = static_cast<std::uint8_t>(m + 1);
        } else if (const auto r = kRimes.find(ch); r != std::u32string_view::npos) {
            if (s.rime != 0) return std::nullopt;
            s.rime = static_cast<std::uint8_t>(r + 1);
        } else if (const int t = toneIndex(ch); t >= 0) {
            if (s.tone != 0) return std::nullopt;
            s.tone = static_cast<std::uint8_t>(t);
        } else {
            return std::nullopt;
        }
    }
    if (s.isEmpty()) return std::nullopt;
    if (s.tone == 0) s.tone = 1;
    return s;
}

Syllable Syllable::fromCode(std::uint16_t code) {
    Syllable s;
    s.consonant = static_cast<std::uint8_t>((code >> 9) & 0x1F);
    s.medial = static_cast<std::uint8_t>((code >> 7) & 0x3);
    s.rime = static_cast<std::uint8_t>((code >> 3) & 0xF);
    s.tone = static_cast<std::uint8_t>(code & 0x7);
    return s;
}

std::uint16_t Syllable::code() const {
    return static_cast<std::uint16_t>(consonant << 9 | medial << 7 | rime << 3 | tone);
}

std::string Syllable::description() const {
    std::string out;
    if (consonant > 0) utf8::append(out, kConsonants[consonant - 1]);
    if (medial > 0) utf8::append(out, kMedials[medial - 1]);
    if (rime > 0) utf8::append(out, kRimes[rime - 1]);
    if (tone > 1 && tone < kToneMarks.size()) utf8::append(out, kToneMarks[tone]);
    return out;
}

std::optional<std::vector<Syllable>> Syllable::parseReading(std::string_view text) {
    std::vector<Syllable> result;
    std::size_t start = 0;
    // 以空白或 - 分隔，略過空的片段（同 Swift 的 split）。
    for (std::size_t i = 0; i <= text.size(); ++i) {
        if (i < text.size() && text[i] != ' ' && text[i] != '-') continue;
        if (i > start) {
            const auto s = Syllable::parse(text.substr(start, i - start));
            if (!s) return std::nullopt;
            result.push_back(*s);
        }
        start = i + 1;
    }
    if (result.empty()) return std::nullopt;
    return result;
}

namespace DaChenLayout {

std::optional<BopomofoComponent> component(char key) {
    static const std::array<std::optional<BopomofoComponent>, 128> table = [] {
        std::array<std::optional<BopomofoComponent>, 128> t{};
        const std::string_view consonantKeys = "1qaz2wsxedcrfv5tgbyhn";
        for (std::size_t i = 0; i < consonantKeys.size(); ++i) {
            t[static_cast<unsigned char>(consonantKeys[i])] = BopomofoComponent::consonant(static_cast<std::uint8_t>(i + 1));
        }
        const std::string_view medialKeys = "ujm";
        for (std::size_t i = 0; i < medialKeys.size(); ++i) {
            t[static_cast<unsigned char>(medialKeys[i])] = BopomofoComponent::medial(static_cast<std::uint8_t>(i + 1));
        }
        const std::string_view rimeKeys = "8ik,9ol.0p;/-";
        for (std::size_t i = 0; i < rimeKeys.size(); ++i) {
            t[static_cast<unsigned char>(rimeKeys[i])] = BopomofoComponent::rime(static_cast<std::uint8_t>(i + 1));
        }
        t[' '] = BopomofoComponent::tone(1);
        t['6'] = BopomofoComponent::tone(2);
        t['3'] = BopomofoComponent::tone(3);
        t['4'] = BopomofoComponent::tone(4);
        t['7'] = BopomofoComponent::tone(5);
        return t;
    }();
    const auto index = static_cast<unsigned char>(key);
    if (index >= table.size()) return std::nullopt;
    return table[index];
}

}  // namespace DaChenLayout

void BopomofoComposer::receive(BopomofoComponent component) {
    switch (component.kind) {
    case BopomofoComponent::Kind::Consonant:
        syllable_.consonant = component.value;
        break;
    case BopomofoComponent::Kind::Medial:
        syllable_.medial = component.value;
        break;
    case BopomofoComponent::Kind::Rime:
        syllable_.rime = component.value;
        break;
    case BopomofoComponent::Kind::Tone:
        break;
    }
}

void BopomofoComposer::backspace() {
    // 刪掉最靠近游標（顯示在最後面）的符號：韻母 → 介音 → 聲母。Windows 版的改動（2026-11-23 使用者要求），
    // Mac 版是刪掉最後輸入的那個：先打 ㄏㄨ 再打 ㄈ（取代成 ㄈㄨ）時，Mac 版刪 ㄈ，這裡刪 ㄨ。
    if (syllable_.rime) {
        syllable_.rime = 0;
    } else if (syllable_.medial) {
        syllable_.medial = 0;
    } else {
        syllable_.consonant = 0;
    }
}

void BopomofoComposer::clear() {
    syllable_ = Syllable();
}

std::optional<Syllable> BopomofoComposer::completed(std::uint8_t tone) const {
    if (isEmpty()) return std::nullopt;
    Syllable s = syllable_;
    s.tone = tone;
    return s;
}

}  // namespace bangbang
