#pragma once

// 偵測「單按 Shift」：Shift 按下再放開，中間沒有按其他鍵、也沒有按滑鼠，才算一次（中英切換，第 5 階段）。
// 純邏輯、和 TSF 無關，單元測試直接測。虛擬鍵碼用 Windows 的數值。
//
// 同一個按鍵事件可能從兩個地方各送來一次（TSF 的按鍵通知與執行緒的訊息掛鉤），所以每個方法重複呼叫都不會出錯：
// 重複的按下不影響結果；放開時第一次回報單按，之後就回到初始狀態。

namespace bangbang {

class ShiftTapDetector {
public:
    static constexpr unsigned kShift = 0x10;       // VK_SHIFT
    static constexpr unsigned kLeftShift = 0xA0;   // VK_LSHIFT
    static constexpr unsigned kRightShift = 0xA1;  // VK_RSHIFT

    static bool isShift(unsigned vk) { return vk == kShift || vk == kLeftShift || vk == kRightShift; }

    /// 有鍵按下（包括按住不放時的自動重複）。
    void keyDown(unsigned vk) {
        if (isShift(vk)) {
            if (!shiftDown_) {
                shiftDown_ = true;
                tap_ = true;
            }
            return;
        }
        tap_ = false;  // Shift 按著時按了別的鍵（Shift＋字母、Ctrl＋Shift…）
    }

    /// 有鍵放開。回傳 true 表示剛完成一次單按 Shift。
    bool keyUp(unsigned vk) {
        if (!isShift(vk)) return false;
        const bool tap = shiftDown_ && tap_;
        shiftDown_ = false;
        tap_ = false;
        return tap;
    }

    /// 按了滑鼠鍵：Shift 按著的話（例如 Shift＋點選來選取文字），這次不算單按。
    void mouseDown() { tap_ = false; }

    /// 焦點換走等情況：忘掉進行中的狀態（放開的事件可能送到別的程式）。
    void reset() {
        shiftDown_ = false;
        tap_ = false;
    }

private:
    bool shiftDown_ = false;
    bool tap_ = false;
};

}  // namespace bangbang
