@testable import BunbunEngine
import XCTest

/// 測試用的迷你語言模型。
private func makeLM(bigrams: [(String, String, Float)] = [], skips: [(String, String, Float)] = []) -> LanguageModel {
    let words = ["<s>", "你", "妳", "好", "你好", "越", "月", "更新", "難", "南", "用", "越南", "更", "新", "中", "鐘", "文", "聞", "中文"]
    let unigram: [String: Float] = [
        "<s>": -30, "你": -3, "妳": -6, "好": -3, "你好": -4, "越": -6, "月": -4,
        "更新": -6, "難": -5, "南": -5.5, "用": -4, "越南": -6, "更": -6, "新": -6,
        "中": -5, "鐘": -6, "文": -5, "聞": -7, "中文": -4,
    ]
    let id = Dictionary(uniqueKeysWithValues: words.enumerated().map { ($1, UInt32($0)) })
    let lexicon: [String: [String]] = [
        "ㄋㄧˇ": ["你", "妳"], "ㄏㄠˇ": ["好"], "ㄋㄧˇ ㄏㄠˇ": ["你好"],
        "ㄩㄝˋ": ["越", "月"], "ㄍㄥ ㄒㄧㄣ": ["更新"], "ㄋㄢˊ": ["難", "南"],
        "ㄩㄥˋ": ["用"], "ㄩㄝˋ ㄋㄢˊ": ["越南"], "ㄍㄥ": ["更"], "ㄒㄧㄣ": ["新"],
        "ㄓㄨㄥ": ["中", "鐘"], "ㄨㄣˊ": ["文", "聞"], "ㄓㄨㄥ ㄨㄣˊ": ["中文"],
    ]
    var lex: [ReadingKey: [LexiconEntry]] = [:]
    for (reading, ws) in lexicon {
        lex[ReadingKey(Syllable.parseReading(reading)!)] = ws.map { LexiconEntry(word: id[$0]!, readingWeight: 0) }
    }
    let sorted = bigrams.map { (id[$0.0]!, id[$0.1]!, $0.2) }.sorted { ($0.0, $0.1) < ($1.0, $1.1) }
    var start = [UInt32](repeating: 0, count: words.count + 1)
    for (h, _, _) in sorted { start[Int(h) + 1] += 1 }
    for i in 1...words.count { start[i] += start[i - 1] }
    var skipBoost: [UInt64: Float] = [:]
    for (a, b, v) in skips { skipBoost[UInt64(id[a]!) << 32 | UInt64(id[b]!)] = v }
    return LanguageModel(words: words, logUnigram: words.map { unigram[$0]! },
                         logBackoff: words.map { _ in log(0.1) },
                         bigramStart: start, bigramNext: sorted.map(\.1), bigramLogProb: sorted.map(\.2),
                         lexicon: lex, skipBoost: skipBoost)
}

/// 依大千排列打字；空白鍵為一聲。
@discardableResult
private func type(_ session: InputSession, _ keys: String) -> [SessionOutput] {
    keys.map { $0 == " " ? session.handle(.space) : session.handle(.character($0, shift: false)) }
}

final class BopomofoTests: XCTestCase {
    func testParseAndDescribe() {
        XCTAssertEqual(Syllable("ㄉㄜ˙")?.description, "ㄉㄜ˙")
        XCTAssertEqual(Syllable("˙ㄉㄜ"), Syllable("ㄉㄜ˙"))
        XCTAssertEqual(Syllable("ㄍㄥ")?.tone, 1)
        XCTAssertNil(Syllable("ㄅㄆ"))
        let s = Syllable("ㄒㄩㄢˊ")!
        XCTAssertEqual(Syllable(code: s.code), s)
    }

    func testDaChenLayout() {
        var composer = BopomofoComposer()
        for key in "su" { composer.receive(DaChenLayout.component(for: key)!) }
        XCTAssertEqual(composer.completed(tone: 3), Syllable("ㄋㄧˇ"))
        XCTAssertEqual(DaChenLayout.component(for: " "), .tone(1))
        XCTAssertEqual(DaChenLayout.component(for: "-"), .rime(13))  // ㄦ
    }

    func testComposerReplacesSlotAndBackspaces() {
        var composer = BopomofoComposer()
        composer.receive(.consonant(1))  // ㄅ
        composer.receive(.rime(1))       // ㄚ
        composer.receive(.consonant(3))  // ㄇ 取代 ㄅ
        XCTAssertEqual(composer.display, "ㄇㄚ")
        composer.backspace()             // 刪掉最後輸入的 ㄇ
        XCTAssertEqual(composer.display, "ㄚ")
    }
}

final class InputSessionTests: XCTestCase {
    func testTypingAndEnterCommits() {
        let session = InputSession(lm: makeLM())
        type(session, "su3cl3")
        XCTAssertEqual(session.markedText, "你好")
        XCTAssertEqual(session.handle(.enter).commit, "你好")
        XCTAssertFalse(session.isComposing)
    }

    func testSpaceWhileSpellingIsFirstTone() {
        let session = InputSession(lm: makeLM())
        let outputs = type(session, "e/ vup ")
        XCTAssertTrue(outputs.allSatisfy { $0.commit.isEmpty })
        XCTAssertEqual(session.markedText, "更新")
    }

    /// 需求 7c：組字中、沒有待拼注音時按空白 → 在組字區加半形空白，按 Enter 才送出。
    func testSpaceAfterCompositionStaysInComposition() {
        let session = InputSession(lm: makeLM())
        type(session, "su3cl3")
        let out = session.handle(.space)
        XCTAssertEqual(out.commit, "")
        XCTAssertTrue(out.handled)
        XCTAssertEqual(session.markedText, "你好 ")
        type(session, "su3")
        XCTAssertEqual(session.markedText, "你好 你")
        XCTAssertEqual(session.handle(.enter).commit, "你好 你")
        XCTAssertFalse(session.isComposing)
    }

    func testSpaceWithNothingComposingPassesThrough() {
        let session = InputSession(lm: makeLM())
        XCTAssertFalse(session.handle(.space).handled)
    }

    /// 需求 7a：Shift + 字母 → 半形英文。
    func testShiftLetterIsHalfWidth() {
        let session = InputSession(lm: makeLM())
        XCTAssertEqual(session.handle(.character("a", shift: true)).commit, "A")
        type(session, "su3")
        session.handle(.character("b", shift: true))
        XCTAssertEqual(session.markedText, "你B")
    }

    func testPunctuation() {
        let session = InputSession(lm: makeLM())
        XCTAssertEqual(session.handle(.character(",", shift: true)).commit, "，")
        XCTAssertEqual(session.handle(.character(".", shift: true)).commit, "。")
        XCTAssertEqual(session.handle(.character("'", shift: false)).commit, "、")
        XCTAssertEqual(session.handle(.character("[", shift: false)).commit, "「")
    }

    /// 需求 7b：游標在字「前面」按 ↓，列出從游標開始的詞。
    func testCandidatesStartAtCursorLikeMicrosoft() {
        let session = InputSession(lm: makeLM())
        type(session, "su3cl3")
        session.handle(.home)
        XCTAssertTrue(session.handle(.down).handled)
        let items = session.candidateList!.items
        XCTAssertEqual(items.map(\.text), ["你好", "你", "妳"])
        XCTAssertTrue(items.allSatisfy { $0.start == 0 })
        // 選「妳」（第 3 個）
        session.handle(.character("3", shift: false))
        XCTAssertNil(session.candidateList)
        XCTAssertEqual(session.markedText, "妳好")
        XCTAssertEqual(session.composition.cursor, 1, "選完游標移到該詞後面")
    }

    /// 游標在最後時只列出最後一個字。
    func testCandidatesAtEndListOnlyLastCharacter() {
        let session = InputSession(lm: makeLM())
        type(session, "su3cl3")
        session.handle(.down)
        XCTAssertEqual(session.candidateList!.items.map(\.text), ["好"])
        session.handle(.escape)
        XCTAssertNil(session.candidateList)
        XCTAssertEqual(session.markedText, "你好")
    }

    /// 改了「文」之後，前面的「中」不能跟著變（沒有固定的話會變成「鐘聞」）。
    func testSelectionKeepsPrecedingText() {
        let lm = makeLM(bigrams: [("鐘", "聞", -1)])
        let session = InputSession(lm: lm)
        type(session, "5j/ jp6")
        XCTAssertEqual(session.markedText, "中文")
        session.handle(.down)
        let items = session.candidateList!.items
        XCTAssertEqual(items.map(\.text), ["文", "聞"])
        XCTAssertEqual(items[session.candidateList!.highlighted].text, "文", "預設反白目前的字")
        session.handle(.character("2", shift: false))
        XCTAssertEqual(session.markedText, "中聞")
        // 沒有固定前面的字時，同樣的選擇會讓整句變成「鐘聞」
        let free = Decoder(lm: lm).decode(session.composition.tokens, pins: [Pin(start: 1, length: 1, word: 17)])
        XCTAssertEqual(free.map(\.text).joined(), "鐘聞")
    }

    func testCursorMovementAndInsertion() {
        let session = InputSession(lm: makeLM())
        type(session, "su3cl3")
        session.handle(.left)
        XCTAssertEqual(session.markedCursor, 1)
        type(session, "cl3")
        XCTAssertEqual(session.composition.count, 3)
        XCTAssertEqual(session.markedCursor, 2)
        XCTAssertEqual(session.handle(.home).handled, true)
        XCTAssertTrue(session.handle(.left).beep, "已在最前面")
    }

    func testBackspaceAndEscape() {
        let session = InputSession(lm: makeLM())
        type(session, "su")
        XCTAssertEqual(session.markedText, "ㄋㄧ")
        session.handle(.backspace)
        XCTAssertEqual(session.markedText, "ㄋ")
        session.handle(.escape)
        XCTAssertFalse(session.isComposing)
        type(session, "su3cl3")
        session.handle(.backspace)
        XCTAssertEqual(session.markedText, "你")
        session.handle(.escape)
        XCTAssertFalse(session.isComposing)
    }

    func testInvalidSyllableIsRejected() {
        let session = InputSession(lm: makeLM())
        let out = type(session, "183").last!  // ㄅㄚˇ 不在迷你詞庫
        XCTAssertTrue(out.beep)
        XCTAssertEqual(session.markedText, "ㄅㄚ")
    }

    func testToneKeyWithoutBopomofoDoesNothing() {
        let session = InputSession(lm: makeLM())
        let out = session.handle(.character("3", shift: false))
        XCTAssertTrue(out.handled)
        XCTAssertEqual(out.commit, "")
    }

    func testLongCompositionCommitsLeadingWords() {
        let session = InputSession(lm: makeLM())
        session.maxCompositionLength = 3
        let outputs = type(session, "su3cl3su3cl3")
        XCTAssertEqual(outputs.map(\.commit).joined(), "你好")
        XCTAssertEqual(session.markedText, "你好")
    }

    func testUserChoiceIsRemembered() {
        let lm = makeLM()
        let memory = UserModel(lm: lm, fileURL: nil)
        let session = InputSession(lm: lm, userModel: memory)
        type(session, "su3")
        XCTAssertEqual(session.markedText, "你")
        session.handle(.down)
        let index = session.candidateList!.items.firstIndex { $0.text == "妳" }!
        session.handle(.character(Character(String(index + 1)), shift: false))
        session.handle(.enter)
        type(session, "su3")
        XCTAssertEqual(session.markedText, "妳")
    }
}

final class DecoderTests: XCTestCase {
    func testUnigramOnly() {
        let decoder = Decoder(lm: makeLM())
        let walk = decoder.decode(Syllable.parseReading("ㄩㄝˋ")!.map { .syllable($0) })
        XCTAssertEqual(walk.map(\.text), ["月"])
    }

    func testBigramContextChangesChoice() {
        let lm = makeLM(bigrams: [("越", "更新", -1)])
        let decoder = Decoder(lm: lm)
        let walk = decoder.decode(Syllable.parseReading("ㄩㄝˋ ㄍㄥ ㄒㄧㄣ")!.map { .syllable($0) })
        XCTAssertEqual(walk.map(\.text).joined(), "越更新")
    }

    func testSkipPairChangesChoice() {
        // 沒有隔詞搭配時：月 更新 越南 用；有「越 ＿ 越」時：越 更新 越 難 用
        let reading = Syllable.parseReading("ㄩㄝˋ ㄍㄥ ㄒㄧㄣ ㄩㄝˋ ㄋㄢˊ ㄩㄥˋ")!.map { Token.syllable($0) }
        XCTAssertEqual(Decoder(lm: makeLM()).decode(reading).map(\.text).joined(), "月更新越南用")
        let lm = makeLM(bigrams: [("越", "難", -1)], skips: [("越", "越", 4)])
        XCTAssertEqual(Decoder(lm: lm).decode(reading).map(\.text).joined(), "越更新越難用")
    }

    func testPinnedWordIsKept() {
        let decoder = Decoder(lm: makeLM())
        let tokens = Syllable.parseReading("ㄋㄧˇ ㄏㄠˇ")!.map { Token.syllable($0) }
        let walk = decoder.decode(tokens, pins: [Pin(start: 0, length: 1, word: 2)])  // 妳
        XCTAssertEqual(walk.map(\.text).joined(), "妳好")
    }

    func testPunctuationResetsContext() {
        let decoder = Decoder(lm: makeLM())
        var tokens = Syllable.parseReading("ㄋㄧˇ ㄏㄠˇ")!.map { Token.syllable($0) }
        tokens.append(.literal("，"))
        tokens.append(.syllable(Syllable("ㄩㄝˋ")!))
        XCTAssertEqual(decoder.decode(tokens).map(\.text).joined(), "你好，月")
    }
}

final class ModelFileTests: XCTestCase {
    func testWriteAndLoadRoundTrip() throws {
        let lm = makeLM(bigrams: [("越", "更新", -1), ("你", "好", -0.5)], skips: [("越", "越", 2)])
        let url = FileManager.default.temporaryDirectory.appendingPathComponent("bunbun-test-\(UUID()).lm")
        defer { try? FileManager.default.removeItem(at: url) }
        try lm.write(to: url)
        let loaded = try LanguageModel.load(from: url)
        XCTAssertEqual(loaded.words, lm.words)
        XCTAssertEqual(loaded.lexicon, lm.lexicon)
        XCTAssertEqual(loaded.logProb(5, 7), -1)
        XCTAssertEqual(loaded.logProb(1, 3), -0.5)
        XCTAssertEqual(loaded.logProb(6, 7), lm.logProb(6, 7))
        XCTAssertEqual(loaded.skip(5, 5), 2)
    }
}
