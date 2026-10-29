import BunbunEngine
import Foundation

/// 讀取語料（每篇文章是 JSON 裡的一個 "text" 字串），斷詞後統計單詞與雙連詞次數。
final class CorpusCounter {
    let trie: ScalarTrie
    let segmentScore: [Float]
    let maxWordLength: Int
    /// 每隔幾篇保留一篇不拿來訓練，作為評測用。
    let heldoutEvery: Int
    let heldoutLimit: Int

    private(set) var unigram: [UInt64]
    private(set) var bigram: [UInt64: UInt32] = [:]
    /// 隔一個詞的詞對次數：(前前詞, 詞)。
    private(set) var skip: [UInt64: UInt32] = [:]
    private(set) var heldout: [String] = []
    private(set) var documents = 0
    private(set) var tokens: UInt64 = 0

    private var isWordChar = [Bool](repeating: false, count: 0x40000)
    private var scalars: [UInt32] = []
    private var best: [Float] = []
    private var from: [Int32] = []
    private var fromWord: [Int32] = []
    private var path: [Int32] = []

    init(dictionary: SourceDictionary, heldoutEvery: Int = 100, heldoutLimit: Int = 20000) {
        trie = ScalarTrie(words: dictionary.words)
        maxWordLength = dictionary.maxWordLength
        self.heldoutEvery = heldoutEvery
        self.heldoutLimit = heldoutLimit
        unigram = [UInt64](repeating: 0, count: dictionary.words.count)
        // 斷詞用小麥注音的詞頻（未出現的詞當 0.5 次）。
        let total = dictionary.occurrences.reduce(0, +) + 0.5 * Double(dictionary.words.count)
        segmentScore = dictionary.occurrences.map { Float(log(($0 + 0.5) / total)) }
        for scalar in 0..<UInt32(isWordChar.count) where trie.isSingleCharWord(scalar) { isWordChar[Int(scalar)] = true }
    }

    func process(file url: URL, stripPTTHeader: Bool) throws {
        let data = try Data(contentsOf: url, options: .alwaysMapped)
        let marker = Array("\"text\": \"".utf8)
        let body = Array("\n內文: ".unicodeScalars.map(\.value))
        data.withUnsafeBytes { (raw: UnsafeRawBufferPointer) in
            let bytes = raw.bindMemory(to: UInt8.self)
            var i = 0
            let n = bytes.count
            while i + marker.count < n {
                // 找下一個 "text": "
                guard bytes[i] == UInt8(ascii: "\""), matches(bytes, at: i, marker) else {
                    i += 1
                    continue
                }
                i = decodeJSONString(bytes, from: i + marker.count - 1)
                var start = 0
                if stripPTTHeader, let pos = find(body, in: scalars) { start = pos + body.count }
                documents += 1
                processDocument(from: start, heldout: documents % heldoutEvery == 0)
                if documents % 200_000 == 0 {
                    FileHandle.standardError.write("  \(documents) 篇，\(tokens) 個詞，\(bigram.count) 組雙連詞\n".data(using: .utf8)!)
                }
            }
        }
    }

    private func matches(_ bytes: UnsafeBufferPointer<UInt8>, at i: Int, _ pattern: [UInt8]) -> Bool {
        for k in 0..<pattern.count where bytes[i + k] != pattern[k] { return false }
        return true
    }

    private func find(_ pattern: [UInt32], in text: [UInt32]) -> Int? {
        guard text.count >= pattern.count else { return nil }
        outer: for i in 0...(text.count - pattern.count) {
            for k in 0..<pattern.count where text[i + k] != pattern[k] { continue outer }
            return i
        }
        return nil
    }

    /// 解析從 quote 位置開始的 JSON 字串，結果放在 scalars；回傳字串結束後的位置。
    private func decodeJSONString(_ b: UnsafeBufferPointer<UInt8>, from quote: Int) -> Int {
        scalars.removeAll(keepingCapacity: true)
        var i = quote + 1
        let n = b.count
        func hex4(_ at: Int) -> UInt32? {
            guard at + 4 <= n else { return nil }
            var v: UInt32 = 0
            for k in 0..<4 {
                let c = b[at + k]
                let d: UInt32
                switch c {
                case 48...57: d = UInt32(c - 48)
                case 65...70: d = UInt32(c - 55)
                case 97...102: d = UInt32(c - 87)
                default: return nil
                }
                v = v << 4 | d
            }
            return v
        }
        while i < n {
            let c = b[i]
            if c == UInt8(ascii: "\"") { return i + 1 }
            if c == UInt8(ascii: "\\"), i + 1 < n {
                let e = b[i + 1]
                i += 2
                switch e {
                case UInt8(ascii: "n"), UInt8(ascii: "r"), UInt8(ascii: "t"): scalars.append(10)
                case UInt8(ascii: "u"):
                    guard var v = hex4(i) else { continue }
                    i += 4
                    if (0xD800..<0xDC00).contains(v), i + 6 <= n, b[i] == UInt8(ascii: "\\"), b[i + 1] == UInt8(ascii: "u"),
                       let low = hex4(i + 2), (0xDC00..<0xE000).contains(low) {
                        v = 0x10000 + ((v - 0xD800) << 10) + (low - 0xDC00)
                        i += 6
                    }
                    scalars.append(v)
                default: scalars.append(UInt32(e))
                }
                continue
            }
            // UTF-8 解碼
            if c < 0x80 {
                scalars.append(UInt32(c))
                i += 1
            } else if c < 0xE0, i + 1 < n {
                scalars.append(UInt32(c & 0x1F) << 6 | UInt32(b[i + 1] & 0x3F))
                i += 2
            } else if c < 0xF0, i + 2 < n {
                scalars.append(UInt32(c & 0x0F) << 12 | UInt32(b[i + 1] & 0x3F) << 6 | UInt32(b[i + 2] & 0x3F))
                i += 3
            } else if i + 3 < n {
                scalars.append(UInt32(c & 0x07) << 18 | UInt32(b[i + 1] & 0x3F) << 12 | UInt32(b[i + 2] & 0x3F) << 6 | UInt32(b[i + 3] & 0x3F))
                i += 4
            } else {
                i += 1
            }
        }
        return n
    }

    /// 以「不在詞庫裡的字元」（標點、英數、空白）切成小段，每段斷詞後計數。
    private func processDocument(from start: Int, heldout isHeldout: Bool) {
        var runStart = -1
        let n = scalars.count
        var i = start
        while i <= n {
            let ok = i < n && scalars[i] < UInt32(isWordChar.count) && isWordChar[Int(scalars[i])]
            if ok {
                if runStart < 0 { runStart = i }
            } else if runStart >= 0 {
                // 緊接在數字或英文後面的段落（如「8月31日」「3 月 2 日」的「月」）不是句子開頭。
                var before = runStart - 1
                while before >= start, scalars[before] == 0x20 { before -= 1 }
                let attached = before >= start && isAlphanumeric(scalars[before])
                if isHeldout {
                    let len = i - runStart
                    if !attached, len >= 4, len <= 30, heldout.count < heldoutLimit {
                        var s = String.UnicodeScalarView()
                        for k in runStart..<i { s.append(Unicode.Scalar(scalars[k])!) }
                        heldout.append(String(s))
                    }
                } else {
                    countRun(runStart, i, startsSentence: !attached)
                }
                runStart = -1
            }
            i += 1
        }
    }

    private func isAlphanumeric(_ c: UInt32) -> Bool {
        switch c {
        case 0x30...0x39, 0x41...0x5A, 0x61...0x7A, 0xFF10...0xFF19, 0xFF21...0xFF3A, 0xFF41...0xFF5A: return true
        default: return false
        }
    }

    private func countRun(_ s: Int, _ e: Int, startsSentence: Bool) {
        let len = e - s
        let negInf = -Float.greatestFiniteMagnitude
        if best.count < len + 1 {
            best = [Float](repeating: negInf, count: len + 1)
            from = [Int32](repeating: 0, count: len + 1)
            fromWord = [Int32](repeating: 0, count: len + 1)
        }
        best[0] = 0
        for k in 1...len { best[k] = negInf }
        for i in 0..<len where best[i] > negInf {
            var node: Int32 = 0
            var j = i
            while j < len, j - i < maxWordLength, let next = trie.child(node, scalars[s + j]) {
                node = next
                j += 1
                let w = trie.terminal[Int(node)]
                if w >= 0 {
                    let score = best[i] + segmentScore[Int(w)]
                    if score > best[j] {
                        best[j] = score
                        from[j] = Int32(i)
                        fromWord[j] = w
                    }
                }
            }
        }
        path.removeAll(keepingCapacity: true)
        var k = len
        while k > 0 {
            path.append(fromWord[k])
            k = Int(from[k])
        }
        var prev: UInt32 = LanguageModel.boundary
        var prev2: UInt32 = LanguageModel.boundary
        var first = true
        for w in path.reversed() {
            let word = UInt32(w)
            unigram[Int(word)] += 1
            if !first || startsSentence { bigram[UInt64(prev) << 32 | UInt64(word), default: 0] += 1 }
            first = false
            if prev2 != LanguageModel.boundary { skip[UInt64(prev2) << 32 | UInt64(word), default: 0] += 1 }
            prev2 = prev
            prev = word
        }
        tokens += UInt64(path.count)
    }
}
