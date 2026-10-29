import Foundation

/// 組字區裡的一格：一個注音音節，或是一個直接輸入的字元（英文字母、標點）。
public enum Token: Equatable {
    case syllable(Syllable)
    case literal(String)

    public var syllable: Syllable? {
        if case .syllable(let s) = self { return s }
        return nil
    }
}

/// 選字結果中的一段：從第 start 格開始、佔 length 格的一個詞（或字元）。
public struct WalkNode: Equatable {
    public let start: Int
    public let length: Int
    /// 詞庫裡的詞；直接輸入的字元為 nil。
    public let word: UInt32?
    public let text: String
    public var end: Int { start + length }
}

/// 使用者手動選定的詞，重新選字時必須保留。
public struct Pin: Equatable {
    public var start: Int
    public var length: Int
    public var word: UInt32
    public var end: Int { start + length }
}

/// 候選字詞。
public struct Candidate: Equatable {
    public let start: Int
    public let length: Int
    public let word: UInt32
    public let text: String
}

/// 讓使用者習慣影響選字的介面（見 UserModel）。
public protocol ScoreAdjuster: AnyObject {
    func bonus(prev: UInt32, word: UInt32, key: ReadingKey) -> Float
}

/// 整句選字：在所有可能的斷詞組合中，找出語言模型分數最高的一條路徑（Viterbi）。
public struct Decoder {
    public let lm: LanguageModel
    public weak var adjuster: ScoreAdjuster?
    /// 每個讀音只拿前幾名候選詞參與整句計算，避免太慢；候選清單仍會列出全部。
    public var singleCharBeam = 40
    public var phraseBeam = 12

    public init(lm: LanguageModel, adjuster: ScoreAdjuster? = nil) {
        self.lm = lm
        self.adjuster = adjuster
    }

    struct LatticeNode {
        let start: Int
        let length: Int
        let word: UInt32?
        let literal: String?
        let key: ReadingKey?
        let weight: Float
        var end: Int { start + length }
    }

    /// 每個節點保留幾種「前一個詞」的狀態（二階搜尋的寬度）。
    public var statesPerNode = 6

    /// 搜尋狀態：走到某節點、且前一個詞是 prevWord 時的最佳分數。
    struct State {
        var score: Float
        var prevWord: UInt32
        var backNode: Int32
        var backState: Int32
    }

    public func decode(_ tokens: [Token], pins: [Pin] = [], context: UInt32 = LanguageModel.boundary) -> [WalkNode] {
        let n = tokens.count
        guard n > 0 else { return [] }
        let nodes = buildLattice(tokens, pins: pins)
        var endsAt = [[Int]](repeating: [], count: n + 1)
        for (i, node) in nodes.enumerated() { endsAt[node.end].append(i) }

        // 二階搜尋：分數同時考慮前一個詞（雙連詞）與前前一個詞（隔詞搭配）。
        // nodes 已依起點排序，處理到某節點時，所有結束在它起點的前驅都已算完。
        var states = [[State]](repeating: [], count: nodes.count)
        var candidates: [State] = []
        for (j, node) in nodes.enumerated() {
            if node.start == 0 {
                states[j] = [State(score: score(node, prev: context, prev2: LanguageModel.boundary),
                                   prevWord: context, backNode: -1, backState: -1)]
                continue
            }
            candidates.removeAll(keepingCapacity: true)
            for m in endsAt[node.start] where !states[m].isEmpty {
                let prevNode = nodes[m]
                // 直接輸入的字元（標點、英文）視為句子邊界。
                let prevWord = prevNode.word ?? LanguageModel.boundary
                var bestScore = -Float.greatestFiniteMagnitude
                var bestIndex = 0
                for (k, st) in states[m].enumerated() {
                    let prev2 = prevNode.word == nil ? LanguageModel.boundary : st.prevWord
                    let s = st.score + (node.word.map { lm.skip(prev2, $0) } ?? 0)
                    if s > bestScore {
                        bestScore = s
                        bestIndex = k
                    }
                }
                candidates.append(State(score: bestScore + score(node, prev: prevWord, prev2: LanguageModel.boundary),
                                        prevWord: prevWord, backNode: Int32(m), backState: Int32(bestIndex)))
            }
            if node.word == nil {
                // 標點之後重新開始，只需保留最好的一條。
                if let top = candidates.max(by: { $0.score < $1.score }) { states[j] = [top] }
            } else {
                candidates.sort { $0.score > $1.score }
                states[j] = Array(candidates.prefix(statesPerNode))
            }
        }

        var lastNode = -1
        var lastScore = -Float.greatestFiniteMagnitude
        for m in endsAt[n] {
            if let top = states[m].first, top.score > lastScore {
                lastScore = top.score
                lastNode = m
            }
        }
        guard lastNode >= 0 else { return fallbackWalk(tokens) }
        var path: [Int] = []
        var cur = lastNode
        var st = 0
        while cur >= 0 {
            path.append(cur)
            let s = states[cur][st]
            cur = Int(s.backNode)
            st = Int(s.backState)
        }
        return path.reversed().map { i in
            let node = nodes[i]
            return WalkNode(start: node.start, length: node.length, word: node.word,
                            text: node.literal ?? lm.words[Int(node.word!)])
        }
    }

    private func score(_ node: LatticeNode, prev: UInt32, prev2: UInt32) -> Float {
        guard let word = node.word, let key = node.key else { return 0 }
        var s = lm.logProb(prev, word) + node.weight + lm.skip(prev2, word)
        if let adjuster { s += adjuster.bonus(prev: prev, word: word, key: key) }
        return s
    }

    func buildLattice(_ tokens: [Token], pins: [Pin]) -> [LatticeNode] {
        let n = tokens.count
        var nodes: [LatticeNode] = []
        func allowed(_ start: Int, _ length: Int, _ word: UInt32?) -> Bool {
            for p in pins where start < p.end && p.start < start + length {
                if !(p.start == start && p.length == length && p.word == word) { return false }
            }
            return true
        }
        for i in 0..<n {
            if case .literal(let text) = tokens[i] {
                nodes.append(LatticeNode(start: i, length: 1, word: nil, literal: text, key: nil, weight: 0))
                continue
            }
            var syllables: [UInt16] = []
            for len in 1...min(lm.maxPhraseLength, n - i) {
                guard let s = tokens[i + len - 1].syllable else { break }
                syllables.append(s.code)
                let key = ReadingKey(syllables)
                let entries = lm.entries(for: key)
                if entries.isEmpty { continue }
                let beam = len == 1 ? singleCharBeam : phraseBeam
                var taken = 0
                for e in entries where taken < beam {
                    guard allowed(i, len, e.word) else { continue }
                    nodes.append(LatticeNode(start: i, length: len, word: e.word, literal: nil, key: key, weight: e.readingWeight))
                    taken += 1
                }
                // 被釘選的詞就算不在前幾名也要放進去。
                for p in pins where p.start == i && p.length == len {
                    if !nodes.contains(where: { $0.start == i && $0.length == len && $0.word == p.word }),
                       let e = entries.first(where: { $0.word == p.word }) {
                        nodes.append(LatticeNode(start: i, length: len, word: e.word, literal: nil, key: key, weight: e.readingWeight))
                    }
                }
            }
        }
        return nodes
    }

    /// 理論上不會發生（每個音節都至少有一個字）；保險起見逐字取第一名。
    private func fallbackWalk(_ tokens: [Token]) -> [WalkNode] {
        tokens.enumerated().map { i, token in
            switch token {
            case .literal(let text):
                return WalkNode(start: i, length: 1, word: nil, text: text)
            case .syllable(let s):
                let e = lm.entries(for: ReadingKey([s.code])).first
                return WalkNode(start: i, length: 1, word: e?.word, text: e.map { lm.words[Int($0.word)] } ?? "?")
            }
        }
    }

    /// 候選清單。游標在字前面時列出「從游標開始」的詞；游標在最後時列出「結束在最後」的詞。
    /// 長詞在前，同長度依上下文分數排序。
    public func candidates(_ tokens: [Token], cursor: Int, walk: [WalkNode]) -> [Candidate] {
        let n = tokens.count
        guard n > 0 else { return [] }
        var spans: [(start: Int, length: Int)] = []
        if cursor < n {
            guard tokens[cursor].syllable != nil else { return [] }
            var len = 0
            while len < lm.maxPhraseLength, cursor + len < n, tokens[cursor + len].syllable != nil { len += 1 }
            for l in stride(from: len, through: 1, by: -1) { spans.append((cursor, l)) }
        } else {
            guard tokens[n - 1].syllable != nil else { return [] }
            var len = 0
            while len < lm.maxPhraseLength, n - 1 - len >= 0, tokens[n - 1 - len].syllable != nil { len += 1 }
            for l in stride(from: len, through: 1, by: -1) { spans.append((n - l, l)) }
        }
        var result: [Candidate] = []
        for span in spans {
            let key = ReadingKey(tokens[span.start..<(span.start + span.length)].compactMap { $0.syllable?.code })
            let entries = lm.entries(for: key)
            if entries.isEmpty { continue }
            let prev = walk.last(where: { $0.end <= span.start }).flatMap { $0.end == span.start ? ($0.word ?? LanguageModel.boundary) : nil }
                ?? LanguageModel.boundary
            let ranked = entries.map { e -> (LexiconEntry, Float) in
                var s = lm.logProb(prev, e.word) + e.readingWeight
                if let adjuster { s += adjuster.bonus(prev: prev, word: e.word, key: key) }
                return (e, s)
            }.sorted { $0.1 > $1.1 }
            for (e, _) in ranked {
                result.append(Candidate(start: span.start, length: span.length, word: e.word, text: lm.words[Int(e.word)]))
            }
        }
        return result
    }
}
