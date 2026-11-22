# 單元測試（GoogleTest）。EngineTests.cpp 是 Tests/BangbangEngineTests/ 的 37 個測試，其餘是 Windows 版新增的。
include(FetchContent)
FetchContent_Declare(googletest
    URL https://github.com/google/googletest/archive/refs/tags/v1.18.0.tar.gz
    URL_HASH SHA256=6e3191c1455468b3fc35a417fb565c1c5071aee1b7e7f85e30cf48a98d37d8b5
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
set(gtest_force_shared_crt OFF CACHE BOOL "" FORCE)
set(BUILD_GMOCK OFF CACHE BOOL "" FORCE)
set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)

add_executable(bangbang_tests
    TestSupport.h
    EngineTests.cpp
    WindowsTests.cpp
    RealModelTests.cpp
    Utf8Tests.cpp)
target_include_directories(bangbang_tests SYSTEM PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/../third_party")
# 外殼裡和 TSF 無關的純邏輯（例如 ShiftTapDetector.h）也在這裡測。
target_include_directories(bangbang_tests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/../tip")
# 真模型測試用 repo 裡和 Mac 版共用的模型檔；不存在時測試會自動略過。
target_compile_definitions(bangbang_tests PRIVATE "BANGBANG_MODEL_PATH=u8\"${BANGBANG_REPO_ROOT}/build/bangbang-v3.lm\"")
target_link_libraries(bangbang_tests PRIVATE bangbang_engine bangbang_options GTest::gtest_main)

include(GoogleTest)
gtest_discover_tests(bangbang_tests DISCOVERY_MODE PRE_TEST)
