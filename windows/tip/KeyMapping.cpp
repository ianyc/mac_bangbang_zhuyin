#include "KeyMapping.h"

namespace bangbang {

namespace {

using T = KeyEvent::Type;

/// 掃描碼（Set 1，非延伸鍵）→ 美式鍵盤上「沒按 Shift」時的字元。
char characterFromScanCode(UINT scan) {
    static constexpr char kTable[0x36] = {
        0, 0, '1', '2', '3', '4', '5', '6', '7', '8', '9', '0', '-', '=', 0, 0,        // 0x00–0x0F
        'q', 'w', 'e', 'r', 't', 'y', 'u', 'i', 'o', 'p', '[', ']', 0, 0, 'a', 's',    // 0x10–0x1F
        'd', 'f', 'g', 'h', 'j', 'k', 'l', ';', '\'', '`', 0, '\\', 'z', 'x', 'c', 'v', // 0x20–0x2F
        'b', 'n', 'm', ',', '.', '/',                                                    // 0x30–0x35
    };
    return scan < sizeof(kTable) ? kTable[scan] : 0;
}

/// 虛擬鍵碼 → 字元（沒有掃描碼時的備案）。
char characterFromVirtualKey(WPARAM vk) {
    if (vk >= 'A' && vk <= 'Z') return static_cast<char>(vk - 'A' + 'a');
    if (vk >= '0' && vk <= '9') return static_cast<char>(vk);
    switch (vk) {
    case VK_OEM_1: return ';';
    case VK_OEM_PLUS: return '=';
    case VK_OEM_COMMA: return ',';
    case VK_OEM_MINUS: return '-';
    case VK_OEM_PERIOD: return '.';
    case VK_OEM_2: return '/';
    case VK_OEM_3: return '`';
    case VK_OEM_4: return '[';
    case VK_OEM_5: return '\\';
    case VK_OEM_6: return ']';
    case VK_OEM_7: return '\'';
    default: return 0;
    }
}

}  // namespace

std::optional<KeyEvent> keyEventFromKey(WPARAM vk, LPARAM lParam, bool shift) {
    switch (vk) {
    case VK_RETURN: return KeyEvent::of(T::Enter);  // 數字鍵盤的 Enter 也是 VK_RETURN
    case VK_TAB: return KeyEvent::of(T::Tab);
    case VK_SPACE: return KeyEvent::of(T::Space);
    case VK_BACK: return KeyEvent::of(T::Backspace);
    case VK_ESCAPE: return KeyEvent::of(T::Escape);
    case VK_DELETE: return KeyEvent::of(T::ForwardDelete);
    case VK_HOME: return KeyEvent::of(T::Home);
    case VK_END: return KeyEvent::of(T::End);
    case VK_PRIOR: return KeyEvent::of(T::PageUp);
    case VK_NEXT: return KeyEvent::of(T::PageDown);
    case VK_LEFT: return KeyEvent::of(T::Left);
    case VK_RIGHT: return KeyEvent::of(T::Right);
    case VK_UP: return KeyEvent::of(T::Up);
    case VK_DOWN: return KeyEvent::of(T::Down);
    case VK_DECIMAL: return KeyEvent::numpad('.');
    case VK_ADD: return KeyEvent::numpad('+');
    case VK_SUBTRACT: return KeyEvent::numpad('-');
    case VK_MULTIPLY: return KeyEvent::numpad('*');
    case VK_DIVIDE: return KeyEvent::numpad('/');
    default: break;
    }
    if (vk >= VK_NUMPAD0 && vk <= VK_NUMPAD9) return KeyEvent::numpad(static_cast<char>('0' + (vk - VK_NUMPAD0)));

    const UINT scan = static_cast<UINT>((lParam >> 16) & 0xFF);
    const bool extended = ((lParam >> 24) & 1) != 0;
    if (characterFromVirtualKey(vk) == 0) return std::nullopt;  // 不是字元鍵（F1、Insert…）
    if (!extended) {
        if (const char c = characterFromScanCode(scan)) return KeyEvent::character(c, shift);
    }
    return KeyEvent::character(characterFromVirtualKey(vk), shift);
}

bool isModifierKey(WPARAM vk) {
    switch (vk) {
    case VK_SHIFT:
    case VK_LSHIFT:
    case VK_RSHIFT:
    case VK_CONTROL:
    case VK_LCONTROL:
    case VK_RCONTROL:
    case VK_MENU:
    case VK_LMENU:
    case VK_RMENU:
    case VK_LWIN:
    case VK_RWIN:
    case VK_CAPITAL:
    case VK_NUMLOCK:
    case VK_SCROLL:
        return true;
    default:
        return false;
    }
}

}  // namespace bangbang
