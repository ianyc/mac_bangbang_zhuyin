import BunbunEngine
import Foundation

/// 把中文句子轉成注音（評測用），以及計算選字正確率。
final class Evaluator {
    let lm: LanguageModel
    let decoder: Decoder
    private var readingsOfWord: [UInt32: [ReadingKey]] = [:]
    private var singleWeight: [UInt32: [UInt16: Float]] = [:]
    private var wordIndex: [String: UInt32] = [:]
    private let trie: ScalarTrie

    init(lm: LanguageModel) {
        self.lm = lm
        self.decoder = Decoder(lm: lm)
        for (key, entries) in lm.lexicon {
            for e in entries {
                readingsOfWord[e.word, default: []].append(key)
                if key.codes.count == 1 { singleWeight[e.word, default: [:]][key.codes[0]] = e.readingWeight }
            }
        }
        for (i, w) in lm.words.enumerated() where i > 0 { wordIndex[w] = UInt32(i) }
        trie = ScalarTrie(words: lm.words)
    }

    /// 以詞庫斷詞後，每個詞取最常見的讀音。
    func reading(of text: String) -> [Syllable]? {
        let scalars = Array(text.unicodeScalars.map(\.value))
        let n = scalars.count
        guard n > 0 else { return nil }
        var best = [Float](repeating: -.greatestFiniteMagnitude, count: n + 1)
        var from = [Int](repeating: 0, count: n + 1)
        var word = [UInt32](repeating: 0, count: n + 1)
        best[0] = 0
        for i in 0..<n where best[i] > -.greatestFiniteMagnitude {
            var node: Int32 = 0
            var j = i
            while j < n, let next = trie.child(node, scalars[j]) {
                node = next
                j += 1
                let w = trie.terminal[Int(node)]
                if w >= 0, readingsOfWord[UInt32(w)] != nil {
                    let s = best[i] + lm.logUnigram[Int(w)]
                    if s > best[j] {
                        best[j] = s
                        from[j] = i
                        word[j] = UInt32(w)
                    }
                }
            }
        }
        guard best[n] > -.greatestFiniteMagnitude else { return nil }
        var words: [UInt32] = []
        var k = n
        while k > 0 {
            words.append(word[k])
            k = from[k]
        }
        var result: [Syllable] = []
        for w in words.reversed() {
            guard let keys = readingsOfWord[w] else { return nil }
            let chosen = keys.max { a, b in score(a, w) < score(b, w) }!
            result.append(contentsOf: chosen.syllables)
        }
        return result
    }

    /// 詞的讀音取「每個字在該讀音下的權重總和」最高者。
    private func score(_ key: ReadingKey, _ word: UInt32) -> Float {
        if key.codes.count == 1 { return singleWeight[word]?[key.codes[0]] ?? 0 }
        let chars = Array(lm.words[Int(word)])
        var s: Float = 0
        for (i, code) in key.codes.enumerated() where i < chars.count {
            if let c = wordIndex[String(chars[i])] { s += singleWeight[c]?[code] ?? -5 }
        }
        return s
    }

    func convert(_ syllables: [Syllable]) -> String {
        decoder.decode(syllables.map { .syllable($0) }).map(\.text).joined()
    }

    struct Result {
        var sentences = 0
        var exact = 0
        var chars = 0
        var correctChars = 0
        var failures: [(expected: String, got: String, reading: String)] = []

        var charAccuracy: Double { chars == 0 ? 0 : Double(correctChars) / Double(chars) }
        var sentenceAccuracy: Double { sentences == 0 ? 0 : Double(exact) / Double(sentences) }
    }

    /// 他／她／它要看使用者想指誰，選哪個都不算錯。
    static func equivalent(_ a: Character, _ b: Character) -> Bool {
        let pronouns: Set<Character> = ["他", "她", "它", "牠"]
        return a == b || (pronouns.contains(a) && pronouns.contains(b))
    }

    /// 每行一句；可用「句子 | 注音」指定讀音（音節以空白分隔）。# 開頭為註解。
    func evaluate(lines: [String], limit: Int = .max) -> Result {
        var result = Result()
        for line in lines.prefix(limit) {
            let trimmed = line.trimmingCharacters(in: .whitespaces)
            if trimmed.isEmpty || trimmed.hasPrefix("#") { continue }
            let parts = trimmed.split(separator: "|", maxSplits: 1).map { $0.trimmingCharacters(in: .whitespaces) }
            let expected = parts[0]
            let syllables: [Syllable]?
            if parts.count == 2 { syllables = Syllable.parseReading(parts[1]) } else { syllables = reading(of: expected) }
            guard let syllables, syllables.count == expected.count else {
                info("無法轉成注音：\(expected)")
                continue
            }
            let got = convert(syllables)
            result.sentences += 1
            result.chars += expected.count
            let same = zip(expected, got).filter { Evaluator.equivalent($0, $1) }.count
            result.correctChars += same
            if same == expected.count {
                result.exact += 1
            } else {
                result.failures.append((expected, got, syllables.map(\.description).joined(separator: " ")))
            }
        }
        return result
    }
}
