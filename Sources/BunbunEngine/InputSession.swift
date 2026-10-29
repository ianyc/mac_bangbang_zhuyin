import Foundation

/// 與鍵盤配置無關的按鍵事件。字元一律是美式鍵盤上「沒按 Shift」時的字元（字母為小寫）。
public enum KeyEvent: Equatable {
    case character(Character, shift: Bool)
    case numpad(Character)
    case space
    case enter
    case backspace
    case forwardDelete
    case escape
    case tab
    case left, right, up, down
    case home, end, pageUp, pageDown
}

/// 一次按鍵處理的結果，由輸入法外殼套用到目前的 App。
public struct SessionOutput: Equatable {
    /// false 表示這個按鍵交還給 App 處理。
    public var handled: Bool
    /// 要直接送出到 App 的文字。
    public var commit: String = ""
    public var beep = false

    static let passThrough = SessionOutput(handled: false)
    static let consumed = SessionOutput(handled: true)
    static let rejected = SessionOutput(handled: true, beep: true)
}

/// 候選字視窗的狀態。
public struct CandidateList: Equatable {
    public let items: [Candidate]
    public var highlighted: Int = 0
    public let perPage: Int

    public var page: Int { highlighted / perPage }
    public var pageCount: Int { max(1, (items.count + perPage - 1) / perPage) }
    public var pageRange: Range<Int> {
        let start = page * perPage
        return start..<min(items.count, start + perPage)
    }
    public var highlightedInPage: Int { highlighted - page * perPage }
}

/// 注音模式下的按鍵狀態機：拼注音、整句選字、游標、候選字、標點符號。
public final class InputSession {
    public let composition: Composition
    public let userModel: UserModel?
    public private(set) var composer = BopomofoComposer()
    public private(set) var candidateList: CandidateList?
    /// 組字區超過這個長度，就自動送出最前面的詞。
    public var maxCompositionLength = 20
    public var candidatesPerPage = 9

    public init(lm: LanguageModel, userModel: UserModel? = nil) {
        self.userModel = userModel
        self.composition = Composition(decoder: Decoder(lm: lm, adjuster: userModel))
    }

    var lm: LanguageModel { composition.decoder.lm }

    public var isComposing: Bool { !composer.isEmpty || !composition.isEmpty }

    /// 要顯示在 App 裡、帶底線的組字文字。
    public var markedText: String {
        composition.textBeforeCursor + composer.display + composition.textAfterCursor
    }

    /// 組字文字中游標的位置（以字元計）。
    public var markedCursor: Int { composition.cursor + composer.display.count }

    /// 候選字對應的組字範圍（以字元計），用來標示正在選哪幾個字。
    public var candidateRange: Range<Int>? {
        guard let list = candidateList else { return nil }
        let item = list.items[list.highlighted]
        return item.start..<(item.start + item.length)
    }

    /// 送出所有組字（切換 App、切換輸入法時用）。
    public func commitAll() -> String {
        let text = composition.text
        reset()
        return text
    }

    public func reset() {
        composer.clear()
        composition.clear()
        candidateList = nil
    }

    public func handle(_ key: KeyEvent) -> SessionOutput {
        if candidateList != nil {
            if let out = handleCandidateKey(key) { return out }
            candidateList = nil
        }
        switch key {
        case .character(let c, let shift):
            return handleCharacter(c, shift: shift)
        case .numpad(let c):
            return insertLiteral(String(c))
        case .space:
            if !composer.isEmpty { return completeSyllable(tone: 1) }
            // 組字中按空白：在組字區放一個半形空白，按 Enter 才送出。
            if !composition.isEmpty { return insertLiteral(" ") }
            return .passThrough
        case .enter:
            guard isComposing else { return .passThrough }
            let text = composition.text
            reset()
            return SessionOutput(handled: true, commit: text)
        case .backspace:
            if !composer.isEmpty {
                composer.backspace()
                return .consumed
            }
            guard !composition.isEmpty else { return .passThrough }
            return composition.deleteBackward() ? .consumed : .rejected
        case .forwardDelete:
            guard isComposing else { return .passThrough }
            guard composer.isEmpty else { return .rejected }
            return composition.deleteForward() ? .consumed : .rejected
        case .escape:
            if !composer.isEmpty {
                composer.clear()
                return .consumed
            }
            guard !composition.isEmpty else { return .passThrough }
            reset()
            return .consumed
        case .left, .right, .home, .end:
            guard isComposing else { return .passThrough }
            guard composer.isEmpty else { return .rejected }
            let target: Int
            switch key {
            case .left: target = composition.cursor - 1
            case .right: target = composition.cursor + 1
            case .home: target = 0
            default: target = composition.count
            }
            return composition.moveCursor(to: target) ? .consumed : .rejected
        case .down:
            guard isComposing else { return .passThrough }
            guard composer.isEmpty else { return .rejected }
            return openCandidates() ? .consumed : .rejected
        case .up, .tab, .pageUp, .pageDown:
            return isComposing ? .consumed : .passThrough
        }
    }

    // MARK: - 一般按鍵

    private func handleCharacter(_ c: Character, shift: Bool) -> SessionOutput {
        if shift {
            // Shift + 字母：半形大寫英文。
            if c.isLetter, c.isASCII { return insertLiteral(c.uppercased()) }
            if let p = Punctuation.shifted[c] { return insertLiteral(p) }
            return isComposing ? .consumed : .passThrough
        }
        if let component = DaChenLayout.component(for: c) {
            if case .tone(let t) = component {
                // 沒有注音時按聲調鍵：不做事。
                return composer.isEmpty ? (isComposing ? .rejected : .consumed) : completeSyllable(tone: t)
            }
            composer.receive(component)
            return .consumed
        }
        if let p = Punctuation.unshifted[c] { return insertLiteral(p) }
        return isComposing ? .consumed : .passThrough
    }

    private func completeSyllable(tone: UInt8) -> SessionOutput {
        guard var s = composer.completed(tone: tone) else { return .rejected }
        if !lm.hasReading(s) {
            // ㄐㄑㄒ 後面打成 ㄨ 時，自動改成 ㄩ。
            if [12, 13, 14].contains(s.consonant), s.medial == 2 {
                var fixed = s
                fixed.medial = 3
                if lm.hasReading(fixed) { s = fixed }
            }
            guard lm.hasReading(s) else { return .rejected }
        }
        composer.clear()
        composition.insert(.syllable(s))
        return SessionOutput(handled: true, commit: composition.commitLeading(keeping: maxCompositionLength))
    }

    /// 英文字母、標點：沒有組字時直接送出；組字中則放進組字區。
    private func insertLiteral(_ text: String) -> SessionOutput {
        composer.clear()
        if composition.isEmpty { return SessionOutput(handled: true, commit: text) }
        composition.insert(.literal(text))
        return SessionOutput(handled: true, commit: composition.commitLeading(keeping: maxCompositionLength))
    }

    // MARK: - 候選字

    private func openCandidates() -> Bool {
        let items = composition.candidates()
        guard !items.isEmpty else { return false }
        var list = CandidateList(items: items, perPage: candidatesPerPage)
        // 預設反白目前顯示的詞；它是更長詞的一部分時，反白目前顯示的那個字。
        let position = items[0].start
        let shown = Array(composition.text)
        if let node = composition.walk.first(where: { $0.start <= position && position < $0.end }) {
            if let idx = items.firstIndex(where: { $0.start == node.start && $0.length == node.length && $0.word == node.word }) {
                list.highlighted = idx
            } else if position < shown.count,
                      let idx = items.firstIndex(where: { $0.length == 1 && $0.text == String(shown[position]) }) {
                list.highlighted = idx
            }
        }
        candidateList = list
        return true
    }

    /// 回傳 nil 表示關閉候選窗，並把這個按鍵當一般按鍵處理。
    private func handleCandidateKey(_ key: KeyEvent) -> SessionOutput? {
        guard var list = candidateList else { return nil }
        switch key {
        case .character(let c, false) where ("1"..."9").contains(c):
            let index = list.page * list.perPage + Int(String(c))! - 1
            guard index < list.items.count, index < (list.page + 1) * list.perPage else { return .rejected }
            choose(list.items[index])
            return .consumed
        case .enter:
            choose(list.items[list.highlighted])
            return .consumed
        case .down:
            guard list.highlighted + 1 < list.items.count else { return .rejected }
            list.highlighted += 1
        case .up:
            guard list.highlighted > 0 else { return .rejected }
            list.highlighted -= 1
        case .space, .right, .pageDown:
            let next = (list.page + 1) * list.perPage
            list.highlighted = next < list.items.count ? next : 0
        case .left, .pageUp:
            let prev = list.page - 1
            list.highlighted = (prev >= 0 ? prev : list.pageCount - 1) * list.perPage
        case .escape, .backspace:
            candidateList = nil
            return .consumed
        default:
            return nil
        }
        candidateList = list
        return .consumed
    }

    private func choose(_ candidate: Candidate) {
        let wasAtEnd = composition.cursor == composition.count
        composition.select(candidate)
        let key = ReadingKey(composition.tokens[candidate.start..<(candidate.start + candidate.length)].compactMap { $0.syllable?.code })
        userModel?.learn(prev: composition.wordEnding(at: candidate.start), word: candidate.word, key: key)
        // 選完後游標移到這個詞後面，方便繼續往後修正。
        if !wasAtEnd { composition.moveCursor(to: candidate.start + candidate.length) }
        candidateList = nil
    }
}

/// 標點符號（仿微軟新注音／小麥注音的標準大千配置）。
public enum Punctuation {
    public static let unshifted: [Character: String] = [
        "'": "、", "[": "「", "]": "」", "`": "`", "=": "=", "\\": "\\",
    ]

    public static let shifted: [Character: String] = [
        ",": "，", ".": "。", "/": "？", ";": "：", "'": "；",
        "1": "！", "2": "@", "3": "#", "4": "$", "5": "%", "6": "^", "7": "&", "8": "*",
        "9": "（", "0": "）", "-": "_", "=": "+",
        "[": "『", "]": "』", "\\": "|", "`": "～",
    ]
}
