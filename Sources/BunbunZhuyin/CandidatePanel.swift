import AppKit
import BunbunEngine

/// 候選字視窗：直式清單，數字 1–9 選字。不會搶走目前 App 的焦點。
final class CandidatePanel: NSPanel {
    private let listView = CandidateListView()
    private let background = NSVisualEffectView()

    init() {
        super.init(contentRect: .zero, styleMask: [.borderless, .nonactivatingPanel], backing: .buffered, defer: true)
        level = NSWindow.Level(rawValue: Int(CGWindowLevelForKey(.cursorWindow)) - 1)
        isOpaque = false
        backgroundColor = .clear
        hasShadow = true
        hidesOnDeactivate = false
        collectionBehavior = [.canJoinAllSpaces, .fullScreenAuxiliary, .transient]

        background.material = .popover
        background.state = .active
        background.wantsLayer = true
        background.layer?.cornerRadius = 8
        background.layer?.masksToBounds = true
        background.addSubview(listView)
        contentView = background
    }

    override var canBecomeKey: Bool { false }
    override var canBecomeMain: Bool { false }

    /// caret：游標所在的那一行（螢幕座標）。視窗放在它下方，放不下就放上方。
    func show(_ list: CandidateList, below caret: NSRect) {
        listView.list = list
        let size = listView.fittingSize
        listView.frame = NSRect(origin: .zero, size: size)
        background.frame = listView.frame

        let screen = NSScreen.screens.first { $0.frame.contains(caret.origin) } ?? NSScreen.main
        let visible = screen?.visibleFrame ?? NSRect(x: 0, y: 0, width: 1440, height: 900)
        var origin = NSPoint(x: caret.minX, y: caret.minY - size.height - 4)
        if origin.y < visible.minY { origin.y = caret.maxY + 4 }
        origin.x = min(max(origin.x, visible.minX), visible.maxX - size.width)
        setFrame(NSRect(origin: origin, size: size), display: true)
        orderFrontRegardless()
    }

    func hide() {
        orderOut(nil)
    }
}

final class CandidateListView: NSView {
    var list: CandidateList? {
        didSet { needsDisplay = true }
    }

    private let candidateFont = NSFont(name: "PingFangTC-Regular", size: 20) ?? .systemFont(ofSize: 20)
    private let numberFont = NSFont.monospacedDigitSystemFont(ofSize: 13, weight: .regular)
    private let footerFont = NSFont.systemFont(ofSize: 11)
    private let rowHeight: CGFloat = 30
    private let padding: CGFloat = 6
    private let numberWidth: CGFloat = 22

    override var isFlipped: Bool { true }

    private var footer: String? {
        guard let list, list.pageCount > 1 else { return nil }
        return "\(list.page + 1)/\(list.pageCount)"
    }

    override var fittingSize: NSSize {
        guard let list else { return .zero }
        var width: CGFloat = 60
        for i in list.pageRange {
            let w = (list.items[i].text as NSString).size(withAttributes: [.font: candidateFont]).width
            width = max(width, numberWidth + w + padding * 3)
        }
        let rows = CGFloat(list.pageRange.count)
        let footerHeight: CGFloat = footer == nil ? 0 : 18
        return NSSize(width: ceil(width), height: ceil(rows * rowHeight + padding * 2 + footerHeight))
    }

    override func draw(_ dirtyRect: NSRect) {
        guard let list else { return }
        for (row, i) in list.pageRange.enumerated() {
            let rowRect = NSRect(x: padding / 2, y: padding + CGFloat(row) * rowHeight, width: bounds.width - padding, height: rowHeight)
            let highlighted = i == list.highlighted
            if highlighted {
                NSColor.selectedContentBackgroundColor.setFill()
                NSBezierPath(roundedRect: rowRect, xRadius: 5, yRadius: 5).fill()
            }
            let textColor: NSColor = highlighted ? .alternateSelectedControlTextColor : .labelColor
            let numberColor: NSColor = highlighted ? .alternateSelectedControlTextColor : .secondaryLabelColor
            let number = "\(row + 1)" as NSString
            let numberSize = number.size(withAttributes: [.font: numberFont])
            number.draw(at: NSPoint(x: rowRect.minX + padding, y: rowRect.midY - numberSize.height / 2),
                        withAttributes: [.font: numberFont, .foregroundColor: numberColor])
            let text = list.items[i].text as NSString
            let textSize = text.size(withAttributes: [.font: candidateFont])
            text.draw(at: NSPoint(x: rowRect.minX + padding + numberWidth, y: rowRect.midY - textSize.height / 2),
                      withAttributes: [.font: candidateFont, .foregroundColor: textColor])
        }
        if let footer {
            let s = footer as NSString
            let size = s.size(withAttributes: [.font: footerFont])
            s.draw(at: NSPoint(x: bounds.width - size.width - padding, y: bounds.height - size.height - 4),
                   withAttributes: [.font: footerFont, .foregroundColor: NSColor.tertiaryLabelColor])
        }
    }
}
