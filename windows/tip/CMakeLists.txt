# TSF 外殼：BangbangZhuyin.dll（in-process COM 伺服器）。取代 Sources/BangbangZhuyin/。
add_library(BangbangZhuyin SHARED
    DllMain.cpp
    BangbangZhuyin.def
    BangbangZhuyin.rc
    resource.h
    Guids.h
    Globals.h Globals.cpp
    ClassFactory.h ClassFactory.cpp
    Register.h Register.cpp
    SharedEngine.h SharedEngine.cpp
    KeyMapping.h KeyMapping.cpp
    EditSession.h EditSession.cpp
    DisplayAttributes.h DisplayAttributes.cpp
    CandidateRenderer.h CandidateRenderer.cpp
    CandidateWindow.h CandidateWindow.cpp
    CandidateListUI.h CandidateListUI.cpp
    TextService.h TextService.cpp)
target_include_directories(BangbangZhuyin PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(BangbangZhuyin PRIVATE bangbang_engine bangbang_options ole32 oleaut32 uuid advapi32 shell32 user32
    d2d1 dwrite dwmapi shcore delayimp)
# 輸入法 DLL 會載入到每一個程式裡：畫候選窗用的 DLL 等到第一次顯示候選窗才載入。
# GetDpiForMonitor 經由 API set（api-ms-win-shcore-scaling）匯入，不是 shcore.dll。
target_link_options(BangbangZhuyin PRIVATE /DELAYLOAD:d2d1.dll /DELAYLOAD:dwrite.dll /DELAYLOAD:dwmapi.dll
    /DELAYLOAD:api-ms-win-shcore-scaling-l1-1-1.dll)
