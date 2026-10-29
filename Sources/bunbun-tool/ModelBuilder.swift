import BunbunEngine
import Foundation

/// 把詞庫與語料統計結果組成語言模型。
struct ModelBuilder {
    /// 語料詞頻與小麥詞頻的混合比例（越大越相信語料）。
    var corpusWeight = 0.8
    /// 出現少於這個次數的雙連詞不存，改用退回機率估計。
    var minBigramCount: UInt32 = 2
    /// 罕用讀音的機率上限（絕對值），避免「的(ㄉㄧˊ)」這類讀音蓋過真正常用的字。
    var rareReadingProbability = 1e-6
    /// 簡體字、罕用異體字的權重倍數。
    var nonBig5Penalty = 0.01
    /// 隔一個詞搭配：至少出現幾次、關聯強度（PMI）至少多少才收錄，以及加分的比例與上限。
    var minSkipCount: UInt32 = 8
    var minSkipPMI = log(20.0)
    var skipScale: Float = 0.5
    var skipCap: Float = 4

    func build(dictionary dict: SourceDictionary, counter: CorpusCounter?) -> LanguageModel {
        let v = dict.words.count
        var pUni = [Double](repeating: 0, count: v)
        let mcTotal = dict.occurrences.dropFirst().reduce(0) { $0 + $1 + 0.5 }
        for w in 1..<v { pUni[w] = (dict.occurrences[w] + 0.5) / mcTotal }
        if let counter, counter.tokens > 0 {
            let n = Double(counter.tokens)
            for w in 1..<v { pUni[w] = corpusWeight * Double(counter.unigram[w]) / n + (1 - corpusWeight) * pUni[w] }
        }
        var logUnigram = pUni.map { Float(log(max($0, 1e-13))) }
        logUnigram[0] = -30

        var logBackoff = [Float](repeating: 0, count: v)
        var bigramStart = [UInt32](repeating: 0, count: v + 1)
        var bigramNext: [UInt32] = []
        var bigramLogProb: [Float] = []

        if let counter {
            let pairs = counter.bigram.sorted { $0.key < $1.key }
            var n1 = 0.0, n2 = 0.0
            for (_, c) in pairs {
                if c == 1 { n1 += 1 } else if c == 2 { n2 += 1 }
            }
            let d = n1 + 2 * n2 > 0 ? n1 / (n1 + 2 * n2) : 0.75
            info("雙連詞種類 \(pairs.count)，折扣 D = \(String(format: "%.3f", d))")

            var historyTotal = [Double](repeating: 0, count: v)
            var historyTypes = [Double](repeating: 0, count: v)
            for (key, c) in pairs {
                let h = Int(key >> 32)
                historyTotal[h] += Double(c)
                historyTypes[h] += 1
            }
            var lambda = [Double](repeating: 1, count: v)
            for h in 0..<v where historyTotal[h] > 0 {
                lambda[h] = d * historyTypes[h] / historyTotal[h]
                logBackoff[h] = Float(log(lambda[h]))
            }
            var row = 0
            for (key, c) in pairs where c >= minBigramCount {
                let h = Int(key >> 32), w = Int(key & 0xFFFF_FFFF)
                while row < h {
                    row += 1
                    bigramStart[row] = UInt32(bigramNext.count)
                }
                let p = (Double(c) - d) / historyTotal[h] + lambda[h] * pUni[w]
                bigramNext.append(UInt32(w))
                bigramLogProb.append(Float(log(p)))
            }
            while row < v {
                row += 1
                bigramStart[row] = UInt32(bigramNext.count)
            }
            info("保留雙連詞 \(bigramNext.count) 組（次數 ≥ \(minBigramCount)）")
        } else {
            bigramStart = [UInt32](repeating: 0, count: v + 1)
        }

        var skipBoost: [UInt64: Float] = [:]
        if let counter, counter.tokens > 0 {
            let n = Double(counter.tokens)
            var historyTotal = [Double](repeating: 0, count: v)
            for (key, c) in counter.skip { historyTotal[Int(key >> 32)] += Double(c) }
            for (key, c) in counter.skip where c >= minSkipCount {
                let h = Int(key >> 32), w = Int(key & 0xFFFF_FFFF)
                let pmi = log((Double(c) / historyTotal[h]) / (Double(counter.unigram[w]) / n))
                if pmi >= minSkipPMI { skipBoost[key] = min(skipCap, skipScale * Float(pmi)) }
            }
            info("隔一個詞的搭配：收錄 \(skipBoost.count) 組")
        }

        var lexicon: [ReadingKey: [LexiconEntry]] = [:]
        for w in 1..<v {
            let word = dict.words[w]
            for reading in dict.readings[w] {
                let weight = reading.count > 1 ? 0 : Float(log(readingWeight(word, reading[0], pUni[w], dict)))
                lexicon[ReadingKey(reading), default: []].append(LexiconEntry(word: UInt32(w), readingWeight: weight))
            }
        }
        return LanguageModel(words: dict.words, logUnigram: logUnigram, logBackoff: logBackoff,
                             bigramStart: bigramStart, bigramNext: bigramNext, bigramLogProb: bigramLogProb,
                             lexicon: lexicon, skipBoost: skipBoost)
    }

    /// P(讀音 | 字)：只對破音字有意義。
    func readingWeight(_ char: String, _ reading: Syllable, _ p: Double, _ dict: SourceDictionary) -> Double {
        let weight = baseReadingWeight(char, reading, p, dict)
        return dict.nonBig5Chars.contains(char) ? weight * nonBig5Penalty : weight
    }

    func baseReadingWeight(_ char: String, _ reading: Syllable, _ p: Double, _ dict: SourceDictionary) -> Double {
        let rare = min(0.05, rareReadingProbability / p)
        if let curated = dict.curatedWeights[char] {
            return curated[reading] ?? rare
        }
        if let tiers = dict.heterophony[char] {
            if tiers[0].contains(reading) { return 1 }
            if tiers[1].contains(reading) { return max(0.5, rare) }
            if tiers[2].contains(reading) { return max(0.25, rare) }
            return rare
        }
        return 1
    }
}

func info(_ message: String) {
    FileHandle.standardError.write((message + "\n").data(using: .utf8)!)
}
