# 棒棒注音 Windows 版。
# 建置方式見 windows/README.md；計畫見 docs/WINDOWS_PLAN.md。
cmake_minimum_required(VERSION 3.25)
project(BangbangZhuyin VERSION 0.1.0 LANGUAGES CXX RC)

if(NOT MSVC)
    message(FATAL_ERROR "棒棒注音 Windows 版需要用 MSVC（Visual Studio）建置。")
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
# 靜態連結 C/C++ 執行階段（/MT）：輸入法 DLL 會載入到每個程式裡，不能依賴 VC++ 可轉散發套件。
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
# Release 版開啟連結時最佳化（/GL、/LTCG），讓跨檔的小函式也能內聯。
set(CMAKE_INTERPROCEDURAL_OPTIMIZATION_RELEASE ON)

configure_file(Version.h.in "${CMAKE_CURRENT_BINARY_DIR}/generated/BangbangVersion.h" @ONLY)

# 和 Mac 版共用的資料（模型檔、評測句）在 repo 根目錄。
set(BANGBANG_REPO_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/.." CACHE PATH "棒棒注音 repo 的根目錄")

# 專案自己的程式共用的編譯設定（不套用到第三方函式庫）。
add_library(bangbang_options INTERFACE)
target_compile_options(bangbang_options INTERFACE
    $<$<COMPILE_LANGUAGE:CXX>:/utf-8>
    $<$<COMPILE_LANGUAGE:CXX>:/W4>
    $<$<COMPILE_LANGUAGE:CXX>:/permissive->
    $<$<COMPILE_LANGUAGE:CXX>:/Zc:__cplusplus>
    $<$<COMPILE_LANGUAGE:CXX>:/Zc:preprocessor>
    $<$<COMPILE_LANGUAGE:CXX>:/MP>)
target_compile_definitions(bangbang_options INTERFACE UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN)
target_include_directories(bangbang_options INTERFACE "${CMAKE_CURRENT_BINARY_DIR}/generated")

add_subdirectory(engine)
add_subdirectory(tip)
add_subdirectory(tool)
add_subdirectory(testpad)
add_subdirectory(installer)

option(BANGBANG_BUILD_TESTS "建置單元測試（第一次設定時會下載 GoogleTest）" ON)
if(BANGBANG_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
