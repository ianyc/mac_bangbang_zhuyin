import Foundation

/// 一個注音音節：聲母、介音、韻母、聲調。0 代表該位置沒有符號。
public struct Syllable: Hashable, Comparable, CustomStringConvertible {
    public static let consonants: [Character] = Array("ㄅㄆㄇㄈㄉㄊㄋㄌㄍㄎㄏㄐㄑㄒㄓㄔㄕㄖㄗㄘㄙ")
    public static let medials: [Character] = Array("ㄧㄨㄩ")
    public static let rimes: [Character] = Array("ㄚㄛㄜㄝㄞㄟㄠㄡㄢㄣㄤㄥㄦ")
    /// 聲調符號，索引 1...5 對應一聲、二聲、三聲、四聲、輕聲。一聲不寫符號。
    public static let toneMarks: [Character?] = [nil, nil, "ˊ", "ˇ", "ˋ", "˙"]

    public var consonant: UInt8 = 0  // 1...21
    public var medial: UInt8 = 0     // 1...3
    public var rime: UInt8 = 0       // 1...13
    public var tone: UInt8 = 0       // 1...5，0 表示尚未輸入聲調

    public init(consonant: UInt8 = 0, medial: UInt8 = 0, rime: UInt8 = 0, tone: UInt8 = 0) {
        self.consonant = consonant
        self.medial = medial
        self.rime = rime
        self.tone = tone
    }

    /// 解析「ㄉㄜ˙」「ㄍㄥ」這類字串。輕聲符號放在前面或後面都可以。無法解析時回傳 nil。
    public init?(_ text: some StringProtocol) {
        var s = Syllable()
        for ch in text {
            if let i = Syllable.consonants.firstIndex(of: ch) {
                guard s.consonant == 0 else { return nil }
                s.consonant = UInt8(i + 1)
            } else if let i = Syllable.medials.firstIndex(of: ch) {
                guard s.medial == 0 else { return nil }
                s.medial = UInt8(i + 1)
            } else if let i = Syllable.rimes.firstIndex(of: ch) {
                guard s.rime == 0 else { return nil }
                s.rime = UInt8(i + 1)
            } else if let i = Syllable.toneMarks.firstIndex(where: { $0 == ch }) {
                guard s.tone == 0 else { return nil }
                s.tone = UInt8(i)
            } else {
                return nil
            }
        }
        guard !s.isEmpty else { return nil }
        if s.tone == 0 { s.tone = 1 }
        self = s
    }

    public init(code: UInt16) {
        consonant = UInt8((code >> 9) & 0x1F)
        medial = UInt8((code >> 7) & 0x3)
        rime = UInt8((code >> 3) & 0xF)
        tone = UInt8(code & 0x7)
    }

    /// 壓縮成 16 位元整數，作為詞庫索引用。
    public var code: UInt16 {
        UInt16(consonant) << 9 | UInt16(medial) << 7 | UInt16(rime) << 3 | UInt16(tone)
    }

    public var isEmpty: Bool { consonant == 0 && medial == 0 && rime == 0 }

    /// 顯示用字串（一聲不加符號）。
    public var description: String {
        var out = ""
        if consonant > 0 { out.append(Syllable.consonants[Int(consonant) - 1]) }
        if medial > 0 { out.append(Syllable.medials[Int(medial) - 1]) }
        if rime > 0 { out.append(Syllable.rimes[Int(rime) - 1]) }
        if tone > 1, let mark = Syllable.toneMarks[Int(tone)] { out.append(mark) }
        return out
    }

    public static func < (a: Syllable, b: Syllable) -> Bool { a.code < b.code }

    /// 解析以空白分隔的多個音節，例如「ㄍㄥ ㄒㄧㄣ」。
    public static func parseReading(_ text: some StringProtocol) -> [Syllable]? {
        var result: [Syllable] = []
        for part in text.split(whereSeparator: { $0 == " " || $0 == "-" }) {
            guard let s = Syllable(part) else { return nil }
            result.append(s)
        }
        return result.isEmpty ? nil : result
    }
}

/// 鍵盤上一個注音鍵代表的符號。
public enum BopomofoComponent: Equatable {
    case consonant(UInt8)
    case medial(UInt8)
    case rime(UInt8)
    case tone(UInt8)
}

/// 標準大千排列：以美式鍵盤「未按 Shift」時的字元對應注音符號。
public enum DaChenLayout {
    public static let table: [Character: BopomofoComponent] = {
        var t: [Character: BopomofoComponent] = [:]
        let consonantKeys = "1qaz2wsxedcrfv5tgbyhn"
        for (i, k) in consonantKeys.enumerated() { t[k] = .consonant(UInt8(i + 1)) }
        for (i, k) in "ujm".enumerated() { t[k] = .medial(UInt8(i + 1)) }
        for (i, k) in "8ik,9ol.0p;/-".enumerated() { t[k] = .rime(UInt8(i + 1)) }
        t[" "] = .tone(1)
        t["6"] = .tone(2)
        t["3"] = .tone(3)
        t["4"] = .tone(4)
        t["7"] = .tone(5)
        return t
    }()

    public static func component(for key: Character) -> BopomofoComponent? { table[key] }
}

/// 正在拼的注音（還沒按聲調鍵的那一個字）。
public struct BopomofoComposer: Equatable {
    public private(set) var syllable = Syllable()
    /// 依輸入順序記錄哪些位置有值，倒退鍵時從最後輸入的開始刪。
    private var order: [Int] = []

    public init() {}

    public var isEmpty: Bool { syllable.isEmpty }

    /// 顯示用：已輸入的符號（不含聲調）。
    public var display: String { syllable.description }

    /// 放入一個聲母、介音或韻母；同一位置再按一次會取代。
    public mutating func receive(_ component: BopomofoComponent) {
        let slot: Int
        switch component {
        case .consonant(let v): syllable.consonant = v; slot = 0
        case .medial(let v): syllable.medial = v; slot = 1
        case .rime(let v): syllable.rime = v; slot = 2
        case .tone: return
        }
        order.removeAll { $0 == slot }
        order.append(slot)
    }

    public mutating func backspace() {
        guard let slot = order.popLast() else { return }
        switch slot {
        case 0: syllable.consonant = 0
        case 1: syllable.medial = 0
        default: syllable.rime = 0
        }
    }

    public mutating func clear() {
        syllable = Syllable()
        order = []
    }

    /// 加上聲調，得到完整音節。
    public func completed(tone: UInt8) -> Syllable? {
        guard !isEmpty else { return nil }
        var s = syllable
        s.tone = tone
        return s
    }
}
