import Foundation

/// 多個音節組成的讀音，作為詞庫查詢的鍵。
public struct ReadingKey: Hashable {
    public let codes: [UInt16]
    public init(_ codes: [UInt16]) { self.codes = codes }
    public init(_ syllables: [Syllable]) { self.codes = syllables.map(\.code) }
    public var syllables: [Syllable] { codes.map(Syllable.init(code:)) }
}

/// 某個讀音下的一個候選詞。
public struct LexiconEntry: Equatable {
    public let word: UInt32
    /// log P(讀音 | 詞)：破音字的少用讀音會是負值，一般為 0。
    public let readingWeight: Float
    public init(word: UInt32, readingWeight: Float) {
        self.word = word
        self.readingWeight = readingWeight
    }
}

/// 雙連詞（bigram）語言模型加上注音詞庫。
///
/// 分數都是自然對數機率。P(w | v) 有統計到就直接查表，否則退回 λ(v)·P(w)。
public final class LanguageModel {
    /// 0 號詞是「句子邊界」，句首與標點之後都以它當前一個詞。
    public static let boundary: UInt32 = 0
    public static let boundaryText = "<s>"

    public let words: [String]
    public let logUnigram: [Float]
    public let logBackoff: [Float]
    let bigramStart: [UInt32]
    let bigramNext: [UInt32]
    let bigramLogProb: [Float]
    public let lexicon: [ReadingKey: [LexiconEntry]]
    public let maxPhraseLength: Int
    /// 隔一個詞的搭配加分，鍵為 (前前詞 << 32 | 詞)。例如「越 ＿ 越」。
    public let skipBoost: [UInt64: Float]

    public init(words: [String], logUnigram: [Float], logBackoff: [Float],
                bigramStart: [UInt32], bigramNext: [UInt32], bigramLogProb: [Float],
                lexicon: [ReadingKey: [LexiconEntry]], skipBoost: [UInt64: Float] = [:]) {
        self.words = words
        self.skipBoost = skipBoost
        self.logUnigram = logUnigram
        self.logBackoff = logBackoff
        self.bigramStart = bigramStart
        self.bigramNext = bigramNext
        self.bigramLogProb = bigramLogProb
        // 每個讀音的候選詞依「單詞機率 + 讀音權重」由高到低排好，解碼時可以只取前幾名。
        var sorted: [ReadingKey: [LexiconEntry]] = [:]
        sorted.reserveCapacity(lexicon.count)
        var maxLen = 1
        for (key, entries) in lexicon {
            sorted[key] = entries.sorted {
                logUnigram[Int($0.word)] + $0.readingWeight > logUnigram[Int($1.word)] + $1.readingWeight
            }
            maxLen = max(maxLen, key.codes.count)
        }
        self.lexicon = sorted
        self.maxPhraseLength = maxLen
    }

    public var bigramCount: Int { bigramNext.count }

    public func entries(for key: ReadingKey) -> [LexiconEntry] { lexicon[key] ?? [] }

    public func hasReading(_ syllable: Syllable) -> Bool { lexicon[ReadingKey([syllable.code])] != nil }

    /// 有統計到 (prev, word) 時回傳其對數機率。
    public func storedBigram(_ prev: UInt32, _ word: UInt32) -> Float? {
        var lo = Int(bigramStart[Int(prev)])
        var hi = Int(bigramStart[Int(prev) + 1])
        while lo < hi {
            let mid = (lo + hi) >> 1
            let w = bigramNext[mid]
            if w == word { return bigramLogProb[mid] }
            if w < word { lo = mid + 1 } else { hi = mid }
        }
        return nil
    }

    /// log P(word | prev)
    public func logProb(_ prev: UInt32, _ word: UInt32) -> Float {
        storedBigram(prev, word) ?? (logBackoff[Int(prev)] + logUnigram[Int(word)])
    }

    /// 前前一個詞是 prev2 時，word 的額外加分。
    @inline(__always)
    public func skip(_ prev2: UInt32, _ word: UInt32) -> Float {
        if prev2 == LanguageModel.boundary || skipBoost.isEmpty { return 0 }
        return skipBoost[UInt64(prev2) << 32 | UInt64(word)] ?? 0
    }
}

// MARK: - 二進位檔讀寫

extension LanguageModel {
    static let magic: UInt32 = 0x4D4C_4242  // "BBLM"
    static let version: UInt32 = 2

    public func write(to url: URL) throws {
        var w = BinaryWriter()
        w.append(LanguageModel.magic)
        w.append(LanguageModel.version)

        var blob = [UInt8]()
        var offsets: [UInt32] = [0]
        for word in words {
            blob.append(contentsOf: Array(word.utf8))
            offsets.append(UInt32(blob.count))
        }
        w.append(UInt32(words.count))
        w.appendArray(offsets)
        w.append(UInt32(blob.count))
        w.appendArray(blob)
        w.appendArray(logUnigram)
        w.appendArray(logBackoff)
        w.append(UInt32(bigramNext.count))
        w.appendArray(bigramStart)
        w.appendArray(bigramNext)
        w.appendArray(bigramLogProb)

        let keys = lexicon.keys.sorted { $0.codes.lexicographicallyPrecedes($1.codes) }
        var codeStart: [UInt32] = [0]
        var codes: [UInt16] = []
        var entryStart: [UInt32] = [0]
        var entryWord: [UInt32] = []
        var entryWeight: [Float] = []
        for key in keys {
            codes.append(contentsOf: key.codes)
            codeStart.append(UInt32(codes.count))
            for e in lexicon[key]! {
                entryWord.append(e.word)
                entryWeight.append(e.readingWeight)
            }
            entryStart.append(UInt32(entryWord.count))
        }
        w.append(UInt32(keys.count))
        w.appendArray(codeStart)
        w.append(UInt32(codes.count))
        w.appendArray(codes)
        w.appendArray(entryStart)
        w.append(UInt32(entryWord.count))
        w.appendArray(entryWord)
        w.appendArray(entryWeight)

        let skipKeys = skipBoost.keys.sorted()
        w.append(UInt32(skipKeys.count))
        w.appendArray(skipKeys)
        w.appendArray(skipKeys.map { skipBoost[$0]! })
        try Data(w.bytes).write(to: url)
    }

    public static func load(from url: URL) throws -> LanguageModel {
        let data = try Data(contentsOf: url, options: .alwaysMapped)
        var r = BinaryReader(data: data)
        guard try r.read(UInt32.self) == magic, try r.read(UInt32.self) == version else {
            throw ModelError.badFormat
        }
        let wordCount = Int(try r.read(UInt32.self))
        let offsets: [UInt32] = try r.readArray(count: wordCount + 1)
        let blobCount = Int(try r.read(UInt32.self))
        let blob: [UInt8] = try r.readArray(count: blobCount)
        var words: [String] = []
        words.reserveCapacity(wordCount)
        blob.withUnsafeBufferPointer { buf in
            for i in 0..<wordCount {
                let slice = UnsafeBufferPointer(rebasing: buf[Int(offsets[i])..<Int(offsets[i + 1])])
                words.append(String(decoding: slice, as: UTF8.self))
            }
        }
        let logUnigram: [Float] = try r.readArray(count: wordCount)
        let logBackoff: [Float] = try r.readArray(count: wordCount)
        let bigramCount = Int(try r.read(UInt32.self))
        let bigramStart: [UInt32] = try r.readArray(count: wordCount + 1)
        let bigramNext: [UInt32] = try r.readArray(count: bigramCount)
        let bigramLogProb: [Float] = try r.readArray(count: bigramCount)

        let keyCount = Int(try r.read(UInt32.self))
        let codeStart: [UInt32] = try r.readArray(count: keyCount + 1)
        let codeCount = Int(try r.read(UInt32.self))
        let codes: [UInt16] = try r.readArray(count: codeCount)
        let entryStart: [UInt32] = try r.readArray(count: keyCount + 1)
        let entryCount = Int(try r.read(UInt32.self))
        let entryWord: [UInt32] = try r.readArray(count: entryCount)
        let entryWeight: [Float] = try r.readArray(count: entryCount)
        var lexicon: [ReadingKey: [LexiconEntry]] = [:]
        lexicon.reserveCapacity(keyCount)
        for k in 0..<keyCount {
            let key = ReadingKey(Array(codes[Int(codeStart[k])..<Int(codeStart[k + 1])]))
            var entries: [LexiconEntry] = []
            for e in Int(entryStart[k])..<Int(entryStart[k + 1]) {
                entries.append(LexiconEntry(word: entryWord[e], readingWeight: entryWeight[e]))
            }
            lexicon[key] = entries
        }
        let skipCount = Int(try r.read(UInt32.self))
        let skipKeys: [UInt64] = try r.readArray(count: skipCount)
        let skipValues: [Float] = try r.readArray(count: skipCount)
        let skipBoost = Dictionary(uniqueKeysWithValues: zip(skipKeys, skipValues))
        return LanguageModel(words: words, logUnigram: logUnigram, logBackoff: logBackoff,
                             bigramStart: bigramStart, bigramNext: bigramNext, bigramLogProb: bigramLogProb,
                             lexicon: lexicon, skipBoost: skipBoost)
    }

    public enum ModelError: Error { case badFormat, truncated }
}

struct BinaryWriter {
    var bytes: [UInt8] = []

    mutating func append<T: FixedWidthInteger>(_ value: T) {
        withUnsafeBytes(of: value.littleEndian) { bytes.append(contentsOf: $0) }
    }

    mutating func appendArray<T>(_ values: [T]) {
        values.withUnsafeBytes { bytes.append(contentsOf: $0) }
    }
}

struct BinaryReader {
    let data: Data
    var offset = 0

    init(data: Data) { self.data = data }

    mutating func read<T: FixedWidthInteger>(_: T.Type) throws -> T {
        let size = MemoryLayout<T>.size
        guard offset + size <= data.count else { throw LanguageModel.ModelError.truncated }
        let value = data.withUnsafeBytes { $0.loadUnaligned(fromByteOffset: offset, as: T.self) }
        offset += size
        return T(littleEndian: value)
    }

    mutating func readArray<T>(count: Int) throws -> [T] {
        let size = MemoryLayout<T>.stride * count
        guard offset + size <= data.count else { throw LanguageModel.ModelError.truncated }
        let result = [T](unsafeUninitializedCapacity: count) { buf, initialized in
            data.withUnsafeBytes { raw in
                let src = UnsafeRawBufferPointer(rebasing: raw[offset..<(offset + size)])
                UnsafeMutableRawBufferPointer(buf).copyMemory(from: src)
            }
            initialized = count
        }
        offset += size
        return result
    }
}
