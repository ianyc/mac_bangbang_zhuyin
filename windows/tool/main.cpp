// bangbang-tool：棒棒注音 Windows 版的開發工具。對應 Sources/bangbang-tool/main.swift。
// 指令與輸出格式都和 Mac 版相同，方便兩邊直接比對。建模型（build-lm）目前沿用 Mac 版。
#include <windows.h>

#include <algorithm>
#include <chrono>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include "BangbangVersion.h"
#include "Decoder.h"
#include "Evaluator.h"
#include "InputSession.h"
#include "LanguageModel.h"
#include "UserModel.h"
#include "Utf8.h"

using namespace bangbang;

namespace {

constexpr const char* kUsage =
    "棒棒注音 Windows 版開發工具 " BANGBANG_VERSION_STRING "\n"
    "用法：\n"
    "  bangbang-tool convert [--lm <模型檔>] <注音，音節以空白分隔>\n"
    "  bangbang-tool lookup  [--lm <模型檔>] <注音>\n"
    "  bangbang-tool score   [--lm <模型檔>] <詞> <詞> ...\n"
    "  bangbang-tool reading [--lm <模型檔>] <中文> ...\n"
    "  bangbang-tool eval    [--lm <模型檔>] <測試句檔> [--limit N] [--show N] [--timing]\n"
    "  bangbang-tool keys    [--lm <模型檔>] [--memory <記憶檔>] \"<按鍵>\"\n"
    "沒有指定 --lm 時，使用 repo 的 build/bangbang-v3.lm。\n"
    "keys 的特殊鍵：<space> <enter> <esc> <bs> <del> <left> <right> <up> <down> <home> <end>，\n"
    "<pick:字> 在候選窗選指定的字，大寫字母代表 Shift+字母。\n";

std::vector<std::string> args;

void print(const std::string& line) {
    std::fputs((line + "\n").c_str(), stdout);
}

void info(const std::string& message) {
    std::fflush(stdout);
    std::fputs((message + "\n").c_str(), stderr);
}

std::string format(const char* fmt, ...) {
    va_list list;
    va_start(list, fmt);
    va_list copy;
    va_copy(copy, list);
    const int n = std::vsnprintf(nullptr, 0, fmt, copy);
    va_end(copy);
    std::string out(n > 0 ? static_cast<std::size_t>(n) : 0, '\0');
    if (n > 0) std::vsnprintf(out.data(), out.size() + 1, fmt, list);
    va_end(list);
    return out;
}

[[noreturn]] void usage() {
    std::fputs(kUsage, stdout);
    std::exit(1);
}

/// 取出並移除「--name 值」。
std::optional<std::string> option(const std::string& name) {
    const auto it = std::find(args.begin(), args.end(), name);
    if (it == args.end() || it + 1 == args.end()) return std::nullopt;
    std::string value = *(it + 1);
    args.erase(it, it + 2);
    return value;
}

std::string joinedArgs() {
    std::string out;
    for (std::size_t i = 0; i < args.size(); ++i) {
        if (i > 0) out += ' ';
        out += args[i];
    }
    return out;
}

std::filesystem::path pathFromUtf8(const std::string& s) {
    return std::filesystem::path(utf8::toWide(s));
}

std::shared_ptr<const LanguageModel> loadModel() {
    const auto lm = option("--lm");
    const std::filesystem::path path = lm ? pathFromUtf8(*lm) : std::filesystem::path(BANGBANG_DEFAULT_MODEL);
    try {
        return LanguageModel::load(path);
    } catch (const std::exception& e) {
        info(std::string("無法讀取模型：") + e.what() + "（" + utf8::fromWide(path.wstring()) + "）");
        std::exit(1);
    }
}

std::vector<std::string> readLines(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        info("無法讀取：" + utf8::fromWide(path.wstring()));
        std::exit(1);
    }
    const std::string data((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    std::vector<std::string> lines;
    std::size_t start = 0;
    for (std::size_t i = 0; i <= data.size(); ++i) {
        if (i == data.size() || data[i] == '\n') {
            std::string line = data.substr(start, i - start);
            if (!line.empty() && line.back() == '\r') line.pop_back();
            lines.push_back(std::move(line));
            start = i + 1;
        }
    }
    return lines;
}

std::string readingText(const std::vector<Syllable>& syllables) {
    std::string out;
    for (std::size_t i = 0; i < syllables.size(); ++i) {
        if (i > 0) out += ' ';
        out += syllables[i].description();
    }
    return out;
}

int commandConvert() {
    const auto lm = loadModel();
    const auto syllables = Syllable::parseReading(joinedArgs());
    if (!syllables) {
        info("注音格式錯誤");
        return 1;
    }
    std::vector<Token> tokens;
    for (const auto& s : *syllables) tokens.push_back(Token::syllableToken(s));
    const auto walk = Decoder(lm).decode(tokens);
    std::string plain, separated;
    for (std::size_t i = 0; i < walk.size(); ++i) {
        plain += walk[i].text;
        if (i > 0) separated += " / ";
        separated += walk[i].text;
    }
    print(plain);
    print(separated);
    return 0;
}

int commandLookup() {
    const auto lm = loadModel();
    const auto syllables = Syllable::parseReading(joinedArgs());
    if (!syllables) {
        info("注音格式錯誤");
        return 1;
    }
    const auto entries = lm->entries(ReadingKey::of(*syllables));
    for (std::size_t i = 0; i < entries.size() && i < 20; ++i) {
        const auto e = entries[i];
        print(format("%s\tunigram %.2f\t讀音權重 %.2f", std::string(lm->words()[e.word]).c_str(),
                     static_cast<double>(lm->logUnigram()[e.word]), static_cast<double>(e.readingWeight)));
    }
    return 0;
}

int commandScore() {
    // 計算一種斷詞的分數明細，例如：score 越 更新 越 難 用
    const auto lm = loadModel();
    std::unordered_map<std::string, WordId> index;
    for (std::size_t i = 0; i < lm->words().size(); ++i) index[std::string(lm->words()[i])] = static_cast<WordId>(i);
    WordId prev = LanguageModel::kBoundary, prev2 = LanguageModel::kBoundary;
    float total = 0;
    for (const auto& w : args) {
        const auto it = index.find(w);
        if (it == index.end()) {
            info("詞庫沒有：" + w);
            return 1;
        }
        const WordId id = it->second;
        const float bi = lm->logProb(prev, id), sk = lm->skip(prev2, id);
        const char* stored = lm->storedBigram(prev, id) ? "有統計" : "退回";
        print(format("%s\t雙連 %.2f（%s）\t隔詞 +%.2f", w.c_str(), static_cast<double>(bi), stored, static_cast<double>(sk)));
        total += bi + sk;
        prev2 = prev;
        prev = id;
    }
    print(format("合計 %.2f（未含讀音權重）", static_cast<double>(total)));
    return 0;
}

int commandReading() {
    const auto lm = loadModel();
    const Evaluator evaluator(lm);
    for (const auto& text : args) {
        const auto r = evaluator.reading(text);
        print(r ? readingText(*r) : "（無法轉換）");
    }
    return 0;
}

int commandEval() {
    const auto limitText = option("--limit");
    const auto showText = option("--show");
    const bool timing = std::find(args.begin(), args.end(), "--timing") != args.end();
    std::erase(args, std::string("--timing"));
    const std::size_t limit = limitText ? static_cast<std::size_t>(std::stoull(*limitText)) : static_cast<std::size_t>(-1);
    const std::size_t show = showText ? static_cast<std::size_t>(std::stoull(*showText)) : 30;
    const auto lm = loadModel();
    if (args.empty()) usage();
    const auto lines = readLines(pathFromUtf8(args.front()));
    const Evaluator evaluator(lm);
    const auto start = std::chrono::steady_clock::now();
    const auto r = evaluator.evaluate(lines, limit);
    const double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
    for (std::size_t i = 0; i < r.failures.size() && i < show; ++i) {
        const auto& f = r.failures[i];
        print("✗ 應為：" + f.expected + "\n  結果：" + f.got + "\n  注音：" + f.reading);
    }
    print(format("句子 %d 句，整句全對 %.1f%%，逐字正確率 %.2f%%（%.1f 秒）", r.sentences, r.sentenceAccuracy() * 100,
                 r.charAccuracy() * 100, seconds));
    if (timing && r.sentences > 0) {
        print(format("其中整句選字 %.2f 秒（每句 %.3f 毫秒），其餘是中文轉注音與比對", r.decodeSeconds,
                     r.decodeSeconds * 1000 / r.sentences));
    }
    return 0;
}

int commandKeys() {
    // 模擬按鍵，例如：keys "5j/ jp6<down><pick:聞><enter>"
    const auto memoryPath = option("--memory");  // 指定記憶檔（會寫入，請用副本）
    const auto lm = loadModel();
    UserModel memory(lm, memoryPath ? std::optional(pathFromUtf8(*memoryPath)) : std::nullopt);
    InputSession session(lm, &memory);
    using T = KeyEvent::Type;
    const std::map<std::string, KeyEvent> specials = {
        {"space", KeyEvent::of(T::Space)}, {"enter", KeyEvent::of(T::Enter)}, {"esc", KeyEvent::of(T::Escape)},
        {"bs", KeyEvent::of(T::Backspace)}, {"del", KeyEvent::of(T::ForwardDelete)}, {"left", KeyEvent::of(T::Left)},
        {"right", KeyEvent::of(T::Right)}, {"up", KeyEvent::of(T::Up)}, {"down", KeyEvent::of(T::Down)},
        {"home", KeyEvent::of(T::Home)}, {"end", KeyEvent::of(T::End)},
    };
    std::string committed;
    const auto show = [&](const std::string& label) {
        std::string line = label + "\t組字：「" + session.markedText() + "」";
        if (const auto& list = session.candidateList()) {
            line += "\t候選：";
            for (std::size_t i = 0; i < list->items.size() && i < 9; ++i) {
                if (i > 0) line += ' ';
                if (static_cast<int>(i) == list->highlighted) line += "▸";
                line += std::to_string(i + 1) + "." + list->items[i].text;
            }
        }
        print(line);
    };
    auto chars = utf8::characters(joinedArgs());
    std::size_t pos = 0;
    while (pos < chars.size()) {
        KeyEvent k;
        std::string label = chars[pos];
        // 找出 <...> 的結尾
        std::size_t close = pos;
        if (chars[pos] == "<") {
            while (close < chars.size() && chars[close] != ">") ++close;
        }
        std::string inner;
        if (chars[pos] == "<" && close < chars.size()) {
            for (std::size_t i = pos + 1; i < close; ++i) inner += chars[i];
        }
        if (chars[pos] == "<" && close < chars.size() && inner.rfind("pick:", 0) == 0) {
            const std::string target = inner.substr(5);
            label = "<" + inner + ">";
            pos = close + 1;
            const auto& list = session.candidateList();
            const auto found = list ? std::find_if(list->items.begin(), list->items.end(), [&](const Candidate& c) { return c.text == target; })
                                    : std::vector<Candidate>::const_iterator();
            if (!list || found == list->items.end()) {
                show(label + "（候選窗裡沒有）");
                continue;
            }
            const int index = static_cast<int>(found - list->items.begin());
            const int perPage = list->perPage;
            while (session.candidateList() && session.candidateList()->page() != index / perPage) session.handle(KeyEvent::of(T::PageDown));
            k = KeyEvent::character(static_cast<char>('1' + index % perPage));
        } else if (chars[pos] == "<" && close < chars.size() && specials.contains(inner)) {
            k = specials.at(inner);
            label = "<" + inner + ">";
            pos = close + 1;
        } else {
            const std::string c = chars[pos++];
            if (c == " ") {
                k = KeyEvent::of(T::Space);
            } else if (c.size() == 1 && c[0] >= 'A' && c[0] <= 'Z') {
                k = KeyEvent::character(static_cast<char>(c[0] - 'A' + 'a'), true);
            } else if (c.size() == 1) {
                k = KeyEvent::character(c[0]);
            } else {
                k = KeyEvent::character('\0');  // 鍵盤上沒有的字元
            }
        }
        const auto out = session.handle(k);
        committed += out.commit;
        if (label.front() == '<' || out.beep) show(label + (out.beep ? "（嗶）" : ""));
    }
    show("結束");
    print("已送出：「" + committed + "」");
    return 0;
}

}  // namespace

int wmain(int argc, wchar_t** argv) {
    SetConsoleOutputCP(CP_UTF8);
    for (int i = 1; i < argc; ++i) args.push_back(utf8::fromWide(argv[i]));
    if (args.empty()) usage();
    const std::string command = args.front();
    args.erase(args.begin());
    try {
        if (command == "convert") return commandConvert();
        if (command == "lookup") return commandLookup();
        if (command == "score") return commandScore();
        if (command == "reading") return commandReading();
        if (command == "eval") return commandEval();
        if (command == "keys") return commandKeys();
    } catch (const std::exception& e) {
        info(std::string("錯誤：") + e.what());
        return 1;
    }
    usage();
}
