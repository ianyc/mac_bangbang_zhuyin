import BunbunEngine
import Foundation

/// 從小麥注音的原始資料讀出：所有詞、每個詞的讀音、詞頻、破音字讀音順位。
struct SourceDictionary {
    /// 0 號保留給句子邊界。
    var words: [String] = [LanguageModel.boundaryText]
    var wordIndex: [String: UInt32] = [:]
    /// 每個詞的所有讀音（依資料檔順序）。
    var readings: [[[Syllable]]] = [[]]
    /// 小麥注音的出現次數（未出現為 0）。
    var occurrences: [Double] = [0]
    /// 破音字讀音順位：字 → [第一順位讀音集合, 第二, 第三]
    var heterophony: [String: [Set<Syllable>]] = [:]
    /// 自訂讀音比例：字 → [讀音: 比例]
    var curatedWeights: [String: [Syllable: Double]] = [:]
    /// 不在 Big5 字集內的字（簡體字、罕用異體字），自動選字時大幅降低優先順序。
    var nonBig5Chars: Set<String> = []

    init(directory: URL, curated: URL?) throws {
        func lines(_ name: String) throws -> [Substring] {
            let text = try String(contentsOf: directory.appendingPathComponent(name), encoding: .utf8)
            return text.split(whereSeparator: \.isNewline).filter { !$0.hasPrefix("#") }
        }

        func add(_ word: String, _ reading: [Syllable]) {
            guard word.count == reading.count else { return }
            let id: UInt32
            if let existing = wordIndex[word] {
                id = existing
            } else {
                id = UInt32(words.count)
                words.append(word)
                wordIndex[word] = id
                readings.append([])
                occurrences.append(0)
            }
            if !readings[Int(id)].contains(reading) { readings[Int(id)].append(reading) }
        }

        // 單字：字 讀音 拼音 按鍵 編碼
        var big5Chars: Set<String> = []
        for line in try lines("BPMFBase.txt") {
            let parts = line.split(separator: " ")
            guard parts.count >= 2, let s = Syllable(parts[1]) else { continue }
            let char = String(parts[0])
            add(char, [s])
            if let encoding = parts.last, encoding == "big5" || encoding == "big5e" { big5Chars.insert(char) }
        }
        for line in try lines("BPMFBase.txt") {
            let char = String(line.split(separator: " ").first ?? "")
            if !char.isEmpty, !big5Chars.contains(char) { nonBig5Chars.insert(char) }
        }
        // 詞：詞 讀音1 讀音2 ...
        var phrases: [(String, [Syllable])] = []
        for line in try lines("BPMFMappings.txt") {
            let parts = line.split(separator: " ")
            guard parts.count >= 3 else { continue }
            let syllables = parts.dropFirst().compactMap { Syllable($0) }
            guard syllables.count == parts.count - 1 else { continue }
            let word = String(parts[0])
            add(word, syllables)
            phrases.append((word, syllables))
        }

        var counts: [String: Double] = [:]
        for line in try lines("phrase.occ") {
            let parts = line.split(whereSeparator: { $0 == " " || $0 == "\t" })
            guard parts.count >= 2, let c = Double(parts[1]) else { continue }
            counts[String(parts[0])] = c
        }
        // 「國一下」裡的「一下」不該算進「一下」的次數。
        for line in try lines("exclusion.txt") {
            let parts = line.split(separator: "\t")
            guard parts.count >= 2 else { continue }
            let key = String(parts[0]), value = String(parts[1])
            if value.contains(key), let k = counts[key], let v = counts[value] { counts[key] = k - v }
        }
        for (word, count) in counts {
            if let id = wordIndex[word] { occurrences[Int(id)] = max(0, count) }
        }

        for (tier, name) in ["heterophony1.list", "heterophony2.list", "heterophony3.list"].enumerated() {
            for line in try lines(name) {
                let parts = line.split(separator: " ")
                guard parts.count >= 2, let s = Syllable(parts[1]) else { continue }
                var tiers = heterophony[String(parts[0]), default: [[], [], []]]
                tiers[tier].insert(s)
                heterophony[String(parts[0])] = tiers
            }
        }

        if let curated {
            let text = try String(contentsOf: curated, encoding: .utf8)
            for line in text.split(whereSeparator: \.isNewline) where !line.hasPrefix("#") {
                let parts = line.split(separator: " ")
                guard parts.count >= 2 else { continue }
                var weights: [Syllable: Double] = [:]
                for p in parts.dropFirst() {
                    let kv = p.split(separator: "=")
                    guard kv.count == 2, let s = Syllable(kv[0]), let w = Double(kv[1]) else { continue }
                    weights[s] = w
                }
                let total = weights.values.reduce(0, +)
                guard total > 0 else { continue }
                curatedWeights[String(parts[0])] = weights.mapValues { $0 / total }
            }
        }

        // 為常見讀音的詞補上「打字時的讀音」（見 typedVariant）。
        for (word, reading) in phrases where isTypical(word, reading) {
            let variant = SourceDictionary.typedVariant(word, reading)
            if variant != reading { add(word, variant) }
        }
    }

    /// 詞裡每個破音字用的都是常見讀音（避免像「說得 ㄕㄨㄟˋ ㄉㄜˊ」這種罕見讀法也被補上變體）。
    func isTypical(_ word: String, _ reading: [Syllable]) -> Bool {
        for (ch, r) in zip(word, reading) {
            let c = String(ch)
            if let curated = curatedWeights[c] {
                if curated[r] == nil { return false }
            } else if let tiers = heterophony[c], !tiers.contains(where: { $0.contains(r) }) {
                return false
            }
        }
        return true
    }

    var maxWordLength: Int { words.dropFirst().map(\.unicodeScalars.count).max() ?? 1 }

    /// 詞庫常記變調或書面讀音，但打字時大家打的是本調：
    /// 不要（ㄅㄨˊ→ㄅㄨˋ）、一下（ㄧˊ→ㄧ）、我覺得（ㄉㄜˊ→ㄉㄜ˙）。
    static func typedVariant(_ word: String, _ reading: [Syllable]) -> [Syllable] {
        let bu = Syllable("ㄅㄨˋ")!, buSandhi = Syllable("ㄅㄨˊ")!
        let yi = Syllable("ㄧ")!, yi2 = Syllable("ㄧˊ")!, yi4 = Syllable("ㄧˋ")!
        let de = Syllable("ㄉㄜ˙")!, de2 = Syllable("ㄉㄜˊ")!
        var result = reading
        for (i, ch) in word.enumerated() where i < result.count {
            switch ch {
            case "不" where result[i] == buSandhi: result[i] = bu
            case "一" where result[i] == yi2 || result[i] == yi4: result[i] = yi
            case "得" where i > 0 && result[i] == de2: result[i] = de
            default: break
            }
        }
        return result
    }
}

/// 以 Unicode 字元為邊的字典樹，用來對語料斷詞。
struct ScalarTrie {
    private var edges: [UInt64: Int32] = [:]
    private(set) var terminal: [Int32] = [-1]

    init(words: [String]) {
        for (id, word) in words.enumerated() where id != 0 {
            var node: Int32 = 0
            for scalar in word.unicodeScalars {
                let key = UInt64(UInt32(node)) << 32 | UInt64(scalar.value)
                if let next = edges[key] {
                    node = next
                } else {
                    let next = Int32(terminal.count)
                    terminal.append(-1)
                    edges[key] = next
                    node = next
                }
            }
            terminal[Int(node)] = Int32(id)
        }
    }

    @inline(__always)
    func child(_ node: Int32, _ scalar: UInt32) -> Int32? {
        edges[UInt64(UInt32(node)) << 32 | UInt64(scalar)]
    }

    /// 這個字本身是否為詞庫中的單字。
    func isSingleCharWord(_ scalar: UInt32) -> Bool {
        guard let c = child(0, scalar) else { return false }
        return terminal[Int(c)] >= 0
    }
}
