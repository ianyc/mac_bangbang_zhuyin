# TSF 外殼：BangbangZhuyin.dll（in-process COM 伺服器）。取代 Sources/BangbangZhuyin/。
add_library(BangbangZhuyin SHARED
    DllMain.cpp
    Guids.h
    BangbangZhuyin.def
    BangbangZhuyin.rc)
target_link_libraries(BangbangZhuyin PRIVATE bangbang_engine bangbang_options)
