import Foundation

/// 記住使用者手動選過的字詞，下次在相同情況優先選它。
///
/// - 上下文記憶：在「前一個詞是 A」時選了 B，下次 A 後面遇到同音就選 B（加分很大）。
/// - 單詞記憶：選過的詞在任何位置都小幅加分。
public final class UserModel: ScoreAdjuster {
    struct Record: Codable {
        var count: Int
        var lastUsed: Double
    }

    struct Stored: Codable {
        var contextual: [String: Record] = [:]
        var unigram: [String: Record] = [:]
    }

    struct UnigramKey: Hashable {
        let word: UInt32
        let key: ReadingKey
    }

    public static let contextualBonus: Float = 5
    public static let unigramBonus: Float = 0.7

    let lm: LanguageModel
    let fileURL: URL?
    private var stored = Stored()
    private var contextual: [UInt64: Int] = [:]
    private var unigram: [UnigramKey: Int] = [:]
    private lazy var wordIndex: [String: UInt32] = {
        var index: [String: UInt32] = [:]
        index.reserveCapacity(lm.words.count)
        for (i, w) in lm.words.enumerated() { index[w] = UInt32(i) }
        return index
    }()
    private let saveQueue = DispatchQueue(label: "bangbang.usermodel.save")

    public init(lm: LanguageModel, fileURL: URL?) {
        self.lm = lm
        self.fileURL = fileURL
        load()
    }

    public func bonus(prev: UInt32, word: UInt32, key: ReadingKey) -> Float {
        var b: Float = 0
        if !contextual.isEmpty, let c = contextual[UInt64(prev) << 32 | UInt64(word)] {
            b += UserModel.contextualBonus + Float(min(c - 1, 3))
        }
        if !unigram.isEmpty, let c = unigram[UnigramKey(word: word, key: key)] {
            b += UserModel.unigramBonus * Float(min(c, 4))
        }
        return b
    }

    public func learn(prev: UInt32, word: UInt32, key: ReadingKey) {
        let now = Date().timeIntervalSince1970
        contextual[UInt64(prev) << 32 | UInt64(word), default: 0] += 1
        unigram[UnigramKey(word: word, key: key), default: 0] += 1
        let ctxKey = "\(lm.words[Int(prev)])\t\(lm.words[Int(word)])"
        stored.contextual[ctxKey, default: Record(count: 0, lastUsed: now)].count += 1
        stored.contextual[ctxKey]!.lastUsed = now
        let uniKey = "\(lm.words[Int(word)])\t\(key.syllables.map(\.description).joined(separator: " "))"
        stored.unigram[uniKey, default: Record(count: 0, lastUsed: now)].count += 1
        stored.unigram[uniKey]!.lastUsed = now
        save()
    }

    public func reset() {
        stored = Stored()
        contextual = [:]
        unigram = [:]
        save()
    }

    private func load() {
        guard let fileURL, let data = try? Data(contentsOf: fileURL),
              let decoded = try? JSONDecoder().decode(Stored.self, from: data) else { return }
        stored = decoded
        for (k, r) in decoded.contextual {
            let parts = k.split(separator: "\t", omittingEmptySubsequences: false)
            guard parts.count == 2, let p = wordIndex[String(parts[0])], let w = wordIndex[String(parts[1])] else { continue }
            contextual[UInt64(p) << 32 | UInt64(w)] = r.count
        }
        for (k, r) in decoded.unigram {
            let parts = k.split(separator: "\t", omittingEmptySubsequences: false)
            guard parts.count == 2, let w = wordIndex[String(parts[0])],
                  let syllables = Syllable.parseReading(parts[1]) else { continue }
            unigram[UnigramKey(word: w, key: ReadingKey(syllables))] = r.count
        }
    }

    private func save() {
        guard let fileURL else { return }
        let snapshot = stored
        saveQueue.async {
            let encoder = JSONEncoder()
            encoder.outputFormatting = [.prettyPrinted, .sortedKeys]
            guard let data = try? encoder.encode(snapshot) else { return }
            try? FileManager.default.createDirectory(at: fileURL.deletingLastPathComponent(), withIntermediateDirectories: true)
            try? data.write(to: fileURL, options: .atomic)
        }
    }
}
