import AppKit
import BangbangEngine
import InputMethodKit

/// 全部輸入視窗共用的語言模型與選字記憶。
enum Shared {
    static let lm: LanguageModel? = {
        guard let url = Bundle.main.url(forResource: "bangbang", withExtension: "lm") else {
            NSLog("BangbangZhuyin: 找不到 bangbang.lm")
            return nil
        }
        do {
            return try LanguageModel.load(from: url)
        } catch {
            NSLog("BangbangZhuyin: 無法載入語言模型：\(error)")
            return nil
        }
    }()

    static let userModel: UserModel? = lm.map { lm in
        let fm = FileManager.default
        let support = fm.urls(for: .applicationSupportDirectory, in: .userDomainMask).first!
        let file = support.appendingPathComponent("BangbangZhuyin/user-model.json")
        // 0.1 版英文名稱是 BunbunZhuyin：第一次啟動時把舊的選字記憶複製過來。
        let legacy = support.appendingPathComponent("BunbunZhuyin/user-model.json")
        if !fm.fileExists(atPath: file.path), fm.fileExists(atPath: legacy.path) {
            try? fm.createDirectory(at: file.deletingLastPathComponent(), withIntermediateDirectories: true)
            try? fm.copyItem(at: legacy, to: file)
        }
        return UserModel(lm: lm, fileURL: file)
    }

    static let candidatePanel = CandidatePanel()
}

/// 每個 App 的輸入區各有一個 controller，負責把按鍵交給 InputSession，再把結果顯示到 App 裡。
@objc(BangbangInputController)
final class BangbangInputController: IMKInputController {
    private var session: InputSession?

    override init!(server: IMKServer!, delegate: Any!, client inputClient: Any!) {
        super.init(server: server, delegate: delegate, client: inputClient)
        if let lm = Shared.lm { session = InputSession(lm: lm, userModel: Shared.userModel) }
    }

    override func recognizedEvents(_ sender: Any!) -> Int {
        Int(NSEvent.EventTypeMask.keyDown.rawValue)
    }

    override func activateServer(_ sender: Any!) {
        session?.reset()
        Shared.candidatePanel.hide()
    }

    override func deactivateServer(_ sender: Any!) {
        commitAll(sender)
        Shared.candidatePanel.hide()
    }

    override func commitComposition(_ sender: Any!) {
        commitAll(sender)
    }

    override func menu() -> NSMenu! {
        let menu = NSMenu()
        let version = Bundle.main.infoDictionary?["CFBundleShortVersionString"] as? String ?? ""
        let title = NSMenuItem(title: "棒棒注音 \(version)", action: nil, keyEquivalent: "")
        title.isEnabled = false
        menu.addItem(title)
        return menu
    }

    override func handle(_ event: NSEvent!, client sender: Any!) -> Bool {
        guard let event, event.type == .keyDown, let session,
              let client = sender as? (IMKTextInput & NSObjectProtocol) else { return false }

        let flags = event.modifierFlags.intersection(.deviceIndependentFlagsMask)
        // Command / Control / Option 組合鍵交給 App。
        if !flags.intersection([.command, .control, .option]).isEmpty { return false }
        // Caps Lock 鎖定大寫時直接輸出英文。
        if flags.contains(.capsLock) {
            if session.isComposing { commitAll(client) }
            return false
        }
        guard let key = KeyMapping.keyEvent(from: event) else {
            return session.isComposing
        }

        let output = session.handle(key)
        if !output.commit.isEmpty {
            client.insertText(output.commit, replacementRange: NSRange(location: NSNotFound, length: 0))
        }
        if output.beep { NSSound.beep() }
        updateClient(client)
        return output.handled
    }

    private func commitAll(_ sender: Any?) {
        guard let session, session.isComposing else { return }
        let text = session.commitAll()
        if let client = (sender as? (IMKTextInput & NSObjectProtocol)) ?? client() {
            if !text.isEmpty { client.insertText(text, replacementRange: NSRange(location: NSNotFound, length: 0)) }
        }
        Shared.candidatePanel.hide()
    }

    /// 更新 App 裡帶底線的組字文字與候選字視窗。
    private func updateClient(_ client: IMKTextInput & NSObjectProtocol) {
        guard let session else { return }
        let text = session.markedText
        let attributed = NSMutableAttributedString(string: text, attributes: [
            .underlineStyle: NSUnderlineStyle.single.rawValue,
            .markedClauseSegment: 0,
        ])
        var candidateUTF16Start = 0
        if let range = session.candidateRange {
            let prefix = String(text.prefix(range.lowerBound))
            let span = String(text.dropFirst(range.lowerBound).prefix(range.count))
            candidateUTF16Start = prefix.utf16.count
            let nsRange = NSRange(location: candidateUTF16Start, length: span.utf16.count)
            attributed.addAttributes([.underlineStyle: NSUnderlineStyle.thick.rawValue, .markedClauseSegment: 1], range: nsRange)
        }
        let cursor = String(text.prefix(session.markedCursor)).utf16.count
        client.setMarkedText(attributed, selectionRange: NSRange(location: cursor, length: 0),
                             replacementRange: NSRange(location: NSNotFound, length: 0))

        if let list = session.candidateList {
            var lineRect = NSRect.zero
            client.attributes(forCharacterIndex: candidateUTF16Start, lineHeightRectangle: &lineRect)
            if lineRect == .zero {
                let mouse = NSEvent.mouseLocation
                lineRect = NSRect(x: mouse.x, y: mouse.y, width: 1, height: 18)
            }
            Shared.candidatePanel.show(list, below: lineRect)
        } else {
            Shared.candidatePanel.hide()
        }
    }
}
