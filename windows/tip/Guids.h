#pragma once

#include <guiddef.h>

// 棒棒注音向系統註冊時用的 GUID（2026-11-20 產生）。
// 一旦安裝到任何電腦上就不能再改：改了會被系統當成另一套輸入法，使用者要重新加入鍵盤。
namespace bangbang {

// 文字服務（TSF text input processor）的 COM CLSID：{37DA0F04-7924-465F-8DA3-C8EEE7D3BFD5}
inline constexpr GUID kTextServiceClsid = {0x37da0f04, 0x7924, 0x465f, {0x8d, 0xa3, 0xc8, 0xee, 0xe7, 0xd3, 0xbf, 0xd5}};

// 繁體中文（LANGID 0x0404）語言設定檔：{0318F0A4-F04F-4C79-B66B-4CF72A7666ED}
inline constexpr GUID kProfileGuid = {0x0318f0a4, 0xf04f, 0x4c79, {0xb6, 0x6b, 0x4c, 0xf7, 0x2a, 0x76, 0x66, 0xed}};

// 顯示屬性：組字中的文字（單底線）：{B0DCA03A-50DC-4986-BF70-D9C69D36FC73}
inline constexpr GUID kDisplayAttributeInput = {0xb0dca03a, 0x50dc, 0x4986, {0xbf, 0x70, 0xd9, 0xc6, 0x9d, 0x36, 0xfc, 0x73}};

// 顯示屬性：正在選字的那一段（粗底線）：{BFE3FCD1-E522-41ED-9CC8-07020EA1E2C1}
inline constexpr GUID kDisplayAttributeConverted = {0xbfe3fcd1, 0xe522, 0x41ed, {0x9c, 0xc8, 0x07, 0x02, 0x0e, 0xa1, 0xe2, 0xc1}};

// 候選字的 UI 元件（UI-less 模式，ITfUIElement::GetGUID）：{17573891-EB5C-435B-AC57-DAB49E0B57E8}
inline constexpr GUID kCandidateUIElementGuid = {0x17573891, 0xeb5c, 0x435b, {0xac, 0x57, 0xda, 0xb4, 0x9e, 0x0b, 0x57, 0xe8}};

// 單按 Shift 切換中英的 preserved key：{C4EF4916-3C59-4D58-A368-75A1E1A389BD}
inline constexpr GUID kPreservedKeyShiftToggle = {0xc4ef4916, 0x3c59, 0x4d58, {0xa3, 0x68, 0x75, 0xa1, 0xe1, 0xa3, 0x89, 0xbd}};

// 繁體中文（台灣）。
inline constexpr unsigned short kLangId = 0x0404;

}  // namespace bangbang
