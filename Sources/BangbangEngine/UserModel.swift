import Foundation

/// 記住使用者手動選過的字詞，下次在相同情況預設選它。
///
/// 選過的字詞依「格」互相競爭：
/// - 上下文記憶：前一個詞相同、讀音相同的算同一格。格內選最多次的一定勝出（不管它是不是常用字），
///   次數一樣時以最近一次選的為準。例如在「愛」後面選過「妳」，下次「愛」後面打 ㄋㄧˇ 就是「妳」；
///   之後改選「你」一次就平手，最近選的是「你」，所以變回「你」。
/// - 單詞記憶：只看讀音的一格，勝出者在任何位置都小幅加分。
/// 每個字詞最多累計 maxCount 次，舊習慣不會累積到改不掉。
///
/// 記憶檔（比照 Windows 版）：
/// - 被別人換掉（例如從另一台電腦複製過來）時，下次開始組字前重新載入；被刪掉就跟著忘掉（見 refresh）。
/// - 讀到壞掉的記憶檔時保留目前的記憶，下次存檔前把壞檔改名成 `.corrupt` 保留，不直接蓋掉。
public final class UserModel: ScoreAdjuster {
    struct Record: Codable {
        var count: Int
        var lastUsed: Double
    }

    /// 記憶檔格式。version 2 起，上下文記錄的鍵是「前一個詞\t詞\t讀音」；
    /// 舊格式（沒有 version）是「前一個詞\t詞」，載入時用單詞記錄補回讀音。
    struct Stored: Codable {
        var version: Int?
        var contextual: [String: Record] = [:]
        var unigram: [String: Record] = [:]
    }

    struct ContextSlot: Hashable {
        let prev: UInt32
        let key: ReadingKey
    }

    struct Pick {
        var count: Int
        var lastUsed: Double
    }

    /// 檔案的版本：檔案編號、大小、修改時間。存檔是寫新檔再取代原檔，所以每次存檔的檔案編號都不同。
    struct FileStamp: Equatable {
        let inode: UInt64
        let size: Int64
        let seconds: Int
        let nanoseconds: Int

        init?(_ url: URL) {
            var info = stat()
            guard stat(url.path, &info) == 0 else { return nil }
            inode = UInt64(info.st_ino)
            size = Int64(info.st_size)
            seconds = Int(info.st_mtimespec.tv_sec)
            nanoseconds = Int(info.st_mtimespec.tv_nsec)
        }
    }

    public static let maxCount = 3
    /// 上下文勝出者比語言模型的首選再多出的分數，確保它一定被選中。
    public static let contextualMargin: Float = 4
    /// 單詞記憶的勝出者每選一次加的分數。
    public static let globalBonusPerPick: Float = 0.8

    let lm: LanguageModel
    let fileURL: URL?
    private var stored = Stored()
    private var contextual: [ContextSlot: [UInt32: Pick]] = [:]
    private var global: [ReadingKey: [UInt32: Pick]] = [:]
    private var contextualBonusCache: [ContextSlot: Float] = [:]
    private let saveQueue = DispatchQueue(label: "bangbang.usermodel.save")
    /// 目前磁碟上的記憶檔是壞的：下次存檔前先改名成 .corrupt 保留。
    private var loadedCorrupt = false
    /// 上次讀進來或自己寫出去的記憶檔版本，和磁碟上的不同就是被別人換掉了。
    /// 存檔在背景佇列執行，所以這兩個欄位用 stateLock 保護。
    private var loadedStamp: FileStamp?
    private var savesInFlight = 0
    private let stateLock = NSLock()

    public init(lm: LanguageModel, fileURL: URL?) {
        self.lm = lm
        self.fileURL = fileURL
        reload()
    }

    public func bonus(prev: UInt32, word: UInt32, key: ReadingKey) -> Float {
        var b: Float = 0
        if !contextual.isEmpty {
            let slot = ContextSlot(prev: prev, key: key)
            if let picks = contextual[slot], UserModel.winner(of: picks) == word {
                b += contextualBonus(slot, winner: word)
            }
        }
        if !global.isEmpty, let picks = global[key], UserModel.winner(of: picks) == word, let pick = picks[word] {
            b += UserModel.globalBonusPerPick * Float(pick.count)
        }
        return b
    }

    public func learn(prev: UInt32, word: UInt32, key: ReadingKey, at time: Double = Date().timeIntervalSince1970) {
        // 記憶檔被換掉或刪掉的話，先以檔案為準，再加上這次的選字。
        refresh()
        let slot = ContextSlot(prev: prev, key: key)
        let contextPick = UserModel.add(word, to: &contextual[slot, default: [:]], at: time)
        let globalPick = UserModel.add(word, to: &global[key, default: [:]], at: time)
        contextualBonusCache = [:]

        let reading = UserModel.readingText(key)
        let text = lm.words[Int(word)]
        stored.version = 2
        stored.contextual["\(lm.words[Int(prev)])\t\(text)\t\(reading)"] = Record(count: contextPick.count, lastUsed: time)
        stored.unigram["\(text)\t\(reading)"] = Record(count: globalPick.count, lastUsed: time)
        save()
    }

    public func reset() {
        clearMemory()
        save()
    }

    /// 記憶檔被別人換掉（例如從另一台電腦複製過來）或刪掉時，重新載入；刪掉就跟著忘掉。
    /// 開始組字前呼叫。檔案沒變時只是一次 stat，很便宜。回傳有沒有重新載入。
    @discardableResult
    public func refresh() -> Bool {
        guard let fileURL else { return false }
        stateLock.lock()
        let busy = savesInFlight > 0
        let known = loadedStamp
        stateLock.unlock()
        // 自己的存檔還在寫：寫完會記下新的版本，這時不用檢查。
        if busy || FileStamp(fileURL) == known { return false }
        reload()
        return true
    }

    /// 等背景的存檔寫完（測試與工具用）。
    public func waitUntilSaved() {
        saveQueue.sync {}
    }

    /// 一格裡選最多次的勝出；次數一樣時，最近選的勝出。
    static func winner(of picks: [UInt32: Pick]) -> UInt32? {
        picks.max { a, b in (a.value.count, a.value.lastUsed) < (b.value.count, b.value.lastUsed) }?.key
    }

    private static func add(_ word: UInt32, to picks: inout [UInt32: Pick], at time: Double) -> Pick {
        var pick = picks[word] ?? Pick(count: 0, lastUsed: time)
        pick.count = min(maxCount, pick.count + 1)
        pick.lastUsed = time
        picks[word] = pick
        return pick
    }

    private static func readingText(_ key: ReadingKey) -> String {
        key.syllables.map(\.description).joined(separator: " ")
    }

    /// 讓勝出者剛好贏過語言模型在這一格的首選，再多 contextualMargin 分。
    private func contextualBonus(_ slot: ContextSlot, winner: UInt32) -> Float {
        if let cached = contextualBonusCache[slot] { return cached }
        var top = -Float.greatestFiniteMagnitude
        var mine = -Float.greatestFiniteMagnitude
        for e in lm.entries(for: slot.key) {
            let s = lm.logProb(slot.prev, e.word) + e.readingWeight
            top = max(top, s)
            if e.word == winner { mine = s }
        }
        let value = (mine > -Float.greatestFiniteMagnitude ? max(0, top - mine) : 0) + UserModel.contextualMargin
        contextualBonusCache[slot] = value
        return value
    }

    private func clearMemory() {
        stored = Stored()
        contextual = [:]
        global = [:]
        contextualBonusCache = [:]
    }

    private func setLoadedStamp(_ stamp: FileStamp?) {
        stateLock.lock()
        loadedStamp = stamp
        stateLock.unlock()
    }

    /// 讀記憶檔。檔案不見了就清空記憶；壞掉就保留目前的記憶（見 loadedCorrupt）。
    private func reload() {
        guard let fileURL else { return }
        guard let stamp = FileStamp(fileURL) else {
            // 被刪掉了（例如使用者想清除記憶）：跟著忘掉。
            clearMemory()
            loadedCorrupt = false
            setLoadedStamp(nil)
            return
        }
        // 讀不到（例如權限）時什麼都不改，下次再試。
        guard let data = try? Data(contentsOf: fileURL) else { return }
        guard let decoded = try? JSONDecoder().decode(Stored.self, from: data) else {
            // 壞檔也記下版本：檔案沒再變之前不用每次重讀。
            loadedCorrupt = true
            setLoadedStamp(stamp)
            return
        }
        loadedCorrupt = false
        load(decoded)
        setLoadedStamp(stamp)
    }

    private func load(_ decoded: Stored) {
        clearMemory()
        var converted = Stored(version: 2)
        // 文字 → 編號的索引只在載入時用，用完就釋放。
        var wordIndex: [String: UInt32] = [:]
        wordIndex.reserveCapacity(lm.words.count)
        for (i, w) in lm.words.enumerated() { wordIndex[w] = UInt32(i) }

        var readingsOfWord: [String: [String]] = [:]
        for (k, r) in decoded.unigram {
            let parts = k.split(separator: "\t", omittingEmptySubsequences: false).map(String.init)
            guard parts.count == 2, let w = wordIndex[parts[0]], let syllables = Syllable.parseReading(parts[1]) else { continue }
            let pick = Pick(count: min(UserModel.maxCount, r.count), lastUsed: r.lastUsed)
            global[ReadingKey(syllables), default: [:]][w] = pick
            converted.unigram[k] = Record(count: pick.count, lastUsed: pick.lastUsed)
            readingsOfWord[parts[0], default: []].append(parts[1])
        }
        for (k, r) in decoded.contextual {
            let parts = k.split(separator: "\t", omittingEmptySubsequences: false).map(String.init)
            guard parts.count >= 2, let p = wordIndex[parts[0]], let w = wordIndex[parts[1]] else { continue }
            // 舊格式沒有讀音：用同一個詞的單詞記錄補上。
            let readings = parts.count == 3 ? [parts[2]] : readingsOfWord[parts[1]] ?? []
            for reading in readings {
                guard let syllables = Syllable.parseReading(reading) else { continue }
                let pick = Pick(count: min(UserModel.maxCount, r.count), lastUsed: r.lastUsed)
                contextual[ContextSlot(prev: p, key: ReadingKey(syllables)), default: [:]][w] = pick
                converted.contextual["\(parts[0])\t\(parts[1])\t\(reading)"] = Record(count: pick.count, lastUsed: pick.lastUsed)
            }
        }
        stored = converted
    }

    private func save() {
        guard let fileURL else { return }
        let snapshot = stored
        let keepCorruptFile = loadedCorrupt
        loadedCorrupt = false
        stateLock.lock()
        savesInFlight += 1
        stateLock.unlock()
        saveQueue.async { [self] in
            let fm = FileManager.default
            let encoder = JSONEncoder()
            encoder.outputFormatting = [.prettyPrinted, .sortedKeys]
            if let data = try? encoder.encode(snapshot) {
                try? fm.createDirectory(at: fileURL.deletingLastPathComponent(), withIntermediateDirectories: true)
                if keepCorruptFile {
                    // 壞掉的記憶檔（例如手動改到一半）改名保留，不直接蓋掉。
                    let aside = fileURL.appendingPathExtension("corrupt")
                    try? fm.removeItem(at: aside)
                    try? fm.moveItem(at: fileURL, to: aside)
                }
                // .atomic 是寫暫存檔再取代原檔：讀的人一定讀到完整的某一版。
                try? data.write(to: fileURL, options: .atomic)
            }
            let written = FileStamp(fileURL)
            stateLock.lock()
            loadedStamp = written
            savesInFlight -= 1
            stateLock.unlock()
        }
    }
}
