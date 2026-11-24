# MSI 安裝檔的自訂動作 DLL：WiX（BangbangZhuyin.wxs）會把它包進 MSI，安裝時以使用者身分把輸入法加進鍵盤清單。
# MSI 是 64 位元的，所以只在 x64 建置。MSI 本身由 windows\scripts\package.ps1 呼叫 WiX 建置。
if(CMAKE_SIZEOF_VOID_P EQUAL 8)
    add_library(BangbangSetup SHARED CustomActions.cpp CustomActions.def)
    target_link_libraries(BangbangSetup PRIVATE bangbang_options msi)
endif()
