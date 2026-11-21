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
    TextService.h TextService.cpp)
target_include_directories(BangbangZhuyin PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(BangbangZhuyin PRIVATE bangbang_engine bangbang_options ole32 oleaut32 uuid advapi32 shell32)
