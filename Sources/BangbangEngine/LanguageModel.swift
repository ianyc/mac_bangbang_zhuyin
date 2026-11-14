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
///
/// 模型檔用 mmap 直接對應到記憶體、不複製，所有表格都以二分搜尋查詢。對應的頁面是可隨時
/// 釋放的檔案快取，不算進程式的記憶體用量（0.2.1 以前是整份複製進記憶體，約 141 MB）。
public final class LanguageModel {
    /// 0 號詞是「句子邊界」，句首與標點之後都以它當前一個詞。
    public static let boundary: UInt32 = 0
    public static let boundaryText = "<s>"

    private let storage: Storage
    public let words: WordTable
    public let logUnigram: UnsafeBufferPointer<Float>
    public let logBackoff: UnsafeBufferPointer<Float>
    let bigramStart: UnsafeBufferPointer<UInt32>
    let bigramNext: UnsafeBufferPointer<UInt32>
    let bigramLogProb: UnsafeBufferPointer<Float>
    /// 讀音表：依音節序列排序，keyCodeStart／entryStart 標出每個讀音的音節與候選詞範圍。
    let keyCodeStart: UnsafeBufferPointer<UInt32>
    let keyCodes: UnsafeBufferPointer<UInt16>
    let entryStart: UnsafeBufferPointer<UInt32>
    let entryWord: UnsafeBufferPointer<UInt32>
    let entryWeight: UnsafeBufferPointer<Float>
    /// 隔一個詞的搭配加分（例如「越 ＿ 越」），跟雙連詞一樣依前前一個詞分段：
    /// skipStart[前前詞]..<skipStart[前前詞 + 1] 是它的範圍，段內依詞排序。
    let skipStart: UnsafeBufferPointer<UInt32>
    let skipNext: UnsafeBufferPointer<UInt32>
    let skipValues: UnsafeBufferPointer<Float>
    public let maxPhraseLength: Int

    private init(storage: Storage, words: WordTable, logUnigram: UnsafeBufferPointer<Float>, logBackoff: UnsafeBufferPointer<Float>,
                 bigramStart: UnsafeBufferPointer<UInt32>, bigramNext: UnsafeBufferPointer<UInt32>, bigramLogProb: UnsafeBufferPointer<Float>,
                 keyCodeStart: UnsafeBufferPointer<UInt32>, keyCodes: UnsafeBufferPointer<UInt16>,
                 entryStart: UnsafeBufferPointer<UInt32>, entryWord: UnsafeBufferPointer<UInt32>, entryWeight: UnsafeBufferPointer<Float>,
                 skipStart: UnsafeBufferPointer<UInt32>, skipNext: UnsafeBufferPointer<UInt32>, skipValues: UnsafeBufferPointer<Float>,
                 maxPhraseLength: Int) throws {
        self.storage = storage
        self.words = words
        self.logUnigram = logUnigram
        self.logBackoff = logBackoff
        self.bigramStart = bigramStart
        self.bigramNext = bigramNext
        self.bigramLogProb = bigramLogProb
        self.keyCodeStart = keyCodeStart
        self.keyCodes = keyCodes
        self.entryStart = entryStart
        self.entryWord = entryWord
        self.entryWeight = entryWeight
        self.skipStart = skipStart
        self.skipNext = skipNext
        self.skipValues = skipValues
        self.maxPhraseLength = maxPhraseLength
    }

    /// 從記憶體中的資料建立（建模型、測試用）。
    public convenience init(words: [String], logUnigram: [Float], logBackoff: [Float],
                            bigramStart: [UInt32], bigramNext: [UInt32], bigramLogProb: [Float],
                            lexicon: [ReadingKey: [LexiconEntry]], skipBoost: [UInt64: Float] = [:]) {
        let bytes = LanguageModel.serialize(words: words, logUnigram: logUnigram, logBackoff: logBackoff,
                                            bigramStart: bigramStart, bigramNext: bigramNext, bigramLogProb: bigramLogProb,
                                            lexicon: lexicon, skipBoost: skipBoost)
        try! self.init(storage: Storage(bytes: bytes))
    }

    public static func load(from url: URL) throws -> LanguageModel {
        try LanguageModel(storage: Storage(mapping: url))
    }

    public func write(to url: URL) throws {
        try Data(bytes: storage.base, count: storage.size).write(to: url)
    }

    public var bigramCount: Int { bigramNext.count }

    /// 某個讀音的候選詞（直接讀模型檔裡的那一段，不複製）。
    public func entries(for key: ReadingKey) -> LexiconEntries {
        lookup(key.codes).entries
    }

    /// 查讀音，同時回答「有沒有以它開頭的更長讀音」，讓整句選字可以提早停止往後延伸。
    public func lookup(_ codes: [UInt16]) -> (entries: LexiconEntries, hasLonger: Bool) {
        let count = keyCodeStart.count - 1
        var lo = 0, hi = count
        while lo < hi {
            let mid = (lo + hi) >> 1
            if compare(key: mid, codes) < 0 { lo = mid + 1 } else { hi = mid }
        }
        // lo 是第一個 ≥ codes 的讀音；完全相同時就是它，更長的同字首讀音緊接在後。
        var entries = LexiconEntries(word: entryWord, weight: entryWeight, range: 0..<0)
        var next = lo
        if lo < count, compare(key: lo, codes) == 0 {
            entries = LexiconEntries(word: entryWord, weight: entryWeight, range: Int(entryStart[lo])..<Int(entryStart[lo + 1]))
            next = lo + 1
        }
        return (entries, next < count && hasPrefix(key: next, codes))
    }

    public func hasReading(_ syllable: Syllable) -> Bool { !lookup([syllable.code]).entries.isEmpty }

    /// 所有讀音與候選詞（工具與測試用；會建立完整的表，輸入法本身不要呼叫）。
    public var lexicon: [ReadingKey: [LexiconEntry]] {
        var result: [ReadingKey: [LexiconEntry]] = [:]
        for k in 0..<(keyCodeStart.count - 1) {
            let key = ReadingKey(Array(keyCodes[Int(keyCodeStart[k])..<Int(keyCodeStart[k + 1])]))
            result[key] = Array(LexiconEntries(word: entryWord, weight: entryWeight,
                                               range: Int(entryStart[k])..<Int(entryStart[k + 1])))
        }
        return result
    }

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
        if prev2 == LanguageModel.boundary { return 0 }
        var lo = Int(skipStart[Int(prev2)])
        var hi = Int(skipStart[Int(prev2) + 1])
        while lo < hi {
            let mid = (lo + hi) >> 1
            let w = skipNext[mid]
            if w == word { return skipValues[mid] }
            if w < word { lo = mid + 1 } else { hi = mid }
        }
        return 0
    }

    private func hasPrefix(key k: Int, _ codes: [UInt16]) -> Bool {
        let start = Int(keyCodeStart[k]), end = Int(keyCodeStart[k + 1])
        guard end - start > codes.count else { return false }
        for (i, c) in codes.enumerated() where keyCodes[start + i] != c { return false }
        return true
    }

    private func compare(key k: Int, _ codes: [UInt16]) -> Int {
        let start = Int(keyCodeStart[k]), end = Int(keyCodeStart[k + 1])
        for (i, c) in codes.enumerated() {
            guard start + i < end else { return -1 }
            let mine = keyCodes[start + i]
            if mine != c { return mine < c ? -1 : 1 }
        }
        return end - start == codes.count ? 0 : 1
    }
}

/// 某個讀音的候選詞清單，直接指向模型檔裡的資料。
public struct LexiconEntries: RandomAccessCollection {
    let word: UnsafeBufferPointer<UInt32>
    let weight: UnsafeBufferPointer<Float>
    let range: Range<Int>

    public var startIndex: Int { range.lowerBound }
    public var endIndex: Int { range.upperBound }

    public subscript(i: Int) -> LexiconEntry { LexiconEntry(word: word[i], readingWeight: weight[i]) }
}

/// 詞表：依編號取出詞的文字，每次從模型檔解碼，不常駐記憶體。
public struct WordTable: RandomAccessCollection {
    let offsets: UnsafeBufferPointer<UInt32>
    let blob: UnsafeBufferPointer<UInt8>

    public var startIndex: Int { 0 }
    public var endIndex: Int { offsets.count - 1 }

    public subscript(i: Int) -> String {
        String(decoding: UnsafeBufferPointer(rebasing: blob[Int(offsets[i])..<Int(offsets[i + 1])]), as: UTF8.self)
    }
}

// MARK: - 模型檔格式

/// 第 3 版格式：固定長度的檔頭，接著各個表格依序排列，每個表格都對齊 8 bytes，
/// 讓 mmap 之後可以直接當成陣列讀取。
///
/// 檔頭（UInt32 × 10）：magic、版本、詞數、雙連詞數、讀音數、音節數、候選詞數、隔詞搭配數、詞文字長度、最長詞長。
/// 表格：詞文字位置、詞文字、單詞機率、退回權重、雙連詞起點、雙連詞下一詞、雙連詞機率、
///       讀音音節起點、讀音音節、讀音候選起點、候選詞、讀音權重、隔詞搭配起點、隔詞搭配詞、隔詞搭配加分。
extension LanguageModel {
    static let magic: UInt32 = 0x4D4C_4242  // "BBLM"
    static let version: UInt32 = 3
    static let headerCount = 10

    public enum ModelError: Error { case badFormat, truncated, cannotOpen }

    private convenience init(storage: Storage) throws {
        var r = SectionReader(storage: storage)
        let header: UnsafeBufferPointer<UInt32> = try r.next(LanguageModel.headerCount)
        guard header[0] == LanguageModel.magic, header[1] == LanguageModel.version else { throw ModelError.badFormat }
        let wordCount = Int(header[2]), bigramCount = Int(header[3]), keyCount = Int(header[4])
        let codeCount = Int(header[5]), entryCount = Int(header[6]), skipCount = Int(header[7])
        let blobCount = Int(header[8]), maxPhraseLength = Int(header[9])
        try self.init(storage: storage,
                      words: WordTable(offsets: r.next(wordCount + 1), blob: r.next(blobCount)),
                      logUnigram: r.next(wordCount), logBackoff: r.next(wordCount),
                      bigramStart: r.next(wordCount + 1), bigramNext: r.next(bigramCount), bigramLogProb: r.next(bigramCount),
                      keyCodeStart: r.next(keyCount + 1), keyCodes: r.next(codeCount),
                      entryStart: r.next(keyCount + 1), entryWord: r.next(entryCount), entryWeight: r.next(entryCount),
                      skipStart: r.next(wordCount + 1), skipNext: r.next(skipCount), skipValues: r.next(skipCount),
                      maxPhraseLength: maxPhraseLength)
    }

    static func serialize(words: [String], logUnigram: [Float], logBackoff: [Float],
                          bigramStart: [UInt32], bigramNext: [UInt32], bigramLogProb: [Float],
                          lexicon: [ReadingKey: [LexiconEntry]], skipBoost: [UInt64: Float]) -> [UInt8] {
        var blob = [UInt8]()
        var offsets: [UInt32] = [0]
        for word in words {
            blob.append(contentsOf: Array(word.utf8))
            offsets.append(UInt32(blob.count))
        }

        // 讀音依音節序列排序；每個讀音的候選詞依「單詞機率 + 讀音權重」由高到低，解碼時只取前幾名。
        let keys = lexicon.keys.sorted { $0.codes.lexicographicallyPrecedes($1.codes) }
        var keyCodeStart: [UInt32] = [0]
        var keyCodes: [UInt16] = []
        var entryStart: [UInt32] = [0]
        var entryWord: [UInt32] = []
        var entryWeight: [Float] = []
        var maxPhraseLength = 1
        for key in keys {
            keyCodes.append(contentsOf: key.codes)
            keyCodeStart.append(UInt32(keyCodes.count))
            maxPhraseLength = max(maxPhraseLength, key.codes.count)
            let sorted = lexicon[key]!.sorted {
                logUnigram[Int($0.word)] + $0.readingWeight > logUnigram[Int($1.word)] + $1.readingWeight
            }
            for e in sorted {
                entryWord.append(e.word)
                entryWeight.append(e.readingWeight)
            }
            entryStart.append(UInt32(entryWord.count))
        }
        let skipKeys = skipBoost.keys.sorted()
        var skipStart = [UInt32](repeating: 0, count: words.count + 1)
        for key in skipKeys { skipStart[Int(key >> 32) + 1] += 1 }
        for i in 1..<skipStart.count { skipStart[i] += skipStart[i - 1] }

        var w = SectionWriter()
        w.section([magic, version, UInt32(words.count), UInt32(bigramNext.count), UInt32(keys.count),
                   UInt32(keyCodes.count), UInt32(entryWord.count), UInt32(skipKeys.count),
                   UInt32(blob.count), UInt32(maxPhraseLength)])
        w.section(offsets)
        w.section(blob)
        w.section(logUnigram)
        w.section(logBackoff)
        w.section(bigramStart)
        w.section(bigramNext)
        w.section(bigramLogProb)
        w.section(keyCodeStart)
        w.section(keyCodes)
        w.section(entryStart)
        w.section(entryWord)
        w.section(entryWeight)
        w.section(skipStart)
        w.section(skipKeys.map { UInt32($0 & 0xFFFF_FFFF) })
        w.section(skipKeys.map { skipBoost[$0]! })
        return w.bytes
    }
}

/// 模型資料所在的記憶體：mmap 對應的檔案，或自己配置的記憶體。物件釋放時一併釋放。
final class Storage {
    let base: UnsafeRawPointer
    let size: Int
    private let release: () -> Void

    init(mapping url: URL) throws {
        let fd = open(url.path, O_RDONLY)
        guard fd >= 0 else { throw LanguageModel.ModelError.cannotOpen }
        defer { close(fd) }
        var info = stat()
        guard fstat(fd, &info) == 0, info.st_size > 0 else { throw LanguageModel.ModelError.cannotOpen }
        let size = Int(info.st_size)
        guard let pointer = mmap(nil, size, PROT_READ, MAP_PRIVATE, fd, 0), pointer != MAP_FAILED else {
            throw LanguageModel.ModelError.cannotOpen
        }
        self.base = UnsafeRawPointer(pointer)
        self.size = size
        self.release = { munmap(pointer, size) }
    }

    init(bytes: [UInt8]) {
        let pointer = UnsafeMutableRawPointer.allocate(byteCount: max(bytes.count, 1), alignment: 16)
        bytes.withUnsafeBytes { pointer.copyMemory(from: $0.baseAddress!, byteCount: bytes.count) }
        self.base = UnsafeRawPointer(pointer)
        self.size = bytes.count
        self.release = { pointer.deallocate() }
    }

    deinit { release() }
}

struct SectionWriter {
    var bytes: [UInt8] = []

    mutating func section<T>(_ values: [T]) {
        while bytes.count % 8 != 0 { bytes.append(0) }
        values.withUnsafeBytes { bytes.append(contentsOf: $0) }
    }
}

struct SectionReader {
    let storage: Storage
    var offset = 0

    mutating func next<T>(_ count: Int) throws -> UnsafeBufferPointer<T> {
        offset = (offset + 7) & ~7
        let byteCount = MemoryLayout<T>.stride * count
        guard offset + byteCount <= storage.size else { throw LanguageModel.ModelError.truncated }
        let pointer = storage.base.advanced(by: offset).bindMemory(to: T.self, capacity: count)
        offset += byteCount
        return UnsafeBufferPointer(start: pointer, count: count)
    }
}
