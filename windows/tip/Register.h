#pragma once

#include <windows.h>

namespace bangbang {

/// 輸入法在系統裡顯示的名稱。
inline constexpr const wchar_t* kDisplayName = L"棒棒注音";

/// 註冊 COM 類別、TSF 語言設定檔與類別（DllRegisterServer）。需要系統管理員權限。
HRESULT registerAll();
/// 取消註冊（DllUnregisterServer）。
void unregisterAll();

}  // namespace bangbang
