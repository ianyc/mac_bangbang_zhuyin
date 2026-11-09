import AppKit
import BangbangEngine

/// 把 NSEvent 轉成引擎用的 KeyEvent。依實體按鍵位置（keyCode）判斷，不受 Caps Lock 與鍵盤配置影響。
enum KeyMapping {
    /// 美式鍵盤上各實體鍵「沒按 Shift」時的字元。
    static let characters: [UInt16: Character] = [
        0x00: "a", 0x01: "s", 0x02: "d", 0x03: "f", 0x04: "h", 0x05: "g", 0x06: "z", 0x07: "x",
        0x08: "c", 0x09: "v", 0x0B: "b", 0x0C: "q", 0x0D: "w", 0x0E: "e", 0x0F: "r", 0x10: "y",
        0x11: "t", 0x12: "1", 0x13: "2", 0x14: "3", 0x15: "4", 0x16: "6", 0x17: "5", 0x18: "=",
        0x19: "9", 0x1A: "7", 0x1B: "-", 0x1C: "8", 0x1D: "0", 0x1E: "]", 0x1F: "o", 0x20: "u",
        0x21: "[", 0x22: "i", 0x23: "p", 0x25: "l", 0x26: "j", 0x27: "'", 0x28: "k", 0x29: ";",
        0x2A: "\\", 0x2B: ",", 0x2C: "/", 0x2D: "n", 0x2E: "m", 0x2F: ".", 0x32: "`",
    ]

    static let numpad: [UInt16: Character] = [
        0x41: ".", 0x43: "*", 0x45: "+", 0x4B: "/", 0x4E: "-", 0x51: "=",
        0x52: "0", 0x53: "1", 0x54: "2", 0x55: "3", 0x56: "4", 0x57: "5", 0x58: "6", 0x59: "7", 0x5B: "8", 0x5C: "9",
    ]

    static func keyEvent(from event: NSEvent) -> KeyEvent? {
        let code = event.keyCode
        switch code {
        case 0x24, 0x4C: return .enter
        case 0x30: return .tab
        case 0x31: return .space
        case 0x33: return .backspace
        case 0x35: return .escape
        case 0x75: return .forwardDelete
        case 0x73: return .home
        case 0x77: return .end
        case 0x74: return .pageUp
        case 0x79: return .pageDown
        case 0x7B: return .left
        case 0x7C: return .right
        case 0x7D: return .down
        case 0x7E: return .up
        default: break
        }
        if let c = numpad[code] { return .numpad(c) }
        if let c = characters[code] { return .character(c, shift: event.modifierFlags.contains(.shift)) }
        return nil
    }
}
