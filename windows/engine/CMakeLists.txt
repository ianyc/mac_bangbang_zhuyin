# 選字引擎：純邏輯，不依賴 TSF，可以單獨測試。逐檔對應 Sources/BangbangEngine/。
add_library(bangbang_engine STATIC
    Bopomofo.h Bopomofo.cpp
    LanguageModel.h LanguageModel.cpp
    Decoder.h Decoder.cpp
    Composition.h Composition.cpp
    InputSession.h InputSession.cpp
    Punctuation.h Punctuation.cpp
    SharedFile.h SharedFile.cpp
    UserModel.h UserModel.cpp
    Utf8.h Utf8.cpp)
target_include_directories(bangbang_engine PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
target_include_directories(bangbang_engine SYSTEM PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/../third_party")
target_link_libraries(bangbang_engine PRIVATE bangbang_options)
