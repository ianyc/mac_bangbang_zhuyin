#pragma once

#include <windows.h>
#include <msctf.h>

namespace bangbang {

/// 組字區的顯示方式（ITfDisplayAttributeProvider）：
/// - 組字中的文字：單底線（TF_ATTR_INPUT）。
/// - 正在選字的那一段：粗底線（TF_ATTR_TARGET_CONVERTED）。
/// bAttr 讓透過 IMM32 的舊程式也能顯示對應的樣式。
HRESULT createDisplayAttributeInfo(REFGUID guid, ITfDisplayAttributeInfo** info);
HRESULT createDisplayAttributeEnum(IEnumTfDisplayAttributeInfo** enumerator);

}  // namespace bangbang
