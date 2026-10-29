import BunbunEngine
import Carbon
import Foundation

let usage = """
用法：
  bunbun-tool build-lm --data <小麥資料夾> --weights <讀音比例檔> [--discouraged <少用字檔>] --out <模型檔> [--heldout <檔案>] [--min-bigram N] [語料檔...]
  bunbun-tool convert --lm <模型檔> <注音，音節以空白分隔>
  bunbun-tool reading --lm <模型檔> <中文>
  bunbun-tool eval --lm <模型檔> <測試句檔> [--limit N] [--show N]
  bunbun-tool register <輸入法 .app 路徑>
"""

var args = Array(CommandLine.arguments.dropFirst())
guard let command = args.first else {
    print(usage)
    exit(1)
}
args.removeFirst()

func option(_ name: String) -> String? {
    guard let i = args.firstIndex(of: name), i + 1 < args.count else { return nil }
    let value = args[i + 1]
    args.removeSubrange(i...(i + 1))
    return value
}

func loadModel() -> LanguageModel {
    guard let path = option("--lm") else {
        print(usage)
        exit(1)
    }
    do {
        return try LanguageModel.load(from: URL(fileURLWithPath: path))
    } catch {
        info("無法讀取模型：\(error)")
        exit(1)
    }
}

switch command {
case "build-lm":
    guard let data = option("--data"), let out = option("--out") else {
        print(usage)
        exit(1)
    }
    let weights = option("--weights")
    let discouraged = option("--discouraged")
    let heldoutPath = option("--heldout")
    var builder = ModelBuilder()
    if let m = option("--min-bigram"), let v = UInt32(m) { builder.minBigramCount = v }
    if let m = option("--skip-scale"), let v = Float(m) { builder.skipScale = v }
    if let m = option("--min-skip"), let v = UInt32(m) { builder.minSkipCount = v }
    if let m = option("--corpus-weight"), let v = Double(m) { builder.corpusWeight = v }
    let start = Date()
    let dict = try SourceDictionary(directory: URL(fileURLWithPath: data), curated: weights.map { URL(fileURLWithPath: $0) },
                                    discouraged: discouraged.map { URL(fileURLWithPath: $0) })
    info("詞庫：\(dict.words.count - 1) 個詞")
    var counter: CorpusCounter?
    if !args.isEmpty {
        let c = CorpusCounter(dictionary: dict)
        for path in args {
            info("讀取語料 \(path)")
            try c.process(file: URL(fileURLWithPath: path), stripPTTHeader: path.lowercased().contains("ptt"))
        }
        info("語料共 \(c.documents) 篇、\(c.tokens) 個詞，用時 \(Int(Date().timeIntervalSince(start))) 秒")
        if let heldoutPath {
            try c.heldout.joined(separator: "\n").write(toFile: heldoutPath, atomically: true, encoding: .utf8)
            info("保留 \(c.heldout.count) 句作為評測：\(heldoutPath)")
        }
        counter = c
    }
    let lm = builder.build(dictionary: dict, counter: counter)
    try lm.write(to: URL(fileURLWithPath: out))
    let size = (try? FileManager.default.attributesOfItem(atPath: out)[.size] as? Int) ?? 0
    info("已寫入 \(out)（\(size / 1_000_000) MB），總用時 \(Int(Date().timeIntervalSince(start))) 秒")

case "convert":
    let lm = loadModel()
    guard let syllables = Syllable.parseReading(args.joined(separator: " ")) else {
        info("注音格式錯誤")
        exit(1)
    }
    let decoder = Decoder(lm: lm)
    let walk = decoder.decode(syllables.map { .syllable($0) })
    print(walk.map(\.text).joined())
    print(walk.map(\.text).joined(separator: " / "))

case "lookup":
    let lm = loadModel()
    guard let syllables = Syllable.parseReading(args.joined(separator: " ")) else {
        info("注音格式錯誤")
        exit(1)
    }
    for e in lm.entries(for: ReadingKey(syllables)).prefix(20) {
        print(String(format: "%@\tunigram %.2f\t讀音權重 %.2f", lm.words[Int(e.word)], lm.logUnigram[Int(e.word)], e.readingWeight))
    }

case "score":
    // 計算一種斷詞的分數明細，例如：score --lm x.lm 越 更新 越 難 用
    let lm = loadModel()
    var index: [String: UInt32] = [:]
    for (i, w) in lm.words.enumerated() { index[w] = UInt32(i) }
    var prev = LanguageModel.boundary, prev2 = LanguageModel.boundary
    var total: Float = 0
    for w in args {
        guard let id = index[w] else {
            info("詞庫沒有：\(w)")
            exit(1)
        }
        let bi = lm.logProb(prev, id), sk = lm.skip(prev2, id)
        let stored = lm.storedBigram(prev, id) != nil ? "有統計" : "退回"
        print(String(format: "%@\t雙連 %.2f（%@）\t隔詞 +%.2f", w, bi, stored, sk))
        total += bi + sk
        prev2 = prev
        prev = id
    }
    print(String(format: "合計 %.2f（未含讀音權重）", total))

case "keys":
    // 模擬按鍵，例如：keys --lm x.lm "5j/ jp6<down>2<enter>"
    // 特殊鍵：<space> <enter> <esc> <bs> <del> <left> <right> <up> <down> <home> <end>，大寫字母代表 Shift+字母
    let lm = loadModel()
    let session = InputSession(lm: lm, userModel: UserModel(lm: lm, fileURL: nil))
    let specials: [String: KeyEvent] = [
        "space": .space, "enter": .enter, "esc": .escape, "bs": .backspace, "del": .forwardDelete,
        "left": .left, "right": .right, "up": .up, "down": .down, "home": .home, "end": .end,
    ]
    var committed = ""
    func show(_ label: String) {
        var line = "\(label)\t組字：「\(session.markedText)」"
        if let list = session.candidateList {
            line += "\t候選：" + list.items.prefix(9).enumerated().map { i, c in
                (i == list.highlighted ? "▸" : "") + "\(i + 1).\(c.text)"
            }.joined(separator: " ")
        }
        print(line)
    }
    var chars = Array(args.joined(separator: " "))
    while !chars.isEmpty {
        let key: KeyEvent
        var label = String(chars[0])
        if chars[0] == "<", let close = chars.firstIndex(of: ">"), let k = specials[String(chars[1..<close])] {
            key = k
            label = String(chars[0...close])
            chars.removeFirst(close + 1)
        } else {
            let c = chars.removeFirst()
            key = c == " " ? .space : (c.isUppercase ? .character(Character(c.lowercased()), shift: true) : .character(c, shift: false))
        }
        let out = session.handle(key)
        committed += out.commit
        if label.hasPrefix("<") || out.beep { show(label + (out.beep ? "（嗶）" : "")) }
    }
    show("結束")
    print("已送出：「\(committed)」")

case "reading":
    let lm = loadModel()
    let evaluator = Evaluator(lm: lm)
    for text in args {
        let r = evaluator.reading(of: text)
        print(r.map { $0.map(\.description).joined(separator: " ") } ?? "（無法轉換）")
    }

case "eval":
    let limit = option("--limit").flatMap(Int.init) ?? .max
    let show = option("--show").flatMap(Int.init) ?? 30
    let lm = loadModel()
    guard let path = args.first else {
        print(usage)
        exit(1)
    }
    let lines = try String(contentsOfFile: path, encoding: .utf8).components(separatedBy: .newlines)
    let evaluator = Evaluator(lm: lm)
    let start = Date()
    let r = evaluator.evaluate(lines: lines, limit: limit)
    for f in r.failures.prefix(show) {
        print("✗ 應為：\(f.expected)\n  結果：\(f.got)\n  注音：\(f.reading)")
    }
    print(String(format: "句子 %d 句，整句全對 %.1f%%，逐字正確率 %.2f%%（%.1f 秒）",
                 r.sentences, r.sentenceAccuracy * 100, r.charAccuracy * 100, Date().timeIntervalSince(start)))

case "register":
    guard let path = args.first else {
        print(usage)
        exit(1)
    }
    let status = TISRegisterInputSource(URL(fileURLWithPath: path) as CFURL)
    if status == noErr {
        print("已向系統註冊：\(path)")
    } else {
        info("註冊失敗，錯誤碼 \(status)")
        exit(1)
    }

default:
    print(usage)
    exit(1)
}
