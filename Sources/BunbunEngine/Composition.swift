import Foundation

/// 組字區：使用者已經打好、還沒送出的那一串字，包含游標與手動選字。
public final class Composition {
    public var decoder: Decoder
    public private(set) var tokens: [Token] = []
    /// 游標在第幾格之前（0...tokens.count）。
    public private(set) var cursor = 0
    public private(set) var pins: [Pin] = []
    public private(set) var walk: [WalkNode] = []

    public init(decoder: Decoder) {
        self.decoder = decoder
    }

    public var isEmpty: Bool { tokens.isEmpty }
    public var count: Int { tokens.count }
    public var text: String { walk.map(\.text).joined() }

    /// 游標前面的文字（每一格剛好對應一個字）。
    public var textBeforeCursor: String { String(text.prefix(cursor)) }
    public var textAfterCursor: String { String(text.dropFirst(cursor)) }

    public func insert(_ token: Token) {
        // 插入點落在某個釘選詞中間時，該釘選失效。
        pins.removeAll { $0.start < cursor && cursor < $0.end }
        for i in pins.indices where pins[i].start >= cursor { pins[i].start += 1 }
        tokens.insert(token, at: cursor)
        cursor += 1
        rewalk()
    }

    @discardableResult
    public func deleteBackward() -> Bool {
        guard cursor > 0 else { return false }
        remove(at: cursor - 1)
        cursor -= 1
        rewalk()
        return true
    }

    @discardableResult
    public func deleteForward() -> Bool {
        guard cursor < tokens.count else { return false }
        remove(at: cursor)
        rewalk()
        return true
    }

    private func remove(at index: Int) {
        pins.removeAll { $0.start <= index && index < $0.end }
        for i in pins.indices where pins[i].start > index { pins[i].start -= 1 }
        tokens.remove(at: index)
    }

    @discardableResult
    public func moveCursor(to position: Int) -> Bool {
        guard position >= 0, position <= tokens.count, position != cursor else { return false }
        cursor = position
        return true
    }

    public func clear() {
        tokens = []
        cursor = 0
        pins = []
        walk = []
    }

    public func candidates() -> [Candidate] {
        decoder.candidates(tokens, cursor: cursor, walk: walk)
    }

    /// 手動選字：把這個詞釘在該位置，再重新計算整句。
    public func select(_ candidate: Candidate) {
        pins.removeAll { $0.start < candidate.start + candidate.length && candidate.start < $0.end }
        pins.append(Pin(start: candidate.start, length: candidate.length, word: candidate.word))
        rewalk()
    }

    /// 選字結果裡，結束在 position 的那個詞（作為下一個詞的上下文）。
    public func wordEnding(at position: Int) -> UInt32 {
        guard position > 0, let node = walk.first(where: { $0.end == position }) else { return LanguageModel.boundary }
        return node.word ?? LanguageModel.boundary
    }

    /// 組字區太長時，把最前面的詞送出，回傳送出的文字。
    public func commitLeading(keeping maxTokens: Int) -> String {
        var committed = ""
        while tokens.count > maxTokens, let first = walk.first {
            committed += first.text
            for _ in 0..<first.length { remove(at: 0) }
            cursor = max(0, cursor - first.length)
            walk.removeFirst()
            walk = walk.map { WalkNode(start: $0.start - first.length, length: $0.length, word: $0.word, text: $0.text) }
        }
        if !committed.isEmpty { rewalk() }
        return committed
    }

    public func rewalk() {
        walk = decoder.decode(tokens, pins: pins)
    }
}
