#pragma once

// 對應 Sources/BangbangZhuyin/KeyMapping.swift：把 Windows 的按鍵轉成引擎用的 KeyEvent。

#include <windows.h>

#include <optional>

#include "InputSession.h"

namespace bangbang {

/// 依實體按鍵位置（掃描碼，美式鍵盤）判斷字元，不受鍵盤配置與 Caps Lock 影響；
/// 沒有掃描碼時（例如遠端桌面或軟體送出的按鍵）改用虛擬鍵碼。沒有對應時回傳 nullopt。
std::optional<KeyEvent> keyEventFromKey(WPARAM virtualKey, LPARAM lParam, bool shift);

/// Shift、Ctrl、Alt、Win、Caps Lock、Num Lock 這類鍵：一律交給應用程式。
bool isModifierKey(WPARAM virtualKey);

}  // namespace bangbang
