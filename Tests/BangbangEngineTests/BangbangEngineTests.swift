@testable import BangbangEngine
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
        composer.backspace()             // 刪掉最靠近游標的 ㄚ（不是最後輸入的 ㄇ）
        XCTAssertEqual(composer.display, "ㄇ")
        composer.backspace()
        XCTAssertTrue(composer.isEmpty)
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

    /// 全形標點不論何時都進入組字區，按 Enter 才送出（同微軟新注音）。
    func testFullWidthPunctuationEntersComposition() {
        let session = InputSession(lm: makeLM())
        let out = session.handle(.option(","))
        XCTAssertTrue(out.handled)
        XCTAssertEqual(out.commit, "")
        XCTAssertEqual(session.markedText, "，")
        session.handle(.option("."))
        session.handle(.option("'"))
        session.handle(.option("["))
        XCTAssertEqual(session.handle(.enter).commit, "，。、【")
        XCTAssertFalse(session.isComposing)
    }

    /// Shift＋標點與 ' [ ] 是半形，沒有組字時直接送出（比照微軟注音）。
    func testShiftedAndBracketKeysAreHalfWidth() {
        let cases: [(KeyEvent, String)] = [
            (.character(",", shift: true), "<"), (.character(".", shift: true), ">"), (.character("1", shift: true), "!"),
            (.character("`", shift: false), "`"), (.character("`", shift: true), "~"),
            (.character("9", shift: true), "("), (.character("0", shift: true), ")"),
            (.character("[", shift: false), "["), (.character("]", shift: false), "]"),
            (.character(";", shift: true), ":"), (.character("'", shift: false), "'"),
            (.character("'", shift: true), "\""), (.character("/", shift: true), "?"),
        ]
        for (key, text) in cases {
            let session = InputSession(lm: makeLM())
            let out = session.handle(key)
            XCTAssertTrue(out.handled, text)
            XCTAssertEqual(out.commit, text, "沒有組字時直接送出")
            XCTAssertFalse(session.isComposing, text)
        }
    }

    /// ⌥＋, . ; ' [ ] 打全形標點；其他 ⌥ 組合鍵交給 App。
    func testOptionKeysTypeFullWidth() {
        let session = InputSession(lm: makeLM())
        for c in ",.;'[]" { XCTAssertTrue(session.handle(.option(c)).handled, String(c)) }
        XCTAssertEqual(session.markedText, "，。；、【】")
        XCTAssertEqual(session.handle(.enter).commit, "，。；、【】")
        XCTAssertFalse(session.handle(.option("c")).handled)
    }

    /// 候選順序：「，」的第二個是「、」；「【」之後是「「」「『」，「】」同理。
    func testOptionPunctuationCandidates() {
        func candidates(_ c: Character) -> [String] {
            let session = InputSession(lm: makeLM())
            session.handle(.option(c))
            XCTAssertTrue(session.handle(.down).handled)
            return session.candidateList?.items.map(\.text) ?? []
        }
        XCTAssertEqual(Array(candidates(",").prefix(3)), ["，", "、", ","])
        XCTAssertEqual(Array(candidates("[").prefix(3)), ["【", "「", "『"])
        XCTAssertEqual(Array(candidates("]").prefix(3)), ["】", "」", "』"])
        XCTAssertEqual(candidates(".").first, "。")
        XCTAssertEqual(candidates(";").first, "；")
        XCTAssertEqual(candidates("'").first, "、")
    }

    /// 組字中打半形符號：和 Shift＋字母一樣放進組字區，跟著句子一起送出。
    func testHalfWidthInsideComposition() {
        let session = InputSession(lm: makeLM())
        type(session, "su3")
        session.handle(.character(",", shift: true))
        type(session, "cl3")
        XCTAssertEqual(session.markedText, "你<好")
        XCTAssertEqual(session.handle(.enter).commit, "你<好")
    }

    /// 拼注音時 Backspace 刪最靠近游標的符號：先打 ㄏㄨ 再打 ㄈ（取代成 ㄈㄨ），刪的是 ㄨ。
    func testBackspaceDeletesComponentNearestTheCursor() {
        let session = InputSession(lm: makeLM())
        type(session, "cjz")
        XCTAssertEqual(session.markedText, "ㄈㄨ")
        session.handle(.backspace)
        XCTAssertEqual(session.markedText, "ㄈ")
        type(session, "u,")  // ㄧ ㄝ
        XCTAssertEqual(session.markedText, "ㄈㄧㄝ")
        session.handle(.backspace)
        XCTAssertEqual(session.markedText, "ㄈㄧ")
        session.handle(.backspace)
        session.handle(.backspace)
        XCTAssertFalse(session.isComposing)
    }

    /// 符號按 ↓：列出全形／半形與同一個鍵上的另一個符號，例如「：」也能換成「；」。
    func testSymbolCandidates() {
        let session = InputSession(lm: makeLM())
        session.handle(.option(";"))
        XCTAssertTrue(session.handle(.down).handled)
        XCTAssertEqual(session.candidateList!.items.map(\.text), ["；", "：", ":", ";"])
        session.handle(.character("2", shift: false))
        XCTAssertEqual(session.markedText, "：")
        XCTAssertEqual(session.handle(.enter).commit, "：")
    }

    func testSymbolCandidatesInsideSentence() {
        let session = InputSession(lm: makeLM())
        type(session, "su3")
        session.handle(.option(","))
        type(session, "cl3")
        XCTAssertEqual(session.markedText, "你，好")
        session.handle(.left)
        session.handle(.left)  // 游標移到「，」前面
        session.handle(.down)
        let items = session.candidateList!.items
        XCTAssertEqual(items.first?.text, "，")
        let comma = items.firstIndex { $0.text == "," }!
        session.handle(.character(Character(String(comma + 1)), shift: false))
        XCTAssertEqual(session.markedText, "你,好")
        XCTAssertEqual(session.composition.cursor, 2, "選完游標移到符號後面")
    }

    /// 把『』的前半改成「，後半自動變成」；改後半也會帶動前半。
    func testBracketPairChangesTogether() {
        let session = InputSession(lm: makeLM())
        session.handle(.character("[", shift: true))   // 『
        type(session, "su3")
        session.handle(.character("]", shift: true))   // 』
        XCTAssertEqual(session.markedText, "『你』")
        session.handle(.home)
        session.handle(.down)
        let index = session.candidateList!.items.firstIndex { $0.text == "「" }!
        session.handle(.character(Character(String(index + 1)), shift: false))
        XCTAssertEqual(session.markedText, "「你」")
        session.handle(.end)
        session.handle(.down)
        let paren = session.candidateList!.items.firstIndex { $0.text == "】" }!
        session.handle(.character(Character(String(paren + 1)), shift: false))
        XCTAssertEqual(session.markedText, "【你】")
    }

    /// 巢狀括號依層數配對：改最外層的前半，只改到最外層的後半。
    func testNestedBracketsMatchByDepth() {
        let lm = makeLM()
        let composition = Composition(decoder: Decoder(lm: lm))
        for t in ["『", "『", "」", "』", "』"] { composition.insert(.literal(t)) }
        composition.replaceLiteral(at: 0, with: "「")
        XCTAssertEqual(composition.text, "「『」』」")
        // 換成不是括號的符號時，另一半不動
        composition.replaceLiteral(at: 1, with: "9")
        XCTAssertEqual(composition.text, "「9」』」")
    }

    func testSpaceCandidatesOfferFullWidthSpace() {
        let session = InputSession(lm: makeLM())
        type(session, "su3")
        session.handle(.space)
        session.handle(.down)
        let items = session.candidateList!.items
        XCTAssertEqual(items.map(\.text), [" ", "\u{3000}"])
        XCTAssertEqual(items.map(\.displayText), ["半形空白", "全形空白"])
    }

    func testHalfWidthSymbolCommitsDirectly() {
        let session = InputSession(lm: makeLM())
        XCTAssertEqual(session.handle(.character("2", shift: true)).commit, "@")
        XCTAssertFalse(session.isComposing)
    }

    /// 目前顯示的詞排在候選字第一筆（同微軟新注音），即使有更長的詞。
    func testCurrentSelectionIsFirstCandidate() {
        let session = InputSession(lm: makeLM())
        type(session, "m,4s06")
        XCTAssertEqual(session.markedText, "越南")
        session.handle(.down)
        XCTAssertEqual(session.candidateList!.items.map(\.text), ["南", "難"], "目前的「南」排第一")
        session.handle(.character("2", shift: false))  // 最後一字改選「難」
        XCTAssertEqual(session.markedText, "越難")
        session.handle(.home)
        session.handle(.down)
        let list = session.candidateList!
        XCTAssertEqual(list.items.first?.text, "越")
        XCTAssertEqual(list.highlighted, 0)
        XCTAssertTrue(list.items.contains { $0.text == "越南" })
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

    /// 組字區上限 30 格（比照 Windows 版）：打到第 31 格才自動送出最前面的詞。
    func testCompositionHoldsThirtyCells() {
        let session = InputSession(lm: makeLM())
        XCTAssertEqual(session.maxCompositionLength, 30)
        let outputs = type(session, String(repeating: "su3", count: 30))
        XCTAssertEqual(outputs.map(\.commit).joined(), "", "30 格以內都留在組字區")
        XCTAssertEqual(session.composition.count, 30)
        XCTAssertEqual(type(session, "su3").map(\.commit).joined(), "你", "第 31 格送出最前面的詞")
        XCTAssertEqual(session.composition.count, 30)
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

/// 選字記憶：同一格選最多次的勝出，平手看最近一次，每個字詞最多累計 3 次。
final class UserModelTests: XCTestCase {
    /// 打 ㄋㄧˇ、用 ↓ 選指定的字、按 Enter 送出，回傳送出的文字。
    @discardableResult
    private func pick(_ session: InputSession, _ text: String) -> String {
        type(session, "su3")
        session.handle(.down)
        let index = session.candidateList!.items.firstIndex { $0.text == text }!
        session.handle(.character(Character(String(index + 1)), shift: false))
        return session.handle(.enter).commit
    }

    /// 不選字、直接打 ㄋㄧˇ 時預設出現的字。
    private func defaultChoice(_ session: InputSession) -> String {
        type(session, "su3")
        return session.handle(.enter).commit
    }

    private func makeSession(_ lm: LanguageModel = makeLM()) -> InputSession {
        InputSession(lm: lm, userModel: UserModel(lm: lm, fileURL: nil))
    }

    func testCorrectionTakesEffectImmediately() {
        let session = makeSession()
        pick(session, "妳")
        XCTAssertEqual(defaultChoice(session), "妳")
        pick(session, "你")  // 平手，最近選的是「你」
        XCTAssertEqual(defaultChoice(session), "你")
    }

    func testMostPickedWins() {
        let session = makeSession()
        pick(session, "妳")
        pick(session, "妳")
        pick(session, "你")
        XCTAssertEqual(defaultChoice(session), "妳", "2 次贏過 1 次")
        pick(session, "你")
        XCTAssertEqual(defaultChoice(session), "你", "2 比 2 平手，最近選的是「你」")
    }

    /// 次數只差一次，也要贏過常用程度差很多的字。
    func testUserChoiceBeatsMuchMoreCommonWord() {
        let session = makeSession()
        pick(session, "你")
        pick(session, "妳")
        pick(session, "妳")
        XCTAssertEqual(defaultChoice(session), "妳")
        pick(session, "你")
        XCTAssertEqual(defaultChoice(session), "你")
        pick(session, "妳")  // 平手時看最近，不看哪個字比較常用
        XCTAssertEqual(defaultChoice(session), "妳")
    }

    /// 選很多次也最多算 3 次：改選 3 次就能換過來。
    func testCountIsCappedSoOldHabitsCanChange() {
        let session = makeSession()
        for _ in 0..<6 { pick(session, "妳") }
        pick(session, "你")
        pick(session, "你")
        XCTAssertEqual(defaultChoice(session), "妳")
        pick(session, "你")
        XCTAssertEqual(defaultChoice(session), "你")
    }

    /// 不同的前一個詞是不同的格，互不影響。
    func testContextsAreSeparate() {
        let session = makeSession()
        pick(session, "妳")  // 句首選「妳」
        type(session, "cl3su3")  // 「好」後面的 ㄋㄧˇ
        XCTAssertEqual(session.markedText, "好你")
    }

    // MARK: - 記憶檔（比照 Windows 版）

    /// 每個測試用自己的暫存資料夾。
    private func temporaryMemoryFile() throws -> URL {
        let dir = FileManager.default.temporaryDirectory.appendingPathComponent("bangbang-memory-\(UUID())")
        try FileManager.default.createDirectory(at: dir, withIntermediateDirectories: true)
        addTeardownBlock { try? FileManager.default.removeItem(at: dir) }
        return dir.appendingPathComponent("user-model.json")
    }

    /// 另一台電腦的記憶檔：句首選過「妳」。
    private let otherMacMemory = #"{"version":2,"contextual":{"<s>\t妳\tㄋㄧˇ":{"count":1,"lastUsed":1}},"unigram":{"妳\tㄋㄧˇ":{"count":1,"lastUsed":1}}}"#

    /// 記憶檔被換掉（例如從另一台電腦複製過來）時，下次組字前重新載入，不用重新啟動輸入法。
    func testReloadsWhenFileIsReplaced() throws {
        let lm = makeLM()
        let url = try temporaryMemoryFile()
        let session = InputSession(lm: lm, userModel: UserModel(lm: lm, fileURL: url))
        XCTAssertEqual(defaultChoice(session), "你")
        try otherMacMemory.write(to: url, atomically: true, encoding: .utf8)
        XCTAssertEqual(defaultChoice(session), "妳", "下次組字前重新載入")
    }

    /// 刪掉記憶檔就是清除記憶：執行中的輸入法也跟著忘掉。
    func testDeletingFileForgetsMemory() throws {
        let lm = makeLM()
        let url = try temporaryMemoryFile()
        let memory = UserModel(lm: lm, fileURL: url)
        let session = InputSession(lm: lm, userModel: memory)
        pick(session, "妳")
        memory.waitUntilSaved()
        XCTAssertEqual(defaultChoice(session), "妳")
        try FileManager.default.removeItem(at: url)
        XCTAssertEqual(defaultChoice(session), "你", "刪掉記憶檔就跟著忘掉")
        pick(session, "妳")
        memory.waitUntilSaved()
        XCTAssertTrue(FileManager.default.fileExists(atPath: url.path), "之後學到的照常存檔")
    }

    /// 自己寫出去的記憶檔不算「被別人換掉」：不會重讀，存檔還沒寫完時也不會遺失剛學到的。
    func testOwnSaveIsNotTreatedAsReplacement() throws {
        let lm = makeLM()
        let url = try temporaryMemoryFile()
        let memory = UserModel(lm: lm, fileURL: url)
        let session = InputSession(lm: lm, userModel: memory)
        pick(session, "妳")
        XCTAssertEqual(defaultChoice(session), "妳", "存檔還在背景寫的時候")
        memory.waitUntilSaved()
        XCTAssertFalse(memory.refresh(), "寫完之後版本和自己寫的一樣")
        XCTAssertEqual(defaultChoice(session), "妳")
        let other = InputSession(lm: lm, userModel: UserModel(lm: lm, fileURL: url))
        XCTAssertEqual(defaultChoice(other), "妳", "確實寫進檔案")
    }

    /// 壞掉的記憶檔（例如手動改到一半）在下次存檔前改名成 .corrupt 保留，不直接蓋掉。
    func testCorruptFileIsKeptAside() throws {
        let lm = makeLM()
        let url = try temporaryMemoryFile()
        try "{ 壞掉了".write(to: url, atomically: true, encoding: .utf8)
        let memory = UserModel(lm: lm, fileURL: url)
        let session = InputSession(lm: lm, userModel: memory)
        XCTAssertEqual(defaultChoice(session), "你")
        pick(session, "妳")
        memory.waitUntilSaved()
        XCTAssertEqual(try String(contentsOf: url.appendingPathExtension("corrupt"), encoding: .utf8), "{ 壞掉了")
        let reloaded = InputSession(lm: lm, userModel: UserModel(lm: lm, fileURL: url))
        XCTAssertEqual(defaultChoice(reloaded), "妳", "新的記憶檔是完整的")
    }

    /// 執行中記憶檔被換成壞檔：保留目前的記憶；下次存檔時才把壞檔改名保留。
    func testCorruptReplacementKeepsCurrentMemory() throws {
        let lm = makeLM()
        let url = try temporaryMemoryFile()
        let memory = UserModel(lm: lm, fileURL: url)
        let session = InputSession(lm: lm, userModel: memory)
        pick(session, "妳")
        memory.waitUntilSaved()
        try "not json".write(to: url, atomically: true, encoding: .utf8)
        XCTAssertEqual(defaultChoice(session), "妳", "壞檔不會清掉目前的記憶")
        XCTAssertFalse(FileManager.default.fileExists(atPath: url.appendingPathExtension("corrupt").path), "還沒存檔前不動它")
        pick(session, "你")
        memory.waitUntilSaved()
        XCTAssertEqual(try String(contentsOf: url.appendingPathExtension("corrupt"), encoding: .utf8), "not json")
        XCTAssertEqual(defaultChoice(session), "你")
        let reloaded = InputSession(lm: lm, userModel: UserModel(lm: lm, fileURL: url))
        XCTAssertEqual(defaultChoice(reloaded), "你", "新檔同時有「妳」和「你」，平手看最近")
    }

    /// 舊版記憶檔（上下文記錄沒有讀音）要能照常使用並轉成新格式。
    func testLoadsOldFormat() throws {
        let lm = makeLM()
        let url = FileManager.default.temporaryDirectory.appendingPathComponent("bangbang-memory-\(UUID()).json")
        defer { try? FileManager.default.removeItem(at: url) }
        let old = #"{"contextual":{"<s>\t妳":{"count":5,"lastUsed":1}},"unigram":{"妳\tㄋㄧˇ":{"count":5,"lastUsed":1}}}"#
        try old.write(to: url, atomically: true, encoding: .utf8)
        let session = InputSession(lm: lm, userModel: UserModel(lm: lm, fileURL: url))
        XCTAssertEqual(defaultChoice(session), "妳")
        pick(session, "你")
        pick(session, "你")
        pick(session, "你")  // 舊的 5 次以 3 次計，改選 3 次就換過來
        XCTAssertEqual(defaultChoice(session), "你")
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
        let url = FileManager.default.temporaryDirectory.appendingPathComponent("bangbang-test-\(UUID()).lm")
        defer { try? FileManager.default.removeItem(at: url) }
        try lm.write(to: url)
        let loaded = try LanguageModel.load(from: url)
        XCTAssertEqual(Array(loaded.words), Array(lm.words))
        XCTAssertEqual(loaded.lexicon, lm.lexicon)
        XCTAssertEqual(loaded.logProb(5, 7), -1)
        XCTAssertEqual(loaded.logProb(1, 3), -0.5)
        XCTAssertEqual(loaded.logProb(6, 7), lm.logProb(6, 7))
        XCTAssertEqual(loaded.skip(5, 5), 2)
    }
}
